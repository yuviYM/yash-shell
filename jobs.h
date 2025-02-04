#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

#define MAX_JOBS 40

// job states
typedef enum {
    RUNNING,
    STOPPED,
    DONE
} job_state_t;


typedef struct {
    int used;
    int job_id;
    pid_t pgid;
    job_state_t state;
    int is_bg; // background or foreground
    char cmdline[200]; // store command line
} job_t;

extern job_t jobs[MAX_JOBS];

void jobs_init(void);
int add_job(pid_t pgid, const char *cmdline, job_state_t state);
void remove_job(int index);
int find_job_PGID(pid_t pgid);
int find_job_ID(int job_id);
int most_recent_job(void);
void run_jobs(void);
void run_fg(int job_id);
void run_bg(int job_id);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);


// shell's PGID accessor 
extern pid_t shell_pgid;

#endif
