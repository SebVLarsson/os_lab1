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


int main(void)
{
  for (;;)
  {
    char *line;
    line = readline("> ");
    
    destroy_bg_jobs(); // before we start another iteration, we're just gonna ensure we can clean up any bg jobs that are finished
    
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
                  pids[i] = pid;
                }
              }

              for (size_t j = 0; j < pipes; j++)
              {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
              }

              if (!cmd.background)
              {
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
              }
              else 
              { 
                for (size_t i = 0; i < cmd_size; i++)
                {
                  if (pids[i] != -1)
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
              if (execvp(cmd.pgm->pgmlist[0], cmd.pgm->pgmlist) == -1)
              {
                perror("execvp failed");
                exit(EXIT_FAILURE);
              }
            }
            else // parent
            {
              if (!cmd.background) // check for background, not implemented yet
              {
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


// If a job is called to be a background job, we cannot waidpid as we did before with foreground jobs
// Because of that we need to ensure we have a function we can periodically call, such as every main loop iteration
// essentially we loop through the static array of background jobs, WNOHANG will always return status immediately
// if a result is greater than 0, we know its finished and can subsequently remove it from the array
// we then replace it with the last, decrement i and rerun the loop (to ensure we dont randomly skip the job we replaced the finished with)
void destroy_bg_jobs()
{
  for (int i = 0; i < bg_job_count; i++)
  {
    int status;
    pid_t result = waitpid(bg_jobs[i], &status, WNOHANG);
    if (result > 0)
    {
      printf("bg job %d finished\n", bg_jobs[i]);
      bg_jobs[i] = bg_jobs[bg_job_count - 1];
      bg_job_count--;
      i--;
    }
  }
}

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