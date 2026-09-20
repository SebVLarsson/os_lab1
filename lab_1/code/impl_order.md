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
