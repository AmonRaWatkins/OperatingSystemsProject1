#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_COMMAND_LINE_LEN 1024
#define MAX_COMMAND_LINE_ARGS 128

char prompt[MAX_COMMAND_LINE_LEN];
extern char **environ;

pid_t foreground_pid; // To keep track of the foreground process ID

// Signal handler for SIGINT (Ctrl+C)
void sigint_handler(int signo) {
    printf("\n%s", prompt); // Print prompt again
    fflush(stdout);
}

// Signal handler for SIGALRM
void sigalrm_handler(int signo) {
    if (foreground_pid > 0) {
        printf("\nProcess timed out. Terminating process %d.\n", foreground_pid);
        kill(foreground_pid, SIGKILL); // Terminate the foreground process
    }
}

// Function to tokenize the command line into an array of tokens
char** tokenize_command(char *command_line) {
    char **arguments = malloc(MAX_COMMAND_LINE_ARGS * sizeof(char*));
    if (arguments == NULL) {
        perror("malloc failed");
        exit(1);
    }

    char *token = strtok(command_line, " \t\r\n");
    int i = 0;
    while (token != NULL) {
        arguments[i] = strdup(token);
        if (arguments[i] == NULL) {
            perror("strdup failed");
            exit(1);
        }
        i++;
        token = strtok(NULL, " \t\r\n");
    }
    arguments[i] = NULL; // Null-terminate the array
    return arguments;
}

// Function to handle the "cd" command
void handle_cd(char **arguments) {
    if (arguments[1] == NULL) {
        chdir(getenv("HOME")); // Change to home directory if no argument provided
    } else {
        if (chdir(arguments[1]) != 0) {
            perror("chdir failed");
        }
    }
}

// Function to handle the "pwd" command
void handle_pwd() {
    char cwd[MAX_COMMAND_LINE_LEN];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("getcwd() error");
    }
}

// Function to handle the "echo" command
void handle_echo(char **arguments) {
    for (int i = 1; arguments[i] != NULL; i++) {
        if (arguments[i][0] == '$') {
            char *variable = arguments[i] + 1; // Skip the '$'
            char *value = getenv(variable);
            printf("%s ", value ? value : "");
        } else {
            printf("%s ", arguments[i]);
        }
    }
    printf("\n");
}

// Function to handle the "env" command
void handle_env() {
    for (int i = 0; environ[i] != NULL; i++) {
        printf("%s\n", environ[i]);
    }
}

// Function to handle the "setenv" command
void handle_setenv(char **arguments) {
    if (arguments[1] == NULL || arguments[2] == NULL) {
        fprintf(stderr, "Usage: setenv <variable> <value>\n");
        return;
    }
    if (setenv(arguments[1], arguments[2], 1) != 0) { // Overwrite existing variable
        perror("setenv failed");
    }
}

// Function to handle redirection and piping
void handle_redirection_and_piping(char **arguments) {
    int fd;

    // Check for output redirection
    for (int i = 0; arguments[i] != NULL; i++) {
        if (strcmp(arguments[i], ">") == 0) {
            arguments[i] = NULL; // Null-terminate the command arguments
            fd = open(arguments[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open file for writing
            if (fd == -1) {
                perror("open failed");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO); // Redirect stdout to file
            close(fd); // Close the file descriptor
            break;
        }

        // Check for input redirection
        if (strcmp(arguments[i], "<") == 0) {
            arguments[i] = NULL; // Null-terminate the command arguments
            fd = open(arguments[i + 1], O_RDONLY); // Open file for reading
            if (fd == -1) {
                perror("open failed");
                exit(1);
            }
            dup2(fd, STDIN_FILENO); // Redirect stdin from file
            close(fd); // Close the file descriptor
            break;
        }

        // Check for piping
        if (strcmp(arguments[i], "|") == 0) {
            int pipefd[2];
            pipe(pipefd); // Create a pipe
            
            pid_t pid1 = fork();
            if (pid1 == 0) { // First child process
                dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe write end
                close(pipefd[0]); // Close unused read end of pipe
                execvp(arguments[0], arguments); // Execute first command
                perror("exec failed");
                exit(EXIT_FAILURE);
            } else { 
                pid_t pid2 = fork();
                if (pid2 == 0) { // Second child process
                    dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to pipe read end
                    close(pipefd[1]); // Close unused write end of pipe
                    execvp(arguments[i + 1], &arguments[i + 1]); // Execute second command after pipe
                    perror("exec failed");
                    exit(EXIT_FAILURE);
                } else { 
                    close(pipefd[0]); // Close both ends in parent process
                    close(pipefd[1]);
                    waitpid(pid1, NULL, 0); // Wait for first child to finish
                    waitpid(pid2, NULL, 0); // Wait for second child to finish
                }
            }
            return; // Exit after handling pipe redirection
        }
    }
}

// Function to launch external commands with timeout handling and I/O redirection support.
void launch_process(char **arguments, bool background) {
    pid_t pid;

    pid = fork(); // Fork a child process

    if (pid == -1) { // Error forking
        perror("fork failed");
        return;
    } else if (pid == 0) { // Child process
        
        handle_redirection_and_piping(arguments); // Handle any redirection before executing
        
        printf("Executing: %s\n", arguments[0]);  // Debugging output
        
        if (execvp(arguments[0], arguments) == -1) {
            perror("exec failed"); 
            exit(EXIT_FAILURE); 
        }
    } else { 
        foreground_pid = pid; 
        
        if (!background) { 
            alarm(10); // Set an alarm for 10 seconds

            int status;
            waitpid(pid, &status, 0); // Wait for child process to finish

            alarm(0); // Cancel any pending alarms after process completes
            
            foreground_pid = -1; // Reset foreground PID after completion
            
            if (WIFEXITED(status)) {
                int exit_status = WEXITSTATUS(status);
                printf("Process exited with status %d\n", exit_status);
            } else if (WIFSIGNALED(status)) {
                printf("Process was killed by signal %d\n", WTERMSIG(status));
            }
        } else { 
            printf("[Process ID: %d]\n", pid); 
            foreground_pid = -1; 
        }
    }
}

int main() {
    char command_line[MAX_COMMAND_LINE_LEN];

    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;

    struct sigaction sa_alrm;
    sa_alrm.sa_handler = sigalrm_handler;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = 0;

    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGALRM, &sa_alrm, NULL);

    while (true) {
        char cwd[MAX_COMMAND_LINE_LEN];
        
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            sprintf(prompt, "%s> ", cwd); 
        } else {
            perror("getcwd() error");
            continue;
        }

        printf("%s", prompt);
        fflush(stdout);

        if (fgets(command_line, MAX_COMMAND_LINE_LEN, stdin) == NULL) {
            fprintf(stderr, "fgets error\n");
            exit(0);
        }

        char **arguments = tokenize_command(command_line);

       bool background = false;
        
       int arg_count = 0;
        
       while (arguments[arg_count] != NULL) { 
           arg_count++; 
       }

       /* Check for background process indicator '&' */
       if (arg_count > 0 && strcmp(arguments[arg_count - 1], "&") == 0) { 
           free(arguments[arg_count - 1]); 
           arguments[arg_count - 1] = NULL; 
           background = true; 
       }

       /* Implement Built-In Commands or Launch Processes */
       if (arguments[0] == NULL) {
           free(arguments);
           continue; 
       } else if (strcmp(arguments[0], "cd") == 0) {
           handle_cd(arguments);
       } else if (strcmp(arguments[0], "pwd") == 0) {
           handle_pwd();
       } else if (strcmp(arguments[0], "echo") == 0) {
           handle_echo(arguments);
       } else if (strcmp(arguments[0], "exit") == 0) {
           free(arguments);
           break; 
       } else if (strcmp(arguments[0], "env") == 0) {
           handle_env();
       } else if (strcmp(arguments[0], "setenv") == 0) {
           handle_setenv(arguments);
       } else { 
           launch_process(arguments, background); 
       }

       /* Free allocated memory */
       for (int j = 0; arguments[j] != NULL; j++) {
           free(arguments[j]);
       }
       
       free(arguments); 
   }

   return 0;
}