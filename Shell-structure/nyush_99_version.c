#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/types.h>

#include <libgen.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_INPUT_LENGTH 1024       // max input length
#define MAX_PATH_LENGTH 1024        // max path length
#define MAX_ARGS 64                 // max args number
#define MAX_JOBS 100                // max jobs number


// construct the job
typedef struct job{
    int index;
    char command[MAX_INPUT_LENGTH];
    pid_t pid;
} job;

job jobs[MAX_JOBS];                 // global job list
int job_count = 0;                  // count of current jobs in job list


// Signal handler implementation: ignore signals like SIGINT, SIGQUIT, and SIGTSTP
void signal_handler(int signal) {
    (void)signal;
}


// Prompt printer implementation: print the prompt
void print_prompt(char *username) {
    char cwd[MAX_PATH_LENGTH];      // PATH array

    // get the current directory
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
        exit(EXIT_FAILURE);
    }

    // take the basename of the current working directory
    char *current_dir = (strcmp(cwd, "/") == 0) ? cwd : basename(cwd);

    printf("[%s %s]$ ", username, current_dir);
    fflush(stdout);
}


// Input reader implementation: read user input (command)
char *read_input(void) {
    char *input = NULL;             // user input
    size_t buf_size = 0;            // allocated buffer by getline()
    ssize_t n_char_read;            // number of char read from input

    n_char_read = getline(&input, &buf_size, stdin);

    // handle EOF
    if (n_char_read == -1) {
        if (feof(stdin)) {
            exit(EXIT_SUCCESS); 
        } else {
            exit(EXIT_FAILURE);
        }
    }

    // check if the input is just one or more whitespaces or a blank line
    if (input[0] == '\0' || strspn(input, " \t\r\n") == strlen(input)) {
        free(input);
        return NULL;
    }

    // remove the newline character from the input
    if (n_char_read > 0 && input[n_char_read -1] == '\n') {
        input[n_char_read - 1] = '\0';
    }

    return input;
}


// Input separator implementation: separate the user input with " " and store them as separated_commands
char **separate_input(char *input) {
    if (input == NULL) {                                                            // check if input is NULL
        return NULL;
    }

    // allocate for separated_commands
    char **separated_commands = malloc(MAX_ARGS * sizeof(char *));

    // separate the user input by " "
    int n_commands = 0;
    char *token = strtok(input, " ");
    while (token != NULL) {
        // check number of speparated commands
        if (n_commands >= MAX_ARGS - 1) {
            fprintf(stderr, "Too many arguments\n");                                // check for debug the number of arguments in command
        }

        // assign the token to separated_commands
        separated_commands[n_commands++] = token;
        token = strtok(NULL, " ");
    }

    // set the last to NULL
    separated_commands[n_commands] = NULL;

    return separated_commands;
}


// Build-in command 'cd <dir>' implementation
int buildin_cd(char **separated_commands) {
    if (separated_commands[1] == NULL || separated_commands[2] != NULL) {           // check if cd with 0 or 2+ args
        fprintf(stderr, "Error: invalid command\n");
    } else if (chdir(separated_commands[1]) != 0) {                                 // check if the dir exist
        fprintf(stderr, "Error: invalid directory\n");
    }

    return 1;
}


// Build-in command 'exit' implementation
int buildin_exit(char **separated_commands) {
    if (separated_commands[1] != NULL) {                                            // check if exit with more than 1 args
        fprintf(stderr, "Error: invalid command\n");
        return 1;
    }
    if (job_count > 0) {                                                            // check if job list is empty
        fprintf(stderr, "Error: there are suspended jobs\n");
        return 1;
    }

    exit(EXIT_SUCCESS);
}


// Jobs helper function that add the job to the job list by pid and the entire command
void add_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {                                                     // check if jobs list contains more than the max number of jobs
        // assign pid, command and jobs list index
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].command, command, MAX_INPUT_LENGTH);
        jobs[job_count].index = job_count + 1;
        job_count++;
    } else {
        fprintf(stderr, "Error: too many jobs\n");
        exit(EXIT_FAILURE);
    }
}


// Jobs helper function that remove the job from the job list by pid
void remove_job(pid_t pid) {
    int found = 0;

    // find the job in job list by searching pid
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].pid == pid) {
            found = 1;
        }
        // remove the found job from job list
        if (found == 1 && i < job_count - 1) {
            jobs[i] = jobs[i + 1];
        }
    }

    // update job_count if the job is found in the job list
    if (found == 1) {
        job_count--;
    }
}


// Jobs helper function that print the jobs
void print_jobs() {
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %s\n", i + 1, jobs[i].command);
    }
}


// Build-in command `jobs` implementation
int buildin_jobs(char **separated_commands) {
    if (separated_commands[1] != NULL) {                                            // check if jobs with args
        fprintf(stderr, "Error: invalid command\n");
    } else {
        print_jobs();
    }
    return 1;
}


// Build-in command `fg <index>` implementation
int buildin_fg(char **separated_commands) {
    if (separated_commands[1] == NULL || separated_commands[2] != NULL) {           // check if fg with 0 or 2+ args
        fprintf(stderr, "Error: invalid command\n");
        return 1;
    }

    // set up the index by `<index>` in user command
    int index = atoi(separated_commands[1]);

    if (index < 1 || index > job_count) {                                           // check if the job index exist
        fprintf(stderr, "Error: invalid job\n");
        return 1;
    }

    int job_index = index - 1;
    pid_t pid = jobs[job_index].pid;

    // save a copy of the command for stopped again use
    char *command = strdup(jobs[job_index].command);

    // resume the job by pid
    kill(pid, SIGCONT);

    // wait for the job to finish or stop
    int status;
    waitpid(pid, &status, WUNTRACED);

    if (!WIFSTOPPED(status)) {
        // remove the job from the list if it's not stopped
        remove_job(pid);
    } else {
        // remove the job first and add it to the end of job list for the job that being stopped again
        remove_job(pid);
        add_job(pid, command);
    }

    free(command);
    return 1;
}


// Build-in implementation: check if it is a build-in command, execute it with specified function
int execute_buildin(char **separated_commands) {
    if (strcmp(separated_commands[0], "cd") == 0) {
        return buildin_cd(separated_commands);
    } else if (strcmp(separated_commands[0], "exit") == 0) {
        return buildin_exit(separated_commands);
    } else if (strcmp(separated_commands[0], "jobs") == 0) {
        return buildin_jobs(separated_commands);
    } else if (strcmp(separated_commands[0], "fg") == 0) {
        return buildin_fg(separated_commands);
    }

    return 0;                                                                       // return 0 if it's not a build-in command
}


// Redirection helper function that handle file descriptor for input redirection
int input_redirection(char *input_file, int *fd_in) {
    *fd_in = open(input_file, O_RDONLY);

    if (*fd_in == -1) {                                                             // check if file descriptor works correctly
        return -1;
    }

    return 1;
}


// Redirection helper function that handle file descriptor for output redirection
int output_redirection(char *output_file, int *fd_out, int append) {
    // set the flags for `>`, and the modes for both of `>` and `>>`
    int flags_single_out = O_APPEND | O_WRONLY | O_CREAT;
    int modes = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    if (append == 1) {                                                              // check if it's `>>`
        *fd_out = open(output_file, flags_single_out, modes);
    } else {                                                                        // or `>`
        *fd_out = creat(output_file, modes);
    }

    if (*fd_out == -1) {                                                            // check if file descriptor works correctly
        fprintf(stderr, "Error: invalid command\n");
        return -1;
    }

    return 1;
}


// Redirection implementation: check if there is a redirection and return which redirection with status
int check_redirection(char **separated_commands, int *fd_in, int *fd_out, int *in_redirect_status, int *out_redirect_status) {
    for (int i = 0; separated_commands[i] != NULL; i++) {
        if (strcmp(separated_commands[i], "<") == 0) {                              // check for `<`
            if (separated_commands[i+1] == NULL) {                                  // check if the file arg is NULL
                fprintf(stderr, "Error: invalid command\n");
                return -1;
            } else if (access(separated_commands[i+1], F_OK) != 0) {                // check if the file arg doesn't exist in current directory
                fprintf(stderr, "Error: invalid file\n");
                return -1;
            }

            // update the in redirection status and call the helper function
            *in_redirect_status = input_redirection(separated_commands[i+1], fd_in);

            // remove the `<` and relative file name in the separated_commands
            for (int j = i; separated_commands[j] != NULL; j++) {
                separated_commands[j] = separated_commands[j + 2];
            }
            i--;

        } else if (strcmp(separated_commands[i], ">") == 0 || strcmp(separated_commands[i], ">>") == 0) { // check for `>` and `>>`
            if (separated_commands[i+1] == NULL) {                                  // check if the file arg is NULL
                fprintf(stderr, "Error: invalid command\n");
                return -1;
            }

            int append = strcmp(separated_commands[i], ">>") == 0;                  // check if its an `>>`

            // update the out redirection status and call the helper function
            *out_redirect_status = output_redirection(separated_commands[i+1], fd_out, append);

            // remove the `>` or `>>` and relative file name in the separated_commands
            for (int j = i; separated_commands[j] != NULL; j++) {
                separated_commands[j] = separated_commands[j + 2];
            }
            i--;
        }
    }

    return 0;
}


// Pipe helper function that segment the separated_commands by `|`
char ***segment_command(char **separated_commands, int *num_segment) {
    int seg_count = 1;

    // count for number of segments
    for (int i = 0; separated_commands[i] != NULL; i++) {
        if (strcmp(separated_commands[i], "|") == 0) {
            if (i == 0 || separated_commands[i + 1] == NULL) {                      // check if pipe is the first command or there is no arg after pipe
                fprintf(stderr, "Error: invalid command\n");
                return NULL;
            }
            seg_count++;
        }
    }

    // allocate for segments and initilize the segments with segments_index
    char ***segments = malloc(sizeof(char **) * (seg_count + 1));
    int segments_index = 0;
    segments[segments_index] = separated_commands;

    // segment the separated_commands by pipe
    for (int i = 0; separated_commands[i] != NULL; i++) {
        if (strcmp(separated_commands[i], "|") == 0) {
            separated_commands[i] = NULL;
            segments[++segments_index] = &separated_commands[i + 1];
        }
    }

    *num_segment = seg_count;
    return segments;
}


// Commands execution implementation
int execute_command(char **separated_commands, char *input_copy) {
    int fd_in = -1, fd_out = -1, in_redirect_status = 0, out_redirect_status = 0;
    int num_segments;

    // check if build-in command (1 for yes, 0 for no)
    if (execute_buildin(separated_commands) == 1) {
        return 1;
    }

    // check if redirection works correctly (0 or -1 for yes, -1 for no and print the relative `stderr` and out of the command execution)
    if (check_redirection(separated_commands, &fd_in, &fd_out, &in_redirect_status, &out_redirect_status) < 0) {
        return -1;
    }

    // initialize segments for pipe
    char ***segments = segment_command(separated_commands, &num_segments);
    if (segments == NULL) return -1;                                                    // check for segment_command works correctly

    // initialize the file descriptors for multiple pipes
    int fds[num_segments - 1][2];
    pid_t pids[num_segments];

    // create pipes
    for (int i = 0; i < num_segments - 1; i++) {
        if (pipe(fds[i]) < 0) {
            exit(EXIT_FAILURE);
        }
    }

    // for loop for handling I/O for multiple pipes
    for (int i = 0; i < num_segments; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            // fork failed (this shouldn't happen)
            perror("fork failed");
            exit(EXIT_FAILURE);
        } else if (pids[i] == 0) {
            // child (new process)
            // signal handle that makes sure SIGINT, SIGQUIT, SIGTSTP are not ignored in child
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // close unrelavent file descriptors
            for (int j = 0; j < num_segments - 1; j++) {
                if (j != i - 1) close(fds[j][0]);
                if (j != i) close(fds[j][1]);
            }

            // handle STDIN_FILENO
            if (i == 0) {
                if (in_redirect_status == 1) {                                          // check for `<` in first program
                    dup2(fd_in, STDIN_FILENO);
                    close(fd_in);
                }
            } else {
                dup2(fds[i-1][0], STDIN_FILENO);
                close(fds[i-1][1]);
                close(fds[i-1][0]);
            }

            // handle STDOUT_FILENO
            if (i == num_segments - 1) {
                if (out_redirect_status == 1) {                                         // check for `>` or `>>` in last program
                    dup2(fd_out, STDOUT_FILENO);
                    close(fd_out);
                }
            } else {
                dup2(fds[i][1], STDOUT_FILENO);
                close(fds[i][0]);
                close(fds[i][1]);
            }

            // exec
            execvp(segments[i][0], segments[i]);
            fprintf(stderr, "Error: invalid program\n");                                // print stderr if execvp failed
            exit(EXIT_FAILURE);
        }
    }

    // parent

    // close all file descriptors
    for (int i = 0; i < num_segments - 1; i++) {
        close(fds[i][0]);
        close(fds[i][1]);
    }

    // wait for all child process and add job with its full command to the job list if the job is stopped
    for (int i = 0; i < num_segments; i++) {
        int status;
        waitpid(pids[i], &status, WUNTRACED); 
        if (WIFSTOPPED(status)) {
            add_job(pids[i], input_copy);
        }
    }
    free(segments);

    return 0;
}


// Main implementation
int main() {
    char *input;                    // user input
    char **separated_commands;      // separated commands
    char *username = "nyush";       // shell name (can be modified for future use)

    // ignore SIGINT, SIGQUIT, and SIGTSTP using signal_handler
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGTSTP, signal_handler);

    // a loop that repeatedly prints the prompt and gets the user input
    while (1) {
        print_prompt(username);

        input = read_input();
        // skip the rest of loop if the input is consisted of whitespace, or a blank line
        if (input == NULL) {
            continue;
        }

        // copy the full input command for jobs use
        char *input_copy = strdup(input);

        separated_commands = separate_input(input);

        execute_command(separated_commands, input_copy);

        // free to prevent memory leaks
        free(input_copy);
        free(separated_commands);
        free(input);
    }

    return 0;
}


/*
The references markdown is also submitted along with the nyush.c and Makefile
*/

