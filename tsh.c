 /* 
 * tsh - A tiny shell program with job control
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* Per-job data */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, FG, BG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

volatile sig_atomic_t ready; /* Is the newest child in its own process group? */

/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);
void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);
void sigusr1_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int freejid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

void set_ignored();
void set_not_ignored();


// OUR HELPER FUNCTIONS
int jid_or_pid(const char *pid_or_jid, char c);
char** readFromFile(const char* filename);
void do_piping(char ** argv, int argc);


/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(STDOUT_FILENO, STDERR_FILENO);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != -1) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }

    /* Install the signal handlers */

    Signal(SIGUSR1, sigusr1_handler); /* Child is ready */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) {
	// First we need to take in the cmdline, then we need to parse it into the different arguments
	// int num_tokens; // Number of arguments
	char * arguments[MAXARGS]; // Array holding the arguments (Max_args used here as placeholder to number of arguments)
	int num_tokens = parseline(cmdline, arguments); // take cmd line and process it into arguments
    // Comandline should consist of a name and zero or more arguments
	// I.e. Arguments[0] should be the name

    pid_t process_pid; //to store the pid of the newly created process
    if (num_tokens == 0 || arguments[0] == NULL){
        return; 
    }
    // If there are no arguments terminate eval immediately 

    int foreground_process = 1;
        // 1 if end with &, otherwise 0
        // 1 - background process
        // 0 -foreground process
    if (strcmp(arguments[num_tokens - 1], "&") == 0){
        foreground_process = 0;
        // Change the type we are running;
        arguments[num_tokens - 1] = NULL; // & replacement
        // We don't want to pass the extra argument - bad for expected execution
        // we do this because last character must be a NULL in execve
    }
    // int getpiped = 0;
    // Check for piping! 
    // Great comment name Ik, thank you very much. Nice lil' easter egg.  
    // for (int i = 0; i < num_tokens; i++) {
        // if (strcmp(arguments[i], "|") == 0) {
            // getpiped = 1;
            // we found a pipe cmd
        // }
    // }
    // if (getpiped == 1) {do_piping(arguments, num_tokens); return;}
    // Deal with the piping (it evaluates all of it then returns)

	int builtin_output = builtin_cmd(arguments);
	// returns 1 if builtincmd otherwise a 0
    // checking if its a background or foreground operation

    // step 2: trying to block signals
    sigset_t signal_set; 
    if (builtin_output == 0){
        // checking if its a foreground or background operation
        
        // Initialize the signal mask to an empty set
        if (sigemptyset(&signal_set) == -1) {
            exit(1);
        }
        // Add SIGCHLD to the signal mask
        sigaddset(&signal_set, SIGCHLD) ;
        // Add SIGINT to the signal mask
        sigaddset(&signal_set, SIGINT);
        // Add SIGSTOP to the signal mask
        sigaddset(&signal_set, SIGSTOP);
        // Block the signals in the parent process
        if (sigprocmask(SIG_BLOCK, &signal_set, NULL) == -1) {
            exit(1);

        }
        // Now we will execute the executable!
        process_pid = fork();
        // checking if we have an error in the child
        if (process_pid == -1) { 
            fprintf(stderr, "An Error has occured.\n");
            return;
        }
        // Fork call should be returning something >= 0. negative if error-prone
        // child process
        else if(process_pid == 0){
            setpgid(0,0); // hint 2
            // hint 2
            sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
            // runs
            // fprintf(stderr, "%s is running.\n", arguments[0]);
            // Doing input and output redirection
            
            // input/output redirection
            // THE BANE OF MY EXISTANCE :(
            // edit: all good now. 
            for (int i = 0; arguments[i] != NULL; i++) {
                if (strcmp(arguments[i], "<") == 0) {
                    int fd = open(arguments[i + 1], O_RDONLY);
                    // Test that the file after the < can infact be read from
                    if (fd < 0) {
                        // it infact could not be read from, big error. 
                        fprintf(stderr, "Error: %s\n", strerror(errno));
                        exit(1);
                    } 
                    dup2(fd, STDIN_FILENO);
                    // Do not close the file descriptor Mike >:( - note to future Mikes
                    // But seriously, who thought that if you pass in just the name of the program,
                    // execve will then take stuff in from STDIN???
                    arguments[i] = NULL; 
                    // Nullify arguments so that execve doesn't try to use < or things after
                    }
                else if (strcmp(arguments[i], ">") == 0) {
                    // Output redirection case
                    int fd = open(arguments[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    // Test that ouput file exists and can be read from
                    if (fd < 0) {
                        // there was a error! Terminate, can't even attempt to read it
                        fprintf(stderr, "Error: %s\n", strerror(errno));
                        exit(1);
                    }
                    // Use dup2 to do the redirect like in lecture
                    dup2(fd, STDOUT_FILENO);
                    // Nullify args so that execve does in fact not try to use unintended args
                    arguments[i] = NULL;
                    arguments[i + 1] = NULL;
                }
            }
            int output = execve(arguments[0], arguments, environ);
            if ( output < 0)
            // Execve error! It should not return or return here ever!  
              {// error occurs if no command
                printf("%s: Command not found\n", arguments[0]); 
                // Bad kaboom! 
                exit(0);
              }
        }
        
        // parent process
        else if (process_pid > 0) {
        // 1 - background process
        // 0 -foreground process
            if (foreground_process) {
                addjob(jobs, process_pid, FG, cmdline);
                // make sure to add to jobs list first
                sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
                // unblock signals here so that job is in list
                waitfg(process_pid);
                // wait for current fg process to terminate (i.e. the new child)

            } else if (!(foreground_process)){
                // Again add to list of jobs
                addjob(jobs, process_pid, BG, cmdline); 
                // unblock the signals only after you've added it to the jobs list
                sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
                // The print statement that you forgot. 
                printf("[%d] (%d) %s", pid2jid(process_pid), (int)process_pid, cmdline);
            }
        }
    
    }
    return;
}

/*
do_piping - Piping
*/
void do_piping(char ** argv, int argc){
// 1st order of business is taking care of the piped commands
// Assume that a piped command i.e. like left | right, left is no more than 4 arguments long b/c why not
// Count the number of piped sections to determine how many seperate cmd arrays you need to keep track of
int pipe_count = 0;

for (int i = 0; i < MAXARGS; i++) {
    if ((strcmp(argv[i], "|") == 0) && (argv[i+1] != NULL)) {pipe_count += 1;}
    // Makes sure its not a BS ending pipe and there is something after it
}
// Use the number of valid pipes to build and fill the cmd arrays.
char * cmd1[MAXARGS];
char * cmd2[MAXARGS];
int counter = 0;
int index = 0;
// Now fill it up;

while (counter < argc) {
    if (argv[counter][0] != '|') {
    strcpy(cmd1[index] ,argv[counter]);
    index += 1;
    counter += 1;}
}
counter += 1; // To get past it
index = 0;
while (counter < argc) {
    if (argv[counter] != NULL) {
        strcpy(cmd2[index], argv[counter]);
        index += 1;
        counter += 1;
    }
}
// TARAS pls fix warnings tmr! :)
int p[2];
int x = pipe(p);
if (x < 0) {// Pretend to do something
perror("Pipe error\n");
exit(EXIT_FAILURE);
}
// So p[0] is read p[1] is write! 
// Just like two exec calls

// step 2: trying to block signals
int builtin_output = builtin_cmd(cmd1);
// Built-in cmd
    sigset_t signal_set; 
    if (builtin_output == 0){
        // checking if its a foreground or background operation
        
        // First cmd in pipe 

        // Initialize the signal mask to an empty set
        if (sigemptyset(&signal_set) == -1) {
            exit(1);
        }
        // Add SIGCHLD to the signal mask
        sigaddset(&signal_set, SIGCHLD) ;
        // Add SIGINT to the signal mask
        sigaddset(&signal_set, SIGINT);
        // Add SIGSTOP to the signal mask
        sigaddset(&signal_set, SIGSTOP);
        // Block the signals in the parent process
        if (sigprocmask(SIG_BLOCK, &signal_set, NULL) == -1) {
            exit(1);

        }
        // Now we will fork
        pid_t process_pid;
        process_pid = fork();
        // checking if we have an error in the child
        if (process_pid == -1) { 
            fprintf(stderr, "An Error has occured.\n");
            return;
        }
        // child process
        else if(process_pid == 0){
            setpgid(0,0); // hint 2
            // hint 2
            sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
            // runs
            // fprintf(stderr, "%s is running.\n", arguments[0]);
            // Doing input and output redirection
            // input/output redirection
            for (int i = 0; cmd1[i] != NULL; i++) {
                if (strcmp(cmd1[i], "<") == 0) {
                    int fd = open(cmd1[i + 1], O_RDONLY);
                    if (fd < 0) {
                        fprintf(stderr, "Error: %s\n", strerror(errno));
                        exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    // Do not close the file descriptor Mike >:( - note to future Mikes
                    cmd1[i] = NULL; 
                    }
                    dup2(p[1], STDOUT_FILENO);
                    cmd1[i] = NULL;
                    cmd1[i + 1] = NULL;
                }
            }
            int output = execve(cmd1[0], cmd1, environ);
            if ( output < 0) 
              {// error occurs if no command
                printf("%s: Command not found\n", cmd1[0]); 
                exit(0);
              }
        } else {
        
        // Second call 

        int builtin_output = builtin_cmd(cmd2);
        int process;
        // Built-in cmd
        sigset_t signal_set; 
        if (builtin_output == 0){
        // checking if its a foreground or background operation
        
        // First cmd in pipe 

        // Initialize the signal mask to an empty set
        if (sigemptyset(&signal_set) == -1) {
            exit(1);
        }
        // Add SIGCHLD to the signal mask
        sigaddset(&signal_set, SIGCHLD) ;
        // Add SIGINT to the signal mask
        sigaddset(&signal_set, SIGINT);
        // Add SIGSTOP to the signal mask
        sigaddset(&signal_set, SIGSTOP);
        // Block the signals in the parent process
        if (sigprocmask(SIG_BLOCK, &signal_set, NULL) == -1) {
            exit(1);

        }
        // Now we will fork
        process = fork();
        // checking if we have an error in the child
        if (process == -1) { 
            fprintf(stderr, "An Error has occured.\n");
            return;
        }
        // child process
        else if(process == 0){
            setpgid(0,0); // hint 2
            // hint 2
            sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
            // runs
            // fprintf(stderr, "%s is running.\n", arguments[0]);
            // Doing input and output redirection
            // input/output redirection
            for (int i = 0; cmd2[i] != NULL; i++) {
                    dup2(p[0], STDIN_FILENO);
                    // Do not close the file descriptor Mike >:( - note to future Mikes
                    cmd2[i] = NULL; 
                    }
                    // Force read from pipe
            }
            int output = execve(cmd2[0], cmd2, environ);
            if ( output < 0) 
              {// error occurs if no command
                printf("%s: Command not found\n", cmd2[0]); 
                exit(0);
              }
        }
        
        // parent process
        else {return;
        }
        return;
        }
    }



/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return number of arguments parsed.
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to space or quote delimiters */
    int argc;                   /* number of args */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;
    
    return argc;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) {
    // array is the same one passed in  ex. [bg, %3]
    // builtin_cmd: Recognizes and interprets the built-in commands: quit, fg, bg, and jobs. 
    int m = 0;
    // Destination array
    char * destinationArray[2];
    //while (argv[m] != NULL) {
        char* command = argv[m];

        // jobs command
        if (strcmp(command, "jobs") == 0) {
            listjobs(jobs); // this method lists the given jobs
            return 1; // (iffy) must return 1 to end command
        }
        // quit command
        if (strcmp(command, "quit") == 0) {
            exit(EXIT_SUCCESS); // must to say success
            return 1;
        }
        // bg commands
        if (strcmp(command, "bg") == 0) {
            destinationArray[0] = argv[m];
            destinationArray[1] = argv[m+1];
            do_bgfg(destinationArray);
            return 1; // must return 1 to say success
        }
        // fg command
        if (strcmp(command, "fg") == 0) {
            destinationArray[0] = argv[m];
            destinationArray[1] = argv[m+1];
            do_bgfg(destinationArray);
            return 1; // must return 1 to say builtin
        }
        m += 1;
    // }
    return 0; // return 0 if not builtin    /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
    char* command = argv[0];
    char* pid_or_jid = argv[1];
    // bg %5987
    // bg commands
    struct job_t *job;
    pid_t pid_num = 0;
    // error checking
    if (pid_or_jid == NULL){
        printf("%s command requires PID or %%jid argument\n", command);
        return;
    }
    if (strcmp(command, "bg") == 0) {
        // if it is a JID(ex.bg %5)
        if (pid_or_jid[0] == '%'){
            char* substring = &pid_or_jid[1];
            // Convert the substring to an integer
            int num = atoi(substring);
            if (num == 0){   
            printf("%s: argument must be a PID or %%jid\n", argv[0]);
            return;
            }
            job = getjobjid(jobs, num); // pointer to struct
               if ( job == NULL ) {
                printf ( "%s: No such job\n", command);
                return;
                }
            pid_num = job->pid;
            if (kill(-pid_num, SIGCONT) != 0) {
               perror("Failed to send SIGCONT signal");
               return;
            }
            // changing state
            job->state = BG;
            printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
        }
        // its PID
        else {
            int num = atoi(pid_or_jid);  // Convert string to integer
            // if atoi returns 0, its not number
            if (num == 0){   
            printf("%s: argument must be a PID or %%jid\n", argv[0]);
            return;
            }
            job = getjobpid(jobs, num);
             if ( job == NULL ) {
            printf ( "(%d): No such process\n", num);
            return;
            }
            pid_num = job->pid;
            if (kill(-pid_num, SIGCONT) != 0) {
                perror("Failed to send SIGCONT signal");
                return;
            }
            // changing state
            job->state = BG;
            printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
        }
        // if it is a PID(ex. bg 2)
        return; // must return to say success
    }
    // fg command
    if (strcmp(command, "fg") == 0) {

	// Need to account for valid jid i.e. can't assume % is always there; cause not guarenteed to be a valid PID.

         // if it is a JID(ex.bg %5)
        if (pid_or_jid[0] == '%'){
            char* substring = &pid_or_jid[1];
            // Convert the substring to an integer
            int num = atoi(substring);
            // if atoi returns 0, its not number
            if (num == 0){   
            printf("%s: argument must be a PID or %%jid\n", argv[0]);
            return;
            }
            job = getjobjid(jobs, num);
            if ( job == NULL ) {
            printf ( "%s: No such job\n", command);
            return; }
            pid_num = job->pid;
            if (kill(-pid_num, SIGCONT) != 0) {
                perror("Failed to send SIGCONT signal");
                return;
            }
            // changing state
            job->state = FG;

	    // waiting for current foreground process to execute
            if (fgpid(jobs) != 0){
                waitfg(fgpid(jobs)); // terminating fg process
            }

            return;
        }
        // its PID
        else {
            int num = atoi(pid_or_jid);  // Convert string to integer
            // if atoi returns 0, its not number
            if (num == 0){   
            printf("%s: argument must be a PID or %%jid\n", argv[0]);
            return; }

            
            job = getjobpid(jobs, num);
             if ( job == NULL ) {
            printf ( "(%d): No such process\n", num );
            return;
            }
            pid_num = job->pid;
            if (kill(-pid_num, SIGCONT) != 0) {
                perror("Failed to send SIGCONT signal");
                return;
            }
            // changing state
            job->state = FG;

	    // waiting for current foreground process to execute
            if (fgpid(jobs) != 0){
                waitfg(fgpid(jobs)); // terminating fg process
            }
        }
    }
    return; // end program
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
    pid_t foreground_process = fgpid(jobs);
    sigset_t mask;
    sigemptyset(&mask);
    // checking if pid given is that of fgpid
    // checking if pid is foreground
    // if foreground process is 0, then means no foreground process
    if (foreground_process == pid && foreground_process != 0){
            while (fgpid(jobs) == pid){
                sigsuspend(&mask);
            }
        }
    return;
}


/*****************
 * Signal handlers
 *****************/
/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {
    int status;
     // Iterate through job list
    int result = waitpid(-1, &status, WNOHANG | WUNTRACED);
    while (result > 0) {
        // returns -1 if no child has the signal
 
        // The macro WCOREDUMP(status) checks if a core dump was produced,
        // indicating that the signal was uncaught.
        if (WCOREDUMP(status)) {
            printf("Signal was uncaught. Job's pid is %d. The uncaught signals number is %d.\n", result,  WTERMSIG(status));
            // Now delete job from list since terminated
            deletejob(jobs, result);
        } 
        else if(WIFEXITED(status)){
            deletejob(jobs, result);
        }
        
        else if(WIFSTOPPED(status)) {
            getjobpid(jobs, result) -> state =ST; 
            printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(result), (int) result, WSTOPSIG(status));
        }
         // Check if the process is a zombie
        else if ( WIFSIGNALED(status)) {
            // printf("Process with PID %d is a zombie\n", jobs[i].pid);
            // You can take further action here if needed
            printf("Job [%d] (%d) terminated by signal %d\n",  pid2jid(result), (int) result, WTERMSIG(status));
            deletejob(jobs, result);
        }
        result = waitpid(-1, &status, WNOHANG | WUNTRACED);
    }
}


/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
    // BAD SPAZZES OUT pid_t process = fgpid(jobs);
    pid_t process = fgpid(jobs);
    if (process != 0){
        if (kill(process, SIGINT) == -1) {
            perror("kill");
        }
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
   pid_t process = fgpid(jobs);
   if (process != 0){
       if (kill(process, SIGTSTP) == -1) {
           perror("kill");
       }
   }
    return;
}

/*
 * sigusr1_handler - child is ready
 */
void sigusr1_handler(int sig) {
    ready = 1;
}

/*
* set_not_ignored - set the signals back to not be ignored
*/
void set_not_ignored() {
	signal(SIGINT, SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	// add any others if needed?
}

/*
* set_ignored - set the signals to be ignored  (used for ignoring stopping background proccess # Hint 3)
*/
void set_ignored() {
	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	// add any others if needed?
	// should mirror set_not_ignored so that the child can not
}



/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* freejid - Returns smallest free job ID */
int freejid(struct job_t *jobs) {
    int i;
    int taken[MAXJOBS + 1] = {0};
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid != 0) 
        taken[jobs[i].jid] = 1;
    for (i = 1; i <= MAXJOBS; i++)
        if (!taken[i])
            return i;
    return 0;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;
    
    if (pid < 1)
        return 0;
    int free = freejid(jobs);
    if (!free) {
        printf("Tried to create too many jobs\n");
        return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = free;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    return 0; /*suppress compiler warning*/
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
    }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG: 
                    printf("Running ");
                    break;
                case FG: 
                    printf("Foreground ");
                    break;
                case ST: 
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", 
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message and terminate
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *   child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}


char** readFromFile(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Failed to open file.\n");
        exit(1);
    }

    int capacity = 10;  // Initial capacity for the array of strings
    int size = 0;       // Number of strings read so far
    char** strings = malloc(capacity * sizeof(char*));
    if (strings == NULL) {
        printf("Memory allocation failed.\n");
        exit(1);
    }

    char currentChar;
    int currentIndex = 0;
    strings[size] = malloc(MAXLINE * sizeof(char));
    while ((currentChar = fgetc(file)) != EOF) {
        if (currentChar != ' ') {
            strings[size][currentIndex] = currentChar;
            currentIndex++;
            if (currentIndex >= MAXLINE) {
                // Resize the current string if it exceeds the maximum length
                strings[size] = realloc(strings[size], currentIndex * 2 * sizeof(char));
                if (strings[size] == NULL) {
                    printf("Memory reallocation failed.\n");
                    exit(1);
                }
            }
        } else {
            strings[size][currentIndex] = '\0'; // Null terminate the string
            size++;
            currentIndex = 0;
            if (size >= capacity) {
                // Resize the array if it exceeds the current capacity
                capacity *= 2;
                strings = realloc(strings, capacity * sizeof(char*));
                if (strings == NULL) {
                    printf("Memory reallocation failed.\n");
                    exit(1);
                }
            }
            strings[size] = malloc(MAXLINE * sizeof(char));
        }
    }
    strings[size][currentIndex] = '\0'; // Null terminate the last string

    fclose(file);
    return strings;
}
