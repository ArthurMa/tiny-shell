/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2006/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:25:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */


/*typedef struct command_t
{
  char* name; //command absolute path
  char *cmdline; //typein.toString()
  char *redirect_in, *redirect_out;
  int is_redirect_in, is_redirect_out;
  int bg; // bg = 1 => background
  int argc; // arguments number
  char* argv[]; // ls -a
} commandT;
*/
/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  pid_t pid;
  struct bgjob_l* next;
  int jobid;
  int status;// 0 done; 1 running; 2 suspend.
  char *cmdline;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

pid_t fgpid = -1;

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(char*);
/************External Declaration*****************************************/
void AddJobs(pid_t, int, char*, bool);
void PrintJobs();
void RunCmdBg(commandT*);
void RunCmdFg(commandT*);
bgjobL* FindJobs(int jobid);
void RemoveJobs(int);
/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    RunCmdFork(cmd[0], TRUE);
  else{
    RunCmdPipe(cmd[0], cmd[1]);
    for(i = 0; i < n; i++)
      ReleaseCmdT(&cmd[i]);
  }
}

void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd->argv[0]))
  {
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}

//Note that jobs that are running in the background are denoted by "&" 
//when running the jobs command. 
void RunCmdBg(commandT* cmd)
{
  int jobid = atoi(cmd->argv[1]);
  bgjobL* bgjob = FindJobs(jobid);
  if(bgjob == NULL){
    printf("The requiring job is not found\n");
    return;
  }
  if(bgjob->status == 2){
    kill(-(bgjob->pid), SIGCONT);
    bgjob->status = 1;
  }
}

void RunCmdFg(commandT* cmd)
{
  int jobid = atoi(cmd->argv[1]);
  int status;
  bgjobL* fgjob = FindJobs(jobid);
  if(fgjob == NULL){
    printf("The requiring job is not found\n");
    return;
  }
  if(fgjob->status == 2 || fgjob->status == 1){
    fgpid = fgjob->pid;
    fgjob->status = 1;
    //printf("fgjob->pid: %d\n",fgjob->pid);
    kill(-(fgjob->pid), SIGCONT);
    RemoveJobs(jobid);
    //printf("before\n");
    waitpid(fgjob->pid, &status, WUNTRACED);
    //printf("after\n");
    if(WTERMSIG(status) == 127){
      AddJobs(fgjob->pid, 2, fgjob->cmdline, TRUE);
      //PrintJobs();
    }
    fgpid = -1;
  }
  else
    printf("No need to fg this job.\nThe requiring job is either Running or Done\n");
}


void RunCmdPipe(commandT* cmd1, commandT* cmd2)
{
}

void RunCmdRedir(commandT* cmd){
  int fileNameIn = open(cmd->redirect_in, O_RDONLY);
  int fileNameOut = creat(cmd->redirect_out, 0777);
  dup2(fileNameIn,0);
  dup2(fileNameOut, 1); 
  close(fileNameIn);
  close(fileNameOut);
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
  int fileName = creat(file, 0777);
  dup2(fileName, fileno(stdout));
  close(fileName);
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
  int fileName = open(file, O_RDONLY);
  dup2(fileName, 0);
  close(fileName);
}


/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
    Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
    ReleaseCmdT(&cmd);
  }
}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf); 
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

/*
In eval, the parent must use sigprocmask to block SIGCHLD signals 
before it forks the child, and then unblock these signals, 
again using sigprocmask after it adds the child to the job list by calling addjob. 
Since children inherit the blocked vectors of their parents, 
the child must be sure to then unblock SIGCHLD signals before it execs the new program. 
The parent needs to block the SIGCHLD signals in this way in order to avoid the race condition 
where the child is reaped by sigchld handler (and thus removed from the job list) before the parent calls addjob.
*/
static void Exec(commandT* cmd, bool forceFork)
{
  //sometimes we dont want to handle signal immediately
  //also dont want to ignore signal
  //we need delay 
  //this is proc mask
  sigset_t blockset;
  sigemptyset(&blockset);//initial an empty signal set
  sigaddset(&blockset, SIGCHLD);//add sigchld to signal set
  // block sigchld signals until recording the new process id,
  // and then fork the foreground process
  sigprocmask(SIG_BLOCK, &blockset, NULL);

  if (forceFork) {//fork
    pid_t pid = fork();
    int status;
    if (pid == 0) {
      //child process
      setpgid(0, 0);// set pgid = process group id.
      if(cmd->is_redirect_in && cmd->is_redirect_out)
        RunCmdRedir(cmd);
      else if(cmd->is_redirect_out)
        RunCmdRedirOut(cmd, cmd->redirect_out);
      else if(cmd->is_redirect_in)
        RunCmdRedirIn(cmd, cmd->redirect_in);
      execv(cmd->name, cmd->argv);
    }
    else {
      //parent process
      //where to unblock
      
      if (cmd->bg) {//'&'
        AddJobs(pid, 1, cmd->cmdline, FALSE);
        sigprocmask(SIG_UNBLOCK, &blockset, NULL);
      }
      else {
        waitpid(pid, &status, WUNTRACED);//WUNTRACED?
        fgpid = pid;//handle signal
        if (WTERMSIG(status) == 127) {//WTERMSIG?
          //If the value of WIFSIGNALED(stat_val) is non-zero
          //this macro evaluates to the number of the signal 
          //that caused the termination of the child process.
          AddJobs(pid, 2, cmd->cmdline, TRUE);
          sigprocmask(SIG_UNBLOCK, &blockset, NULL);
          setbuf(stdout, NULL);//setbuf?
        }
        fgpid = -1;
      }
    }
  }
  else
    execv(cmd->name, cmd->argv);
}

static bool IsBuiltIn(char* cmd) //Done!
{
  if (strcmp(cmd, "bg") == 0) return TRUE;
  if (strcmp(cmd, "fg") == 0) return TRUE;
  if (strcmp(cmd, "jobs") == 0) return TRUE;
  if (strcmp(cmd, "alias") == 0) return TRUE;
  if (strcmp(cmd, "unalias") == 0) return TRUE;
  return FALSE;     
}


static void RunBuiltInCmd(commandT* cmd)//Done!
{
  if (strcmp(cmd->argv[0], "jobs") == 0) 
    PrintJobs();
  else if (strcmp(cmd->argv[0], "bg") == 0) 
    RunCmdBg(cmd);
  else if (strcmp(cmd->argv[0], "fg") == 0)
    RunCmdFg(cmd);
}

void PrintJobs(){
  bgjobL* cur = bgjobs;
  while(cur){
    if(cur->status == 2){
      //char temp[1024];
      //strcat(temp, cur->cmdline);
      //strcat(temp, "&");
      int i;
      for(i = strlen(cur->cmdline) - 1; i >= 0; i--){
        if(cur->cmdline[i] == '&'){
          cur->cmdline[i] = '\0';
          break;
        }
      }
      //cur->cmdline[strlen(cur->cmdline); i-1] = 0;
      setbuf(stdout, NULL);
      printf("[%d]   Stopped                 %s\n", cur->jobid, cur->cmdline);
    }
    else if(cur->status == 1){
      if(waitpid(cur->pid, NULL, WNOHANG || WUNTRACED) == 0){
        char* temp = cur->cmdline;
        //strcat(temp, cur->cmdline);
        if(cur->cmdline[strlen(cur->cmdline) - 1] != '&'){
          if(cur->cmdline[strlen(cur->cmdline) - 1] != ' ')
            strcat(temp, " ");
          strcat(temp, "&");
        }
        setbuf(stdout, NULL);
        printf("[%d]   Running                 %s\n", cur->jobid, temp);
      }
    }
    cur = cur->next;
  }
}

//goes through the list of your jobs
//remove jobs that finished
//print that a "Done" status for any background job. 
void CheckJobs()
{
  if (!bgjobs) return;
  int status;
  while (bgjobs != NULL) {
    if (waitpid(bgjobs->pid, &status, WNOHANG || WCONTINUED) == bgjobs->pid){
      //if job is done!
      //WNOHANG: parent process shouldn't wait ! if status not available, return 0, if sucessful, return same id, otherwise -1.
      //WUNTRACED: request status information from stopped processes as well as processes that have terminated.
      //WCONTINUED: The waitpid() function shall report the status of any continued child process specified by 
      //pid whose status has not been reported since it continued from a job control stop.
      //waitpid(): on success, returns the process ID of the child whose state has changed;
      //if WNOHANG was specified and one or more child(ren) specified by pid exist, 
      //but have not yet changed state, then 0 is returned. On error, -1 is returned.
      int i;
      for (i = strlen(bgjobs->cmdline) - 1; i >= 0; i--) {
        if (bgjobs->cmdline[i] == '&') {
          bgjobs->cmdline[i] = '\0';
          break;
        }
      }
      setbuf(stdout, NULL); //??
      printf("[%d]   Done                    %s\n", bgjobs->jobid, bgjobs->cmdline);
      bgjobs = bgjobs->next;
    }
    else
      break;
  }
  if(bgjobs == NULL) return;

  bgjobL* curjob = bgjobs->next;
  bgjobL* prevjob = bgjobs;
  while (curjob != NULL) {
    if (waitpid(curjob->pid, &status, WNOHANG)==curjob->pid){
      int i;
      for (i = strlen(bgjobs->cmdline) - 1; i >= 0; i--) {
        if (curjob->cmdline[i] == '&') {
          curjob->cmdline[i] = '\0';
          break;
        }
      }
      setbuf(stdout, NULL);
      printf("[%d]   Done                    %s\n", curjob->jobid, bgjobs->cmdline);
      prevjob->next = curjob->next;
    }
    else
      prevjob = prevjob->next;
    curjob = curjob->next;
  }
}

bgjobL* FindJobs(int jobid){
  if(bgjobs == NULL) return bgjobs;
  bgjobL* cur = bgjobs;
  while(cur){
    if(cur->jobid == jobid)
      return cur;
    cur = cur->next;
  }
  printf("Requiring job is not found\n");
  return NULL;
}

void RemoveJobs(int jobid){ // this func garantee the job can be found
  if(bgjobs->jobid == jobid){
    bgjobs = bgjobs->next;
    return;
  }
  bgjobL* curjob = bgjobs;
  bgjobL* prevjob = malloc(sizeof(bgjobL));
  prevjob->next = bgjobs;
  while(curjob->jobid != jobid){
    curjob = curjob->next;
    prevjob = prevjob->next;
  }
  prevjob->next = curjob->next;
}

void AddJobs(pid_t pid, int status, char* cmdline, bool printFlag){
  //printf("add jobs pid: %d\n", pid);
  bgjobL* newjob = malloc(sizeof(bgjobL));
  newjob->pid = pid;
  newjob->status = status;
  newjob->next = NULL;
  newjob->cmdline = cmdline;
  if(bgjobs == NULL){
    newjob->jobid = 1;
    bgjobs = newjob;
    if(printFlag){
      int i;
      for(i = strlen(newjob->cmdline) - 1; i >= 0; i--){
        if(newjob->cmdline[i] == '&'){
          newjob->cmdline[i] = '\0';
          break;
        }
      }
    printf("[%d]   Stopped                 %s\n", newjob->jobid, newjob->cmdline); 
    //printf("Executing background job[%d]\n", newjob->jobid);
    }
    return;
  }
  bgjobL* curjob = bgjobs;
  while(curjob->next){
    curjob = curjob->next;
  }  
  newjob->jobid = curjob->jobid + 1; 
  curjob->next = newjob;
  
  if(printFlag){
    int i;
    for(i = strlen(newjob->cmdline) - 1; i >= 0; i--){
      if(newjob->cmdline[i] == '&'){
        newjob->cmdline[i] = '\0';
        break;
      }
    }
    printf("[%d]   Stopped                 %s\n", newjob->jobid, newjob->cmdline); 
  }     
}

commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}
