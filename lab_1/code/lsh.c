/*
 * Main source file for the lsh shell program.
 *
 * You are free to add functions to this file.
 * If you want to add functions in separate files,
 * you will need to modify CMakeLists.txt to compile
 * your additional files.
 *
 * Add appropriate comments to make your code
 * easier for us to grade.
 *
 * Using assert statements is a good way to catch errors early and make debugging easier.
 * Think of them as mini self-checks that ensure your program behaves as expected.
 * By setting up these guardrails, you're creating a more robust and maintainable solution.
 * So go ahead, sprinkle some asserts in your code; they're your friends in disguise!
 *
 * All the best!
 */
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

// The <unistd.h> header is your gateway to the OS's process management facilities.
#include <unistd.h>

#include "parse.h"

// background jobs, keeping it small
#define MAX_BG_JOBS 16
static pid_t bg_jobs[MAX_BG_JOBS];
static int bg_job_count = 0;

static void print_cmd(Command *cmd);
static void print_pgm(Pgm *p);
void stripwhite(char *);
void destroy_bg_jobs();
int is_builtin(Pgm *p);
void builtin_cd(Pgm *p);
void builtin_exit();
void sigchld_handler(int sig);


int main(void)
{
  // setting up signal handles
  struct sigaction sa;
  sa.sa_handler = SIG_IGN; // sigaction, default
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  // we're adding SIGINT, SIGTTIN (TERMINAL INPUT), SIGTTOU (TERMINAL OUTPUT) into the handler, so that they can be ignored by the shell
  if (sigaction(SIGINT, &sa, NULL) == -1)
  {
    perror("sigaction fail: INT");
    exit(EXIT_FAILURE);
  }

  if (sigaction(SIGTTIN, &sa, NULL) == -1)
  {
    perror("sigaction fail: IN");
    exit(EXIT_FAILURE);
  }

  if (sigaction(SIGTTOU, &sa, NULL) == -1)
  {
    perror("sigaction fail: OUT");
    exit(EXIT_FAILURE);
  }

  struct sigaction sc; //sigaction child, special case for SIGCHLD
  sc.sa_handler = sigchld_handler; // upon a signal event, we want to call a predetermined funciton (that will clean up finished bg jobs)
  sigemptyset(&sc.sa_mask);
  sc.sa_flags = SA_RESTART;

  // the whole deal here is that once a bg job is finished, the child status changes which the kernel will interpret and forward the process to th defined function via the handler
  // it allows the shell to not have to wait for the next iteration of the loop
  if (sigaction(SIGCHLD, &sc, NULL) == -1)
  {
    perror("sigaction fail: CHLD");
    exit(EXIT_FAILURE);
  }

  for (;;)
  {

    // setting the shell to be a foreground process group
    // allowing shell to be process id 0, and have its own process group id
    // also allowing it to take input from terminal, not blocked by any other process group
    setpgid(0, 0);
    tcsetpgrp(STDIN_FILENO, getpid());

    //OBSOLETE, replaced by signal handler
    //destroy_bg_jobs(); // before we start another iteration, we're just gonna ensure we can clean up any bg jobs that are finished

    char *line;
    line = readline("> ");
    
    // EOF Check add signal handling later
    if (line == NULL)
    {
      printf("EOF reached, exiting shell.\n");
      builtin_exit();
      break; 
    }

    // Remove leading and trailing whitespace from the line
    stripwhite(line);

    // If the stripped line is not blank
    if (*line)
    {
      add_history(line);

      Command cmd;
      if (parse(line, &cmd) == 1)
      {
        // control var to see if its a builtin
        // 0 for not, 1 for cd, 2 for exit
        int builtin = is_builtin(cmd.pgm);

        switch (builtin)
        {
          case 1:
            builtin_cd(cmd.pgm);
            break;
          case 2:
            builtin_exit();
            break;
          default:
            break;
        }

        if (builtin == 0) 
        {
          // we want to count the commands so that we can reverse the order (so it becomes correct) abit later
          size_t cmd_size = 0;
          Pgm *temp = cmd.pgm;
          while (temp != NULL)
          {
            cmd_size++;
            temp = temp->next;
          }

          if (cmd_size > 1) // pipe branch
          {
            int pipe_status = 0; // setting a pipe_status to know if we need to cancel forking or if its safe to continue

            // array of pointers to the "argv" arrays of each command
            char *** cmd_array = malloc(cmd_size * sizeof(char**));
            Pgm *p = cmd.pgm;
            // reversing the order of the commands so we can go by 0 indexing, this means we would avoid having to traverse the LL more than once
            for (size_t i = cmd_size; i > 0; i--)
            {
              cmd_array[i - 1] = p->pgmlist;
              p = p->next;
            }

            // need to create and store our fds in advance so we can dup and close them in the child processes
            int** pipe_fds = malloc((cmd_size - 1) * sizeof(int*));

            // Since we want to unconditionally free and close any amount of pipes at the end of the branch, we keep track of how many pipes we create successfully
            // in case of failure halfway through, we know exacly how many pipes we need to close at the end of the branch
            // this is just to prevent having duplicate code and keeping it more readable
            size_t pipes = 0;

            // initializing a new pgid
            pid_t pgid = 0;

            // creating all pipe fds in advance
            // if a pipe fails, we set the pipe_status to -1 to indicate something went wrong and we know to cancel and clean it up rather than continuing a broken pipe chain
            for (size_t i = 0; i < cmd_size - 1; i++)
            {
              pipe_fds[i] = malloc(2 * sizeof(int));
              if (pipe(pipe_fds[i]) == -1)
              {
                perror("pipe failed");
                pipe_status = -1;
                break;
              }
              pipes++;
            }

            // array to hold the pids, we initialize it to NULL because we do not want any grabage values in it
            // reason being that if we skip a fork() due to failure, we want to avoid an edge case where we try to wait for a pid that doesnt exist
            pid_t *pids = NULL;
            if (pipe_status == -1) // if something with piping failed, skip the fork stage and go straight to freeing memory
            {
              printf("piping failed\n");
            }
            else { // successful case
              pids = malloc(cmd_size * sizeof(pid_t));

              for (size_t i = 0; i < cmd_size; i++)
              {
                pid_t pid = fork();
                if (pid < 0) // fail case
                {
                  // adding a sentinel value for failed forks
                  // there are more correct ways to handle this
                  // however, for now simplicity will do
                  pids[i] = -1;
                  perror("fork failed");
                }
                else if (pid == 0) // child
                {

                  // setting the pgid of the child to the same as the first child, so that we group all children related to the same pipeline together
                  // this allows us to send signal to the entire pipeline at once
                  setpgid(0, (i == 0) ? 0 : pgid);

                  if (i > 0) // if not first stage, dup2 needs to READ from previous pipe
                  {
                    dup2(pipe_fds[i - 1][0], STDIN_FILENO);
                  }

                  // if first stage AND redirection IN is not NULL, ensure we open a new file descriptor and dup2 it into the stdin of the child
                  // NOTE: not fan of having stray fds, ask TAs for ideas
                  if (i == 0 && cmd.rstdin)
                  {
                    int fd = open(cmd.rstdin, O_RDONLY);
                    if (fd == -1)
                    {
                      perror("open failed");
                      exit(EXIT_FAILURE);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                  }
                
                  if (i < cmd_size - 1) // if not last stage, dup2 needs to WRITE to next pipe
                  {
                    dup2(pipe_fds[i][1], STDOUT_FILENO);
                  }

                  // if last stage AND redirection OUT is not null, open new fd and dup2 it into the stdout of the child
                  // NOTE: not fan of having stray fds, ask TAs for ideas
                  if (i == cmd_size - 1 && cmd.rstdout)
                  {
                    int fd = open(cmd.rstdout, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1)
                    {
                      perror("open failed");
                      exit(EXIT_FAILURE);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                  }

                  // close all pipe fds in child
                  for (size_t j = 0; j < cmd_size - 1; j++)
                  {
                    close(pipe_fds[j][0]);
                    close(pipe_fds[j][1]);
                    free(pipe_fds[j]);
                  }


                  // resetting the sigint handler to default rather than ignore for children
                  signal(SIGINT, SIG_DFL);

                  // execvp doesnt return if successful so we only need to know if it failed
                  if (execvp(cmd_array[i][0], cmd_array[i]) == -1)
                  {
                    perror("execvp failed");
                    exit(EXIT_FAILURE);
                  }
                }
                // parent stores the pid in the pid array
                else
                {
                  // if we're not the first child, we set the pgid to the same as the first child, group all other the same pgid umbrella
                  if (i == 0) pgid = pid;
                  setpgid(pid, pgid);

                  pids[i] = pid;
                }
              }

              // close the pipes
              for (size_t j = 0; j < pipes; j++)
              {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
              }

              if (!cmd.background)
              { // if foreground job

                // set terminal control to the process group of first child so it can take input from terminal, not blocked by shell
                // i.e for piping
                tcsetpgrp(STDIN_FILENO, pgid);

                // foreground wait() loop
                for (size_t i = 0; i < cmd_size; i++)
                {
                  if (pids[i] == -1) continue; // skip waiting for failed forks

                  int status;
                  if (waitpid(pids[i], &status, 0) < 0)
                  {
                    perror("waitpid failed");
                  }
                  else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) // if child exited with error, print error message
                  {
                    fprintf(stderr, "child process %d exited, error: %d\n", pids[i], WEXITSTATUS(status));
                  }
                  else {
                    printf("child process %d successful\n", pids[i]);
                  }
                }
                
                // set terminal control back to the shell since children finished
                tcsetpgrp(STDIN_FILENO, getpid());
              }
              else 
              { // if background job
                for (size_t i = 0; i < cmd_size; i++)
                {
                  if (pids[i] != -1) // -1 is our sentinel value for failed forks
                  {
                    if (bg_job_count < MAX_BG_JOBS) 
                    { // if bg job, add to our static array and increment counter
                      bg_jobs[bg_job_count++] = pids[i];
                    }
                    else 
                    { // if max bg jobs reached
                      fprintf(stderr, "max bg jobs reached, %d not added\n", pids[i]);
                    }
                  }
                  printf("debug: background job %d started\n", pids[i]);
                }
              }
            }

            // free memory
            for (size_t i = 0; i < pipes; i++)
            {
              free(pipe_fds[i]);
            }
            free(pipe_fds);
            free(cmd_array);
            free(pids);
          }
          else // single command branch
          {
            pid_t pid = fork();

            if (pid < 0) // fail case
            {
              perror("fork failed");
            } 
            // child, executing command, if command returns anything we know it failed so we throw up error message and exit
            // if no return, it executed successfully and exit process
            else if (pid == 0) 
            {

              // setting child process to be its own process group so it can take input from terminal
              setpgid(0, 0);

              // checking if redirection IN is not null, if it isnt open fd and dup2 
              if (cmd.rstdin)
              {
                int fd = open(cmd.rstdin, O_RDONLY);
                if (fd == -1)
                {
                  perror("open failed");
                  exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
              }

              // check if redirection for out is not null, if it isnt open new fd and dup2
              if (cmd.rstdout)
              {
                int fd = open(cmd.rstdout, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1)
                {
                  perror("open failed");
                  exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
              }

              // setting signal handler back to default so it can be interrupted by ctrl+c and not ignore like the shell
              signal(SIGINT, SIG_DFL);

              // execvp doesnt return if successful so, verbal fail, silent success
              if (execvp(cmd.pgm->pgmlist[0], cmd.pgm->pgmlist) == -1)
              {
                perror("execvp failed");
                exit(EXIT_FAILURE);
              }
            }
            else // parent
            {
              
              //parent gets it own group since its a single job
              setpgid(pid, pid);

              if (!cmd.background) // check for background
              {
                
                tcsetpgrp(STDIN_FILENO, pid);

                int status;
                if (waitpid(pid, &status, 0) < 0)
                {
                  perror("waitpid failed");
                }
                else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                {
                  fprintf(stderr, "child process %d exited, error: %d\n", pid, WEXITSTATUS(status));
                }
                else {
                  printf("child process %d successful\n", pid);
                }

                tcsetpgrp(STDIN_FILENO, getpid());

              }
              else // if bg job, add to our static array and increment counter
              {
                if (bg_job_count < MAX_BG_JOBS)
                {
                  bg_jobs[bg_job_count++] = pid;
                  printf("debug: background job %d started\n", pid);
                }
              }
            }
            // Print the parsed command
            print_cmd(&cmd);
          }
        }
      }
      else
      {
        printf("Parse ERROR\n");
      }
    }

    // Free the input buffer
    free(line);
  }

  return 0;
}

/*
 * Print a Command structure as returned by parse on stdout.
 *
 * Helper function, no need to change. Might be useful to study as inspiration.
 */
static void print_cmd(Command *cmd_list)
{
  printf("------------------------------\n");
  printf("Parse OK\n");
  printf("stdin:      %s\n", cmd_list->rstdin ? cmd_list->rstdin : "<none>");
  printf("stdout:     %s\n", cmd_list->rstdout ? cmd_list->rstdout : "<none>");
  printf("background: %s\n", cmd_list->background ? "true" : "false");
  printf("Pgms:\n");
  print_pgm(cmd_list->pgm);
  printf("------------------------------\n");
}

/* Print a linked list of Pgm structures.
 *
 * Helper function, no need to change. It may be useful to study for inspiration.
 */
static void print_pgm(Pgm *p)
{
  if (p == NULL)
  {
    return;
  }
  else
  {
    char **pl = p->pgmlist;

    /* The list is stored in reverse order, so print
     * it in reverse to restore the original order.
     */
    print_pgm(p->next);
    printf("            * [ ");
    while (*pl)
    {
      printf("%s ", *pl++);
    }
    printf("]\n");
  }
}


/* Strip whitespace from the start and end of a string.
 *
 * Helper function, no need to change.
 */
void stripwhite(char *string)
{
  size_t i = 0;

  while (isspace(string[i]))
  {
    i++;
  }

  if (i)
  {
    memmove(string, string + i, strlen(string + i) + 1);
  }

  i = strlen(string) - 1;
  while (i > 0 && isspace(string[i]))
  {
    i--;
  }

  string[++i] = '\0';
}


// OBSOLETE, replaced by signal handler
//// If a job is called to be a background job, we cannot waidpid as we did before with foreground jobs
//// Because of that we need to ensure we have a function we can periodically call, such as every main loop iteration
//// essentially we loop through the static array of background jobs, WNOHANG will always return status immediately
//// if a result is greater than 0, we know its finished and can subsequently remove it from the array
//// we then replace it with the last, decrement i and rerun the loop (to ensure we dont randomly skip the job we replaced the finished with)
//void destroy_bg_jobs()
//{
//  for (int i = 0; i < bg_job_count; i++)
//  {
//    int status;
//    pid_t result = waitpid(bg_jobs[i], &status, WNOHANG);
//    if (result > 0)
//    {
//      printf("bg job %d finished\n", bg_jobs[i]);
//      bg_jobs[i] = bg_jobs[bg_job_count - 1];
//      bg_job_count--;
//      i--;
//    }
//  }
//}

// We need to check if the command is a valid builtin command
// we start by ensuring that the function was called with a valid pgm
// if it is, we check if its cd or exit, return 1 if cd, 2 if exit
// if invalid return 0
int is_builtin(Pgm *p)
{
  if (p != NULL && p->next == NULL)
  {
    if (strcmp(p->pgmlist[0], "cd") == 0) 
    {
      return 1;
    } 

    else if (strcmp(p->pgmlist[0], "exit") == 0) 
    {
      return 2;
    }
  }
  return 0;
}

void builtin_cd(Pgm *p)
{
  // set target to second arg
  char *target = p->pgmlist[1];
  if (target == NULL) // check for NULL meaning no path given
  {
    target = getenv("HOME"); // try set path to home env
    if (target == NULL) // if still null, theres no home env so throw error and return
    {
      fprintf(stderr, "cd: home not set\n");
      return;
    }
  }
  else if (p->pgmlist[2] != NULL) // if theres a second arg, user used incorrectly, throw error and return
  {
    fprintf(stderr, "cd: too many args\n");
    return;
  }

  // we call chdir inside if statement for verbal fail but silent success
  if (chdir(target) == -1) 
  {
    perror("cd fail");
  }
}

void builtin_exit()
{
  for (int i = 0; i < bg_job_count; i++)
  { // loop through and kill all bg jobs, their status is irrelevant
    kill(bg_jobs[i], SIGKILL);
  }

  for (int i = 0; i < bg_job_count; i++)
  { 
    // quick loop just to ensure they're all exited, SIGKILL is not instant instant, may take a few ms
    // null instead of &status because we dont care about their status
    // we just care about them actually being dead before we exit shell
    waitpid(bg_jobs[i], NULL, 0);
  }
  exit(0);
}

// function we feed to the child signal handler
void sigchld_handler(int sig)
{
  // so the way the kernel handles signal functions is that they _HAVE_ to take an int as argument
  // since we dont really care about that and just need the function to be called as we loop through all the jobs
  // we cast the int the void to avoid compiler warnings regarding unused variables
  // if our function was abit more elaborate and not a be all end all, get rid of bg jobs glorified loop
  // then we'd use the int, but since we just wanna walk through ALL the bg jobs, we dont need an id to check what to remove
  (void)sig;
  for (int i = 0; i < bg_job_count; i++)
  {
    int status;
    // WNOHANG will return if child processes are still running, so if result not greater than 0, its still running
    pid_t result = waitpid(bg_jobs[i], &status, WNOHANG);
    if (result > 0)
    {
      bg_jobs[i] = bg_jobs[bg_job_count - 1];
      bg_job_count--;
      i--;
    }
  }
}