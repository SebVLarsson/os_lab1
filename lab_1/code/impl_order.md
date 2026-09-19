## Order of Implementation

** 1. Implement simple functions first (~2 hours, documentation reading bonanza)
    1.1. Read up on fork() and how to exec() (and variants)
    1.2. Implemented simple usages such as date, who and ls

** 2. Piping (~3.5 hours)
    2.1. Reverse order is a problem, so start by implementing a simple LL to array
    2.2. Scratched re-creating LL into array
    2.3. Went with storing pointers to each entry instead in reverse order
    2.4. Count the amount of entries and then subsequently map each argv in reverse order
    2.5. Implement an array to hold the file descriptors for each pipe FIRST
    2.6. After the 2 int array is created, run the pipe in the if statement
    2.7. Realised we need a control variable if something fails, so put in pipe_status
    2.8. Create an array to hold each childs pid for the parent to keep track for waiting on
    2.9. implement the forking loop for each child, dup2 correctly
        2.9.1. dup2 correctly for first and last value since all others are read previous, write next
    2.10. Spent ALOT of time figuring out how to keep track and do unconditional free'ing and closing
    2.11. Realised I need to keep track of HOW many pipes we have created to facilitate unconditional closing/freeing, added 'pipes' control variable
    2.12. Implement the parent waiting group
    2.13. Refactor scoping and code to properly free memory and close pipes in every (?) case
    2.14.1. Realised there are cases if a fork fails, we may have garbage values
    2.14.2. Upon fork() failure, overwrite current loop pid with sentinel value
    2.14.3. Implement sentinel value check during waiting to prevent edge case deadlock