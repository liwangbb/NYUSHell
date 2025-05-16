#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/wait.h>

#include <libgen.h>
#include <string.h>
#include <fcntl.h>

#define MAX_INPUT_LENGTH 1024       // max input length
#define MAX_PATH_LENGTH 1024        // max path length
#define MAX_ARGS 64                 // max args number


// ignore signals like SIGINT, SIGQUIT, and SIGTSTP
void signal_handler(int signal) {
    (void)signal;
}


// Print the prompt
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


// Read user input (command)
char *read_input(void) {
    char *input = NULL;             // user input
    size_t buf_size = 0;            // allocated buffer by getline()
    ssize_t n_char_read;            // number of char read from input

    n_char_read = getline(&input, &buf_size, stdin);

    // handle EOF
    if (n_char_read == -1) {
        if (feof(stdin)) {
            fprintf(stderr, "Exiting the shell.\n");
            exit(EXIT_SUCCESS); 
        } else {
            fprintf(stderr, "getline() failed\n");
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


// Separate the user input with " " and store them as separated_commands
char **separate_input(char *input) {
    if (input == NULL) {
        return NULL;
    }

    char **separated_commands = malloc(MAX_ARGS * sizeof(char *));
    if (!separated_commands) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    // separate the user input by " "
    int n_commands = 0;
    char *token = strtok(input, " ");
    while (token != NULL) {
        // check number of speparated commands
        if (n_commands >= MAX_ARGS - 1) {
            fprintf(stderr, "Too many arguments\n");
        }

        separated_commands[n_commands++] = token;
        token = strtok(NULL, " ");
    }

    separated_commands[n_commands] = NULL;
    return separated_commands;
}


// Build-in command 'cd <dir>' implementation
int buildin_cd(char **separated_commands) {
    if (separated_commands[1] == NULL || separated_commands[2] != NULL || chdir(separated_commands[1]) != 0) {
        fprintf(stderr, "Error: invalid command\n");
    }
    return 1;
}


// Build-in command 'exit' implementation
int buildin_exit(void) {
    exit(EXIT_SUCCESS);
    return 1;
}


// Check if it is a build-in command, execute it with specified function
int execute_buildin(char **separated_commands) {
    if (strcmp(separated_commands[0], "cd") == 0) {
        return buildin_cd(separated_commands);
    } else if (strcmp(separated_commands[0], "exit") == 0) {
        return buildin_exit();
    }

    return 0; // return 0 if it's not a build-in command
}


// handle file descriptor for input redirection
int input_redirection(char *input_file, int *fd_in) {
    *fd_in = open(input_file, O_RDONLY);
    if (*fd_in == -1) {
        // fprintf(stderr, "Error: invalid file\n");
        return -1;
    }
    return 1;
}


// handle file descriptor for output redirection
int output_redirection(char *output_file, int *fd_out, int append) {
    int flags_single_out = O_APPEND | O_WRONLY | O_CREAT;
    int modes = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if (append == 1) {
        *fd_out = open(output_file, flags_single_out, modes);
    } else {
        *fd_out = creat(output_file, modes);
    }

    if (*fd_out == -1) {
        fprintf(stderr, "Error: invalid command\n");
        return -1;
    }
    return 1;
}


// Check if there is a redirection and return which redirection with status
int check_redirection_pipe(char **separated_commands, int *fd_in, int *fd_out, int *in_redirect_status, int *out_redirect_status, int *pipe_pos) {
    for (int i = 0; separated_commands[i] != NULL; i++) {
        if (strcmp(separated_commands[i], "<") == 0) {
            // check if the file is NULL
            if (separated_commands[i+1] == NULL) {
                fprintf(stderr, "Error: invalid command\n");
                return -1;
            // check if the file doesn't exist in current directory
            } else if (access(separated_commands[i+1], F_OK) != 0) {
                fprintf(stderr, "Error: invalid file\n");
                return -1;
            }
            *in_redirect_status = input_redirection(separated_commands[i+1], fd_in);
            separated_commands[i] = NULL;
        } else if (strcmp(separated_commands[i], ">") == 0 || strcmp(separated_commands[i], ">>") == 0) {
            if (separated_commands[i+1] == NULL) {
                fprintf(stderr, "Error: invalid command\n");
                return -1;
            }
            int append = strcmp(separated_commands[i], ">>") == 0;
            *out_redirect_status = output_redirection(separated_commands[i+1], fd_out, append);
            separated_commands[i] = NULL;
        } else if (strcmp(separated_commands[i], "|") == 0) {
            if (i == 0 || separated_commands[i+1] == NULL) {
                fprintf(stderr, "Error: invalid command\n");
                return -1;
            }
            separated_commands[i] = NULL;
            *pipe_pos = i;
        }
    }

    return 0;
}


int apply_redirection(int *fd_in, int *fd_out, int in_redirect_status, int out_redirect_status) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);

    if (in_redirect_status == 1) {
        dup2(*fd_in, STDIN_FILENO);
        close(*fd_in);
    }
    if (out_redirect_status == 1) {
        dup2(*fd_out, STDOUT_FILENO);
        close(*fd_out);
    }

    return 0;
}

int execute_command(char **separated_commands) {
    int fd_in = -1, fd_out = -1, in_redirect_status = 0, out_redirect_status = 0, pipe_pos = -1;
    int fd[2];

    // check if build-in command (1 for yes)
    if (execute_buildin(separated_commands) == 1) {
        return 1;
    }

    if (check_redirection_pipe(separated_commands, &fd_in, &fd_out, &in_redirect_status, &out_redirect_status, &pipe_pos) < 0) {
        return -1;
    }

    if (pipe_pos == -1) {
        pid_t pid0 = fork();
        if (pid0 < 0) {
            // fork failed which shouldn't happen
            perror("fork 0 failed");
            exit(EXIT_FAILURE);
        } else if (pid0 == 0) {
            apply_redirection(&fd_in, &fd_out, in_redirect_status, out_redirect_status);
            if (in_redirect_status != -1 && out_redirect_status != -1) {
                execvp(separated_commands[0], separated_commands);
                perror("execvp failed");
                exit(EXIT_FAILURE);
            }
        } else {
            // parent
            int status;
            waitpid(pid0, &status, 0);
        }
    } else {
        if (pipe(fd) == -1) {
            perror("pipe failed");
            exit(EXIT_FAILURE);
        }

        pid_t pid1 = fork();
        if (pid1 < 0) {
            perror("fork 1 failed");
            exit(EXIT_FAILURE);
        } else if (pid1 == 0) {
            if (in_redirect_status == 1) {
                apply_redirection(&fd_in, NULL, in_redirect_status, 0);
            }
            dup2(fd[1], STDOUT_FILENO);
            close(fd[0]);
            close(fd[1]);
            execvp(separated_commands[0], separated_commands);
        }

        pid_t pid2 = fork();
        if (pid2 < 0) {
            perror("fork 2 failed");
            exit(EXIT_FAILURE);
        } else if (pid2 == 0) {
            if (out_redirect_status == 1) {
                apply_redirection(NULL, &fd_out, 0, out_redirect_status);
            }
            dup2(fd[0], STDIN_FILENO);
            close(fd[1]);
            close(fd[0]);
            execvp(separated_commands[pipe_pos + 1], &separated_commands[pipe_pos + 1]);
        }

        close(fd[0]);
        close(fd[1]);
        wait(NULL);
        wait(NULL);
    }

    return 0;
}


int main() {
    char *input;                    // user input
    char **separated_commands;      // separated commands
    char *username = "nyush";       // shell name (can be modified for future use)
    // int execute_status;             // execute status: 0, 1, -1

    // ignore SIGINT, SIGQUIT, and SIGTSTP using signal_handler
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGTSTP, signal_handler);

    while (1) {

        print_prompt(username);

        input = read_input();
        // skip the rest of loop if the input is consisted of whitespace, or a blank line
        if (input == NULL) {
            continue;
        }

        separated_commands = separate_input(input);

        execute_command(separated_commands);

        free(separated_commands);
        free(input);
    }

    return 0;
}