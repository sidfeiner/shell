#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

int runningProcesses[2];

/*
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

void execFromArgs(char **args) {
    execvp(args[0], args);
}

void waitAndPurge(pid_t pid, int index) {
    int status;
    waitpid(pid, &status, WCONTINUED | WUNTRACED);
    runningProcesses[index] = 0;
}

/**
 * @param pipe array with file descriptors. NULL if no piping is needed
 * @param pipeDirection Decides if we're reading from pipe or writing to it. 0 is read, 1 is write.
 * @param processIndex index in runningProcesses that pid will be kept
 * @param block If pipe is not null, this will be ignored and behaviour will be non blocking
 *              Otherwise, will block for pid until done
 * @return
 */
int processCmd(int count, char **arglist, int *pipe, int pipeDirection, int processIndex, int block) {
    int isBackground = arglist[count - 1][0] == '&' ? 1 : 0;
    pid_t pid = fork();
    switch (pid) {
        case -1:
            printf("fork failed: %s\n", strerror(1));
            return -1;
        case 0:
            // Child
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
            break;
        default:
            //Parent
            if (!isBackground) {
                runningProcesses[processIndex] = pid;
                if (block == 1) {
                    waitAndPurge(pid, processIndex);
                }
            }
    }
    return pid;
}


int handlePipe(int count, char **arglist, int indexOfPipe) {
    arglist[indexOfPipe] = NULL;
    int fds[2];
    if (pipe(fds) == -1) {
        printf("could not init pipe: %s", strerror(3));
        return 3;
    }
    pid_t firstChildPid, secondChildPid;

    if (-1 == (firstChildPid = processCmd(count, arglist, fds, 1, 0, 0))) {
        return -1;
    }
    if (-1 == (secondChildPid = processCmd(count - (indexOfPipe + 1), arglist + 1 + indexOfPipe, fds, 0, 1, 0))) {
        return -1;
    }
    close(fds[0]);
    close(fds[1]);

    waitAndPurge(firstChildPid, 0);
    waitAndPurge(secondChildPid, 1);
    return 1;
}

int process_arglist(int count, char **arglist) {
    int indexOfPipe = indexOf(arglist, "|", count);
    if (indexOfPipe >= 0) {
        return handlePipe(count, arglist, indexOfPipe);
    } else {
        int pid = processCmd(count, arglist, NULL, -1, 0, 1);
        return pid >= 0 ? 1 : 0;
    }
}

void SIGINTHandler(int sig) {
    if (runningProcesses[0] != 0) {
        kill(runningProcesses[0], sig);
        runningProcesses[0] = 0;
    }
    if (runningProcesses[1] != 0) {
        kill(runningProcesses[1], sig);
        runningProcesses[1] = 0;
    }
}

int initHandler(int s, void (*f)(int)) {
    struct sigaction sa;
    sa.sa_handler = f;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    return sigaction(s, &sa, NULL);
}

// prepare and finalize calls for initialization and destruction of anything required
int prepare() {
    if (initHandler(SIGINT, SIGINTHandler) < 0) {
        printf("could not define handler: %s\n", strerror(6));
        return 6;
    }
    if (initHandler(SIGCHLD, SIG_IGN)) {
        printf("could not define handler: %s\n", strerror(6));
        return 6;
    }
    return 0;
}

int finalize() {
    return 0;
}
