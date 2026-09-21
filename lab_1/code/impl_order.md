# Order of Implementation

### 1.  Implement simple functions first (~2 hours, documentation reading bonanza)
    1. Read up on fork() and how to exec() (and variants)
    2. Implemented simple usages such as date, who and ls

### 2. Piping (~3.5 hours)
    1. Reverse order is a problem, so start by implementing a simple LL to array
    2. Scratched re-creating LL into array
    3. Went with storing pointers to each entry instead in reverse order
    4. Count the amount of entries and then subsequently map each argv in reverse order
    5. Implement an array to hold the file descriptors for each pipe FIRST
    6. After the 2 int array is created, run the pipe in the if statement
    7. Realised we need a control variable if something fails, so put in pipe_status
    8. Create an array to hold each childs pid for the parent to keep track for waiting on
    9. implement the forking loop for each child, dup2 correctly
       1. dup2 correctly for first and last value since all others are read previous, write next
    10. Spent ALOT of time figuring out how to keep track and do unconditional free'ing and closing
    11. Realised I need to keep track of HOW many pipes we have created to facilitate unconditional closing/freeing, added 'pipes' control variable
    12. Implement the parent waiting group
    13. Refactor scoping and code to properly free memory and close pipes in every (?) case
    14. Realised there are cases if a fork fails, we may have garbage values
        1. Upon fork() failure, overwrite current loop pid with sentinel value
        2. Implement sentinel value check during waiting to prevent edge case deadlock

### 3. I/O Redirection (~3 hours, documentation bonanza part 2)
    1. Added a cmd.rstdin exist check in single command branch, if exist open a new fd
    2. Added error check -1, if no error dup2 the new fd and close it
    3. Added same logic for cmd.rstdout
    4. Added same logic for multi cmd to only apply to first or last stage
    5. Refactor multiple times trying to get pesky bug
        1. Bug was due to ordering
        2. Second bug appeared where a closing bracket was accidently added
    

### 4. Background Execution (~40 minutes, 5 minutes of code, 10 minutes of googling and 10 minutes of bugfixing)
    1. Because of how I built the cmd.background checks, conceptually and realistically extremely easy to implement
        1. In retrospect, need to figure out how to get bg jobs to finish without a keyboard action (?) get to this later
    2. I felt from the start the easiest way to keep track of the processes was just to have a static control int and a static array to store them
    3. Add the else statements to the if !cmd.background in both branches
    4. Add a function to kill finished bg processes
        1. Spent about 10 minutes googling to try and find how to get children to instantly return their status
    5. Spent 10 minutes trying to figure out why compiler is telling me bg_pids is undeclared, turns out, I am an idiot for writing bg_pids instead of the static array which i named bg_jobs


### 5. Builtin's (2-2½ hours)
    1. Started off by researching exacly what this meant, how to reach and how to execute replicas of this
    2. Started off with identifying if a command was a builtin with is_builtin function
        1. Originally just returned 1 for builtin, but I realised I had to swap approach to identify cd and kill differently
        2. turned into string compare function returning 1 for cd, 2 for exit and 0 for not being a builtin
    3. Added the logic into the main loop (Took a while figuring out where and how to do it)
        1. Started with no control variable, turned out I need two separate cases (implemented switch case)
        2. changed the mainloop to check if builtin was identified or not
    4. Implemented builtin_cd which seemed fairly simple as it was just two calls I had to keep track of and learn
        1. getenv because typing simply cd should send you to home environment
        2. chdir which was fairly simple, also learnt that it supports '..' by default so saved from having to implement that
    5. Had to debug for abit because I accidently set target to p->pgmlist[0]
    6. Once again went back to documentation to figure out HOW to KILL processes (Ask TA, this is fascinating)
        1. This is an absolute rabbithole, I went with SIGKILL only because it was simple
        2. Theres supposedly also multiple "levels" of terminating/killing a process
        3. From what I gathered, if you're doing this more professionally, you should almost always SIGTERM with a grace period before you kill anything off
        4. I also learnt that EVEN SIGKILL which is an absolute kill switch is still just a signal "kernel, do this". because of this we still need to wait for the processes to be killed
    7. Added a simple waitpid loop to ensure all processes are killed before actually exiting
    8. Lastly, added the builtin_exit to EOF (CTRL+D) logic


## 6. CTRL+C aka SIGNAL HANDLING (I dont even know how much time, but I am really tired of documentation :)
    1. Implemented signal handler for standard sigaction
        1. HOURS of documentation and fighting the compiler
    2. Refactored and added a sigaction for children aswell
        1. More documentation, more fighting the compiler
    3. Added logic to ensure that the function runs when it should
        1. Fighting the compiler
    4. Refactored AGAIN because things didn't work as planned
        1. More documentation and wanting to kill the compiler
    5.  Removed destroy_bg_jobs etc to clean up code base