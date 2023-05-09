#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

void signal_handler(int signum){
    printf("\nSignal %d received.\n",signum);
}

//That function parse the user input line by '|'
void parser(char *input, char **commands, int *count){
    *count = 0;

    char *command = strtok(input, "|");
    while(command != NULL && *count < 20){
        commands[*count] = command;
        (*count)++;
        command = strtok(NULL, "|");
    }
}

//That function execude command 
void execute_command(char *command){
    execl("/bin/sh", "sh", "-c", command, NULL);
    perror("Error: ");
    exit(1);
}


int main(){
    
    char *commands[20]; //string array that holds commands

    char input[500];    //string that holds user input
    char filename[100]; //string that holds name of log file

    //time variables
    time_t raw_time;    
    struct tm *timeinfo;

    int count;  //command counter
    pid_t pid;  //fork variable
    
    int status; //signal status
    int fd[2];  //pipe file descriptors
    int pre_fd; //read file descriptor of previous pipe
    int log_fd; //log file descriptor

    //signal handling
    struct sigaction sa;
    sa.sa_handler = &signal_handler;
    sa.sa_flags = 0;

    if((sigemptyset(&sa.sa_mask) == -1) || sigaction(SIGINT,&sa,NULL) == -1)
        perror("Failed to install SIGINT signal handler");

    if((sigemptyset(&sa.sa_mask) == -1) || sigaction(SIGTERM,&sa,NULL) == -1)
        perror("Failed to install SIGTERM signal handler");
        
    while (1){

        //get input
        printf(">");
        fgets(input, 500, stdin);
        input[strcspn(input, "\n")] = '\0';

        //exit control
        if((strcmp(input , ":q")) == 0){
            printf("Input is ':q':Exiting\n");
            break;
        }

        //get current time and create file with specific name
        time(&raw_time);
        timeinfo = localtime(&raw_time);
        strftime(filename, sizeof(filename), "%H.%M.%S_log.txt", timeinfo);
        log_fd = open(filename, O_WRONLY | O_CREAT);

        if (log_fd == -1) {
            perror("Failed to create log file");
            exit(1);
        }

        //parse user input
        parser(input, commands, &count);
        
        //default value of pre_fd
        pre_fd = STDIN_FILENO;

        //pipeing algorithm and forking for all commands
        for(int i = 0; i < count; i++){

            //pipeing
            if (i < count - 1){
                if (pipe(fd) < 0)
                {
                    perror("pipe error");
                    break;
                }
            }

            //forking
            pid = fork();

            //error check
            if(pid < 0){
                perror("fork error");
                break;
            }

            //CHILD
            else if(pid == 0){
                
                //write command to log file
                size_t size = strlen(commands[i]);
                write(log_fd , commands[i] , size);

                char newline = '\n';
                write(log_fd, &newline, 1);

                //set STDOUT/STDIN
                if(i < count - 1){
                    dup2(fd[1], STDOUT_FILENO);
                    close(fd[1]);
                    close(fd[0]);
                }
                if(i > 0){
                    dup2(pre_fd, STDIN_FILENO);
                    close(pre_fd);
                }
                
                //execution
                execute_command(commands[i]);

                //if child process return execution it is error, exit the program
                exit(0);
            }

            //PARENT
            else{
                //close necessery file descriptors
                if(i < count - 1){
                    close(fd[1]);
                }
                if(i > 0){
                    close(pre_fd);
                }

                //set previous file descriptor
                pre_fd = fd[0];

                //wait for the child process
                waitpid(pid, &status, 0);
            }
        }
        //close log file
        close(log_fd);
        commands[0] = NULL;
    }

    return 0;
}