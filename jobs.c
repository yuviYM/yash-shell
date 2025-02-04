#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "jobs.h" 

job_t jobs[MAX_JOBS];

pid_t shell_pgid = 0;   // set in main

// return the first free slot or -1 if none
static int find_job_slot(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].used) {
            return i;
        }
    }
    return -1; // no free jobs
}

// return next job id, ascending order so job numbers assigned sequentially
static int get_next_jobID(void) {
    int max_id = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && jobs[i].job_id > max_id) {
            max_id = jobs[i].job_id;
        }
    }
    return max_id + 1;
}

void jobs_init(void) {
    memset(jobs, 0, sizeof(jobs));  // clear all job entries
}

int add_job(pid_t pgid, const char *cmdline, job_state_t state) {
    int index = find_job_slot();
    if (index < 0) {
        return -1; // job table is full
    }
    jobs[index].used = 1;

    jobs[index].pgid = pgid;
    // printf("Job added with PGID: %d", pgid);

    jobs[index].state = state;
    jobs[index].job_id = get_next_jobID();
    jobs[index].is_bg = 0; // default to fg

    // save original command line safely
    strncpy(jobs[index].cmdline, cmdline, sizeof(jobs[index].cmdline) - 1); 
    jobs[index].cmdline[sizeof(jobs[index].cmdline) - 1] = '\0';            
    return index;
}

void remove_job(int index) {
    // job finished
    jobs[index].used = 0;
    jobs[index].pgid = 0;
    jobs[index].cmdline[0] = '\0';
    jobs[index].state = DONE;
    jobs[index].job_id = 0;
}

int find_job_PGID(pid_t pgid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && jobs[i].pgid == pgid) {
            return i;
        }
    }
    return -1;
    
}



int find_job_ID(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && jobs[i].job_id == job_id) {
            return i;
        }
    }
    return -1;
}

int most_recent_job(void) {
    // for finding the most recently created job for '+' denotion
    int max_index = -1;
    int max_id = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && jobs[i].state != DONE && jobs[i].job_id > max_id) {
            max_id = jobs[i].job_id;
            max_index = i;
        }
    }
    return max_index;
}

// void run_jobs(void) {
//     // user runs 'jobs'
//     int index_recent = most_recent_job();
//     int recent_id = -1;
//     if (index_recent >= 0) {
//         recent_id = jobs[index_recent].job_id;
//     }

//     for (int i = 0; i < MAX_JOBS; i++) {
//         if (!jobs[i].used) continue;    // skip free jobs

//         char marker = (jobs[i].job_id == recent_id) ? '+' : '-';    // most recent job -> '+'

//         if (jobs[i].state == DONE) {
//             // when finished, print the firs time
//             printf("[%d]%c  Done       %s", jobs[i].job_id, marker, jobs[i].cmdline);

//             // next time 'jobs' is used, it is gone from the table
//             remove_job(i); 
            
//         }
//         else if (jobs[i].state == RUNNING) {
//             printf("[%d]%c  Running    %s", jobs[i].job_id, marker, jobs[i].cmdline);
//         }
//         else if (jobs[i].state == STOPPED) {
//             printf("[%d]%c  Stopped    %s", jobs[i].job_id, marker, jobs[i].cmdline);
//         }

        
//     }
// }

void run_jobs(void) {
    // get used jobs into a list
    int used_list[MAX_JOBS];
    int used_count = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used) {
            used_list[used_count++] = i;
        }
    }

    // sort used jobs by ascending ID
    for (int i = 0; i < used_count - 1; i++) {
        for (int j = i + 1; j < used_count; j++) {
            int idx_i = used_list[i];
            int idx_j = used_list[j];
            if (jobs[idx_i].job_id > jobs[idx_j].job_id) {
                int temp = used_list[i];
                used_list[i] = used_list[j];
                used_list[j] = temp;
            }
        }
    }

    // find most recent jobs (with highest ID)
    int idx_recent = most_recent_job();
    int recent_id  = (idx_recent >= 0) ? jobs[idx_recent].job_id : -1;

    // print in ascending ID order
    for (int x = 0; x < used_count; x++) {
        int i = used_list[x];

        char marker = (jobs[i].job_id == recent_id) ? '+' : '-';

        switch (jobs[i].state) {
            case DONE:
                // print once on the next jobs call
                if (jobs[i].is_bg) {
                    // show '&' for background
                    printf("[%d]%c  Done       %s&\n", jobs[i].job_id, marker, jobs[i].cmdline);
                } else {
                    // foreground finished
                    printf("[%d]%c  Done       %s\n", jobs[i].job_id, marker, jobs[i].cmdline);
                }

                // remove for the next jobs call
                remove_job(i);
                break;

            case RUNNING:
                if (jobs[i].is_bg) {
                    // show '&' for background
                    printf("[%d]%c  Running    %s&\n", jobs[i].job_id, marker, jobs[i].cmdline);
                } else {
                    // foreground
                    printf("[%d]%c  Running    %s\n", jobs[i].job_id, marker, jobs[i].cmdline);
                }
                break;

            case STOPPED:
                printf("[%d]%c  Stopped    %s\n", jobs[i].job_id, marker, jobs[i].cmdline);
                break;
        }
    }
}

void run_fg(int job_id) {
    // user runs 'fg'
    // bring job into foreground / resume stopped/bg process
    // wait for completion and handle process state updates

    // if job ID is specified, use that, if not resume most recent
    int idx;
    if (job_id > 0) {
        idx = find_job_ID(job_id);
    } else {
        idx = most_recent_job();
    }
    if (idx < 0) {
        fprintf(stderr, "fg: no current job\n");
        return;
    }

    // jobs command line
    printf("%s\n", jobs[idx].cmdline);
    fflush(stdout);

    // update state and bg
    jobs[idx].is_bg = 0;
    jobs[idx].state = RUNNING;

    // terminal control to the fg job
    tcsetpgrp(STDIN_FILENO, jobs[idx].pgid);

    // resume all processes in jobs process group
    kill(-jobs[idx].pgid, SIGCONT);

    // blocking wait, waitpid on the PGID
    int status;
    pid_t wpid;
    do {
        wpid = waitpid(-jobs[idx].pgid, &status, WUNTRACED);    // negative for pgid
        if (wpid == -1 && errno == ECHILD) {
            break;
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status) && !WIFSTOPPED(status));

    tcsetpgrp(STDIN_FILENO, shell_pgid);

    // if exited or killed, remove from job table
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        remove_job(idx);
    } else if (WIFSTOPPED(status)) {    // if stopped update state
        jobs[idx].state = STOPPED;
    }
}

// void run_bg(int job_id) {
//     // resumes stopped job in bg, does not block the shell

//     int idx;
//     if (job_id > 0) {
//         idx = find_job_ID(job_id);
//     } else {
//         idx = most_recent_job();
//     }
//     if (idx < 0) {
//         fprintf(stderr, "bg: no current job\n");
//         return;
//     }

//     // remove newline for printing
//     char *cmd = jobs[idx].cmdline;
//     size_t len = strlen(cmd);
//     if (len > 0 && cmd[len - 1] == '\n') {
//         cmd[len - 1] = '\0'; 
//     }

//     // print job
//     printf("[%d] %s& \n", jobs[idx].job_id, jobs[idx].cmdline); 

//     jobs[idx].state = RUNNING;
//     kill(-jobs[idx].pgid, SIGCONT);
// }

void run_bg(int job_id) {
    // Find specified job (or most recent)
    int idx = (job_id > 0) ? find_job_ID(job_id) : most_recent_job();
    if (idx < 0) {
        fprintf(stderr, "bg: no current job\n");
        return;
    }

    // Only resume if the job is STOPPED
    if (jobs[idx].state == STOPPED) {
        // remove newline from the job’s command line
        char *cmd = jobs[idx].cmdline;
        size_t len = strlen(cmd);
        if (len > 0 && cmd[len - 1] == '\n') {
            cmd[len - 1] = '\0';
        }
        printf("[%d]+ %s &\n", jobs[idx].job_id, cmd);

        // mark as running and bg
        jobs[idx].is_bg = 1;
        jobs[idx].state = RUNNING;

        // send continue
        kill(-jobs[idx].pgid, SIGCONT);
    } else {
        // job done or dne
        fprintf(stderr, "bg: no current job\n");
    }
}



void sigchld_handler(int sig) {
    // handle child process state changes

    int status;
    pid_t child_pid;

    // get all children with changed state
    while ((child_pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {   
        // printf("child process %d changed state w status: %d\n", child_pid, status);

        int idx = find_job_PGID(child_pid);        // find corresponding child (getpgid(child_PID))
        if (idx < 0) {
            continue;
        }

        if (WIFSTOPPED(status)) {
            // printf("job %d (pgid: %d) stopped\n", idx, jobs[idx].pgid);
            jobs[idx].state = STOPPED;
        } else if (WIFEXITED(status)) {
            // printf("job %d (pgid: %d) exited normally\n", idx, jobs[idx].pgid);
            jobs[idx].state = DONE;
        } else if (WIFSIGNALED(status)) {
            // printf("job %d (pgid: %d) killed by signal\n", idx, jobs[idx].pgid);
            jobs[idx].state = DONE;
        }
    }
}


void sigtstp_handler(int sig) {
    // stop foreground process with ctrl z is pressed

    (void)sig;
    
    pid_t fg_pgid = tcgetpgrp(STDIN_FILENO);    // get foreground process group
    if (fg_pgid != shell_pgid) {
        kill(-fg_pgid, SIGTSTP);    // stop foreground process, not entire shell
    }
}
