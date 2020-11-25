#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

int foregroundProcesses[2];

/**
 * Returns the index of the first appearance of `item` in array, -1 if not found
 */
int indexOf(char **array, char *item, int count) {
    char **tempArray, **limit = array + count;
    int index;
    for (tempArray = array, index = 0; tempArray < limit; tempArray++, index++) {
        if (strcmp(*tempArray, item) == 0) return index;
    }
    return -1;
}

int execFromArgs(char **args) {
    return execvp(args[0], args);
}

/**
 * wait for process to end and once it's done, remove it from the running processes array
 */
void waitPid(pid_t pid) {
    int status;
    waitpid(pid, &status, WCONTINUED | WUNTRACED);
}

int initHandler(int s, void (*f)(int)) {
    struct sigaction sa;
    sa.sa_handler = f;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    return sigaction(s, &sa, NULL);
}

void killForegroundProcess(int index) {
    if (foregroundProcesses[index] != 0) {
        kill(foregroundProcesses[index], SIGKILL);
        foregroundProcesses[index] = 0;
    }
}

void sigIntHandler(int sig) {
    if (sig == SIGINT) {
        killForegroundProcess(0);
        killForegroundProcess(1);
    }
}

// prepare and finalize calls for initialization and destruction of anything required
int prepare() {
    if (initHandler(SIGINT, sigIntHandler) + initHandler(SIGCHLD, SIG_IGN) < 0) {
        printf("could not define SIGINT/SIGCHLD handler: %s\n", strerror(6));
        return 6;
    }
    return 0;
}

int finalize() {}

void blockSignal(int sig, sigset_t *set) {
    sigemptyset(set);
    sigaddset(set, sig);
    sigprocmask(SIG_BLOCK, set, NULL);
}

/**
 * @param pipe array with file descriptors. NULL if no piping is needed
 * @param pipeDirection Decides if we're reading from pipe or writing to it. 0 is read, 1 is write.
 * @param processIndex index in runningProcesses that pid will be kept
 * @param block If pipe is not null, this will be ignored and behaviour will be non blocking
 *              Otherwise, will block for pid until done
 * @return
 */
int processCmd(int count, char **arglist, int *pipe, int pipeDirection, int block, int index) {
    sigset_t blockSet;
    int isBackground = arglist[count - 1][0] == '&' ? 1 : 0;
    if (!isBackground && block == 1) blockSignal(SIGINT, &blockSet); // Block SIGINT until child process is persisted
    pid_t pid = fork();
    switch (pid) {
        case -1:
            printf("fork failed: %s\n", strerror(1));
            return -1;
        case 0:
            // Child
            if (initHandler(SIGINT, SIG_IGN) < 0) {
                printf("could not define SIGINT handler in child: %s\n", strerror(6));
                return 6;
            }
            if (isBackground) {
                arglist[count - 1] = NULL;  // Remove ampersand from command arguments
            }
            // Piping handling
            if (pipe != NULL) {
                if (pipeDirection == 1) { // Write
                    close(pipe[0]);
                    dup2(pipe[1], STDOUT_FILENO);
                    close(pipe[1]);
                } else { // Read
                    close(pipe[1]);
                    dup2(pipe[0], STDIN_FILENO);
                    close(pipe[0]);
                }
            }
            execFromArgs(arglist);
            printf("execvp failed: %s\n", strerror(1));
            exit(1);
        default:
            //Parent
            if (!isBackground && block == 1) {
                foregroundProcesses[index] = pid;
                sigprocmask(SIG_UNBLOCK, &blockSet, NULL);  // Now pid has been persisted, handle SIGINT if needed
                waitPid(pid);
                foregroundProcesses[index] = 0;
            }
    }
    return pid;
}


int handlePipe(int count, char **arglist, int indexOfPipe) {
    arglist[indexOfPipe] = NULL;  // Ensure first command runs only with it's arguments
    int fds[2];
    if (pipe(fds) == -1) {
        printf("could not init pipe: %s", strerror(3));
        return 3;
    }
    pid_t firstChildPid, secondChildPid;

    // Run first command
    if (-1 == (firstChildPid = processCmd(count, arglist, fds, 1, 0, 0))) {
        return -1;
    }

    // Run second command, arguments being after the pipe
    if (-1 == (secondChildPid = processCmd(count - (indexOfPipe + 1), arglist + 1 + indexOfPipe, fds, 0, 0, 1))) {
        return -1;
    }
    close(fds[0]);
    close(fds[1]);

    waitPid(firstChildPid);
    waitPid(secondChildPid);
    return 1;
}

int process_arglist(int count, char **arglist) {
    int indexOfPipe = indexOf(arglist, "|", count);
    if (indexOfPipe >= 0) {
        return handlePipe(count, arglist, indexOfPipe);
    } else {
        int pid = processCmd(count, arglist, NULL, -1, 1, 0);
        return pid >= 0 ? 1 : 0;
    }
}