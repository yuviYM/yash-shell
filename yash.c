#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include "jobs.h"

#define MAX_INPUT 2000
#define MAX_ARGS  100

void parse_left_redirection(char **args, char **in_file, char **out_file, char **err_file, int *pipe_index);

void parse_right_redirection(char **args, int pipe_index, char **right_in, char **right_out, char **right_err);

void setup_redirections(const char *in_file, const char *out_file, const char *err_file);

void run_command(char **args, const char *in_file, const char *out_file, const char *err_file, int background, const char *original_cmdline);

void run_pipe( char **args, int pipe_index,
    const char *left_in, const char *left_out, const char *left_err,
    const char *right_in, const char *right_out, const char *right_err
);

int parse_input(char *input, char **args, int *arg_count);

static int check_background(char **args, char *original_cmdline);

int main() {    

    jobs_init();

    // ignore SIGTTOU and SIGTTIN so not suspended for calling tcsetpgrp
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid); // terminal control to the shell

    signal(SIGCHLD, sigchld_handler);
    signal(SIGTSTP, sigtstp_handler);

    // ignore SIGINT in the shell so ctrl c won't kill yash itself
    signal(SIGINT, SIG_IGN); 

    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    int arg_count;

    char og_cmdline[MAX_INPUT];

    while (1) {
        // prompt displayed immediately 
        printf("# ");
        fflush(stdout);

        // read input
        if (fgets(input, MAX_INPUT, stdin) == NULL) {
            // on ctrl d EOF
            printf("\n");
            break;
        }

        // store original input for job control
        strcpy(og_cmdline, input);

        parse_input(input, args, &arg_count);

        // continue if empty line
        if (arg_count == 0) {
            continue;
        }

        // check for job commands
        if (strcmp(args[0], "jobs") == 0) {
            run_jobs();
            continue; 
        }
        if (strcmp(args[0], "fg") == 0) {
            // parse if user typed fg with a number
            int job_id = 0;
            if (args[1]) {
                job_id = atoi(args[1]);  // convert to integer
            }
            run_fg(job_id);
            continue;
        }
        if (strcmp(args[0], "bg") == 0) {
            int job_id = 0;
            if (args[1]) {
                job_id = atoi(args[1]);
            }
            run_bg(job_id);
            continue;
        }
        

        // parse for redirection and check if pipe
        char *in_file, *out_file, *err_file;
        int pipe_index;
        parse_left_redirection(args, &in_file, &out_file, &err_file, &pipe_index);

        // if found a pipe
        if (pipe_index != -1) {
            // check for right side of pipe redirections
            char *right_in, *right_out, *right_err;
            parse_right_redirection(args, pipe_index, &right_in, &right_out, &right_err);

            run_pipe(args, pipe_index,
                in_file, out_file, err_file,      // left side
                right_in, right_out, right_err    // right side
            );
        } else {
            // single command
            int is_background = check_background(args, og_cmdline); // '&' + pipe not supported
            run_command(args, in_file, out_file, err_file, is_background, og_cmdline);
        }
    }

    return 0;
}

int parse_input(char *input, char **args, int *arg_count) {
    *arg_count = 0;

    // remove trailing newline
    size_t len = strlen(input);
    if (len > 0 && input[len-1] == '\n') {
        input[len-1] = '\0';
    }

    char *saveptr;
    char *token = strtok_r(input, " \t", &saveptr);

    while (token && *arg_count < MAX_ARGS - 1) {
        args[(*arg_count)++] = token;
        token = strtok_r(NULL, " \t", &saveptr);    // delimit spaces + tabs
    }
    args[*arg_count] = NULL;    // null terminate for execvp
    return *arg_count;
}


void parse_left_redirection(char **args, char **in_file, char **out_file, char **err_file, int *pipe_index) {
    // "< file" , file replaces STDIN
    // "> file" , file replaces STDOUT
    //  "2> file" , file replaced STDERR
    // only actual command arguments remain in args[], redirection details stored separately
  
    *in_file = NULL;
    *out_file = NULL;
    *err_file = NULL;
    *pipe_index = -1;   

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            *pipe_index = i;    // store location of pipe if found
            break;
        }
        else if (strcmp(args[i], "<") == 0) {
            if (args[i+1]) {
                *in_file = args[i+1];      // store input file
                args[i] = NULL;            // remove symbol
                args[i+1] = NULL;          // remove filename
                i++;                       // skip filename in next iteration
            }
        }
        else if (strcmp(args[i], ">") == 0) {
            if (args[i+1]) {
                *out_file = args[i+1];
                args[i] = NULL;
                args[i+1] = NULL;
                i++;
            }
        }
        else if (strcmp(args[i], "2>") == 0) {
            if (args[i+1]) {
                *err_file = args[i+1];
                args[i] = NULL;
                args[i+1] = NULL;
                i++;
            }
        }
    }

    // compact args to remove all the null values left by removing redirection symbols 
    // keep the last null
    int j = 0;
    for (int i = 0; i < MAX_ARGS && args[i] != NULL; i++) {
        args[j++] = args[i];
    }
    // fill remainder w null
    while (j < MAX_ARGS) {
        args[j++] = NULL;
    }
}

void parse_right_redirection(char **args, int pipe_index,
                             char **right_in, char **right_out, char **right_err)
{
    *right_in = NULL;
    *right_out = NULL;
    *right_err = NULL;

    // remove any <, >, 2> from the portion after the pipe
    for (int i = pipe_index + 1; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0 && args[i+1]) {
            *right_in = args[i+1];
            args[i] = NULL;
            args[i+1] = NULL;
            i++;
        }
        else if (strcmp(args[i], ">") == 0 && args[i+1]) {
            *right_out = args[i+1];
            args[i] = NULL;
            args[i+1] = NULL;
            i++;
        }
        else if (strcmp(args[i], "2>") == 0 && args[i+1]) {
            *right_err = args[i+1];
            args[i] = NULL;
            args[i+1] = NULL;
            i++;
        }
    }

    // compact ONLY the portion after pipe_index+1
    // so no overwrite the left side's tokens
    int j = pipe_index + 1; 
    for (int i = pipe_index + 1; i < MAX_ARGS && args[i] != NULL; i++) {
        args[j++] = args[i];
    }
    while (j < MAX_ARGS) {
        args[j++] = NULL;
    }
}




void setup_redirections(const char *in_file, const char *out_file, const char *err_file){
    // configure input/output/error redirection in child process before executing a command

    // checks if input redirection is applied
    if (in_file) {
        int fd_in = open(in_file, O_RDONLY);    // open in read only mode

        if (fd_in < 0) {    // unable to open
            fprintf(stderr, "Error: cannot open input file '%s'\n", in_file);
            _exit(1);   // prevent running incomplete command
        }
        dup2(fd_in, STDIN_FILENO);  // redirect STDIN to input file
        close(fd_in);
    }

    // checks if output redirection is applied
    if (out_file) {
        int fd_out = open(out_file, O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH); 
        if (fd_out < 0) {
            fprintf(stderr, "Error: cannot open output file '%s'\n", out_file);
            _exit(1);  
        }
        dup2(fd_out, STDOUT_FILENO); 
        close(fd_out);
    }
    
    // checks if error redirection is applied
    if (err_file) {
        int fd_err = open(err_file, O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd_err < 0) {
            fprintf(stderr, "Error: cannot open error file '%s'\n", err_file);
            _exit(1);
        }
        dup2(fd_err, STDERR_FILENO);
        close(fd_err);
    }
}

void run_command(char **args, const char *in_file, const char *out_file, const char *err_file, int background, const char *original_cmdline){
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }
    else if (pid == 0) {
        // child

        setpgid(0,0);

        signal(SIGINT, SIG_DFL); // let child be interrupted
        signal(SIGTSTP, SIG_DFL);  // allow ctrl z

        setup_redirections(in_file, out_file, err_file);

        execvp(args[0], args);
        // fprintf(stderr, "Command not found: %s\n", args[0]);    // execvp returns if unsuccessful 
        _exit(127); // bash error code 127
    }
    else {
        // parent

        // set child's pgid
        setpgid(pid, pid);

        // add job to the table as running
        int idx = add_job(pid, original_cmdline, RUNNING);

        if(!background){
            // in foreground

            // give child terminal control
            tcsetpgrp(STDIN_FILENO, pid);

            // wait for child to finsih/stop
            int status;
            waitpid(pid, &status, WUNTRACED); // blocking wait

            // after finishes/stops, restore terminal to shell
            tcsetpgrp(STDIN_FILENO, shell_pgid);

            // if child is stopped, store the job as stopped
            if (WIFSTOPPED(status)) {
                jobs[idx].state = STOPPED;
            } else {
                // exited or killed
                remove_job(idx);
            }
        } else {
            // background job
            jobs[idx].is_bg = 1;
        }

        
    }
}

void run_pipe(char **args, int pipe_index,
    const char *left_in, const char *left_out, const char *left_err,
    const char *right_in, const char *right_out, const char *right_err
) {
    // build left_args
    char *left_args[MAX_ARGS];
    memset(left_args, 0, sizeof(left_args));    // init to 0

    int i;
    for (i = 0; i < pipe_index; i++) {
        left_args[i] = args[i];
    }
    left_args[i] = NULL;    // null terminate for execvp

    // build right_args
    char *right_args[MAX_ARGS];
    memset(right_args, 0, sizeof(right_args));

    int j = 0;
    for (i = pipe_index + 1; args[i] != NULL; i++) {
        right_args[j++] = args[i];
    }
    right_args[j] = NULL;

    // create pipe
    // pipefd[0] -> read end
    // pipefd[1] -> write end
    int pipefd[2];
    if (pipe(pipefd) < 0) { // pipe failed
        perror("pipe");
        return;
    }

    pid_t pid_left = fork();
    if (pid_left < 0) {
        perror("fork");
        return;
    }
    else if (pid_left == 0) {
        // left child
        signal(SIGINT, SIG_DFL); 

        // if output redirection not specified, then connect pipe to stdout
        if (!left_out) {
            close(pipefd[0]);              // close read end
            dup2(pipefd[1], STDOUT_FILENO); // redirect stdout to pipe write end
            close(pipefd[1]);              // close write end after dup
        } else {
            // do not connect pipe to stdout
            close(pipefd[0]);
            close(pipefd[1]);
        }

        setup_redirections(left_in, left_out, left_err);

        execvp(left_args[0], left_args);
        fprintf(stderr, "Command not found: %s\n", left_args[0]);   // execvp failed
        _exit(127);
    }

    pid_t pid_right = fork();
    if (pid_right < 0) {
        perror("fork");
        return;
    }
    else if (pid_right == 0) {
        // right child
        signal(SIGINT, SIG_DFL);

        // if input redirection not specified, connect pipe to stdin
        if (!right_in) {
            close(pipefd[1]);   // close write end
            dup2(pipefd[0], STDIN_FILENO);  // redirect stdin to pipe write end
            close(pipefd[0]);   // close read end after dup
        } else {
            // do not connect pipe to stdin
            close(pipefd[0]);
            close(pipefd[1]);
        }

        setup_redirections(right_in, right_out, right_err);

        execvp(right_args[0], right_args);
        fprintf(stderr, "Command not found: %s\n", right_args[0]);  // execvp failed
        _exit(127);
    }

    // parent
    close(pipefd[0]);
    close(pipefd[1]);

    // blocking wait for both children
    waitpid(pid_left, NULL, 0);
    waitpid(pid_right, NULL, 0);
}


static int check_background(char **args, char *original_cmdline) {
    // check if last token is '&'
    int i = 0;
    while (args[i] != NULL) i++;
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        args[i - 1] = NULL;  // remove '&'
    } else {
        return 0; // not background
    }

    // also want to remove '&' from the end of original commandline
    // look from the end and remove
    char *amp = strrchr(original_cmdline, '&');
    if (amp) {
        *amp = '\0'; // cut off the &
        // remove trailing spaces or tabs
        int end = strlen(original_cmdline) - 1;
        while (end >= 0 && (original_cmdline[end] == ' ' || original_cmdline[end] == '\t')) {
            original_cmdline[end] = '\0';
            end--;
        }
    }

    return 1; // background
}




