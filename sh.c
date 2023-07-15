#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_COMMAND_LENGTH 100
#define MAX_ARGUMENTS 10
#define MAX_PIPES 10

typedef struct {
    char command[MAX_COMMAND_LENGTH];
    char* arguments[MAX_ARGUMENTS];
    int inputRedirect;
    int outputRedirect;
    int appendRedirect;
    char inputFile[MAX_COMMAND_LENGTH];
    char outputFile[MAX_COMMAND_LENGTH];
    char appendFile[MAX_COMMAND_LENGTH];
} ParsedCommand;

typedef struct {
    ParsedCommand commands[MAX_PIPES + 1];
    int numCommands;
} ParsedInput;

void parseCommand(const char* input, ParsedCommand* parsedCommand) {
    memset(parsedCommand, 0, sizeof(ParsedCommand));

    char inputCopy[MAX_COMMAND_LENGTH];
    strncpy(inputCopy, input, sizeof(inputCopy) - 1);

    char* token = strtok(inputCopy, " ");
    int argIndex = 0;

    while (token != NULL && argIndex < MAX_ARGUMENTS - 1) {
        if (strcmp(token, "<") == 0) {
            parsedCommand->inputRedirect = 1;
            token = strtok(NULL, " ");
            if (token != NULL)
                strncpy(parsedCommand->inputFile, token, MAX_COMMAND_LENGTH - 1);
        } else if (strcmp(token, ">") == 0) {
            parsedCommand->outputRedirect = 1;
            token = strtok(NULL, " ");
            if (token != NULL)
                strncpy(parsedCommand->outputFile, token, MAX_COMMAND_LENGTH - 1);
        } else if (strcmp(token, ">>") == 0) {
            parsedCommand->appendRedirect = 1;
            token = strtok(NULL, " ");
            if (token != NULL)
                strncpy(parsedCommand->appendFile, token, MAX_COMMAND_LENGTH - 1);
        } else {
            parsedCommand->arguments[argIndex++] = token;
        }
        token = strtok(NULL, " ");
    }
    parsedCommand->arguments[argIndex] = NULL;
}

void parseInput(const char* input, ParsedInput* parsedInput) {
    memset(parsedInput, 0, sizeof(ParsedInput));

    char inputCopy[MAX_COMMAND_LENGTH];
    strncpy(inputCopy, input, sizeof(inputCopy) - 1);

    char* token = strtok(inputCopy, "|");
    int commandIndex = 0;

    while (token != NULL && commandIndex < MAX_PIPES + 1) {
        parseCommand(token, &parsedInput->commands[commandIndex++]);
        token = strtok(NULL, "|");
    }

    parsedInput->numCommands = commandIndex;
}

void executeCommand(const ParsedCommand* parsedCommand, int inputFd, int outputFd) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        // Child process

        if (parsedCommand->inputRedirect) {
            int inputFileDescriptor = open(parsedCommand->inputFile, O_RDONLY);
            if (inputFileDescriptor < 0) {
                perror("open");
                exit(1);
            }
            dup2(inputFileDescriptor, STDIN_FILENO);
            close(inputFileDescriptor);
        } else if (inputFd != STDIN_FILENO) {
            dup2(inputFd, STDIN_FILENO);
            close(inputFd);
        }

        if (parsedCommand->outputRedirect) {
            int outputFileDescriptor = open(parsedCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (outputFileDescriptor < 0) {
                perror("open");
                exit(1);
            }
            dup2(outputFileDescriptor, STDOUT_FILENO);
            close(outputFileDescriptor);
        } else if (parsedCommand->appendRedirect) {
            int appendFileDescriptor = open(parsedCommand->appendFile, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (appendFileDescriptor < 0) {
                perror("open");
                exit(1);
            }
            dup2(appendFileDescriptor, STDOUT_FILENO);
            close(appendFileDescriptor);
        } else if (outputFd != STDOUT_FILENO) {
            dup2(outputFd, STDOUT_FILENO);
            close(outputFd);
        }

        execvp(parsedCommand->arguments[0], parsedCommand->arguments);
        perror("execvp");
        exit(1);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <script_file>\n", argv[0]);
        return 1;
    }

    FILE* scriptFile = fopen(argv[1], "r");
    if (scriptFile == NULL) {
        perror("fopen");
        return 1;
    }

    char input[MAX_COMMAND_LENGTH];

    while (fgets(input, sizeof(input), scriptFile) != NULL) {
        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0)
            continue;

        ParsedInput parsedInput;
        parseInput(input, &parsedInput);

        int inputFd = STDIN_FILENO;
        int outputFd = STDOUT_FILENO;

        for (int i = 0; i < parsedInput.numCommands; i++) {
            int currentPipeFd[2];

            if (i < parsedInput.numCommands - 1) {
                if (pipe(currentPipeFd) == -1) {
                    perror("pipe");
                    exit(1);
                }
                outputFd = currentPipeFd[1];
            } else {
                outputFd = STDOUT_FILENO;
            }

            executeCommand(&parsedInput.commands[i], inputFd, outputFd);

            if (inputFd != STDIN_FILENO)
                close(inputFd);

            if (i < parsedInput.numCommands - 1) {
                close(outputFd);
                inputFd = currentPipeFd[0];
            }
        }
    }

    fclose(scriptFile);

    return 0;
}

