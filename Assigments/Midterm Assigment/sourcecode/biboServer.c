#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <dirent.h>
#include <sys/sem.h>

#define MAX_FILENAME 256
#define MAX_BUFFER 1024
#define MAX_CLIENTS 100
#define MAX_PATH 1024

typedef struct {
    int client_pid;
    int client_fifo;
} ClientInfo;

typedef struct {
    ClientInfo clients[MAX_CLIENTS];
    int front;
    int rear;
    int count;
} ServerQueue;

ServerQueue* sharedQueue;
int server_pid;


void handleSignal(int);
void terminateAllClients();
int process(const char*, int, char*);
int handleClientRequests(int, int, int, char*);


int main(int argc, char *argv[]){
    // Parse command-line arguments (directory name, max number of clients)
    if(argc != 3) 
    {
        printf("Usage: biboServer <dirname> <max. #ofClients>\n");
        return 1;
    }
    
    key_t semKey = ftok("/tmp", 'S');
    int semaphore = semget(semKey, 1, IPC_CREAT | IPC_EXCL | 0666);
    
    if(semaphore == -1) 
    {
        if(errno == EEXIST) 
        {
            // Semaphore already exists, open it instead
            semaphore = semget(semKey, 1, 0);
            if (semaphore == -1) {
                perror("Error opening semaphore");
                exit(1);
            }
        } 
        else 
        {
            perror("Error creating semaphore");
            exit(1);
        }
    }

    char *dirname = argv[1];
    int max_clients = atoi(argv[2]);

    // Create the specified directory if it doesn't exist
    if(mkdir(dirname, 0755) == -1 && errno != EEXIST) 
    {
        perror("Error creating directory");
        return 1;
    }

    char currentPath[MAX_FILENAME];
    getcwd(currentPath, sizeof(currentPath));
    

    // Enter the directory using chdir
    if(chdir(dirname) == -1) 
    {
        perror("Error changing directory");
        return 1;
    }
    
    int log_file = open("client_log.txt", O_WRONLY | O_TRUNC | O_CREAT, 0644);
    
    if(log_file == -1) 
    {
        perror("Error creating log file");
        close(log_file);
    }

    server_pid = getpid();
    // Display the server's PID using getpid and printf
    printf(">> Server Started PID %d...\n", server_pid);

    // Set up signal handlers for graceful termination
    signal(SIGINT, handleSignal);

    char serverFifoName[32];
    sprintf(serverFifoName, "/tmp/biboServer_%d", getpid());

    if(mkfifo(serverFifoName, 0666) == -1) 
    {
        perror("Error creating server FIFO");
        exit(1);
    }
    
    key_t key = ftok(serverFifoName, 65);
    if(key == -1) 
    {
        perror("Error generating key");
        exit(1);
    }

    int shmid = shmget(key, sizeof(ServerQueue), 0666 | IPC_CREAT);
    if(shmid == -1) 
    {
        perror("Error creating shared memory segment");
        exit(1);
    }

    sharedQueue = (ServerQueue *)shmat(shmid, NULL, 0);
    if(sharedQueue == (void *)-1) 
    {
        perror("Error attaching shared memory segment");
        exit(1);
    }

    sharedQueue->front = 0;
    sharedQueue->rear = -1;
    sharedQueue->count = 0;
    
    semctl(semaphore, 0, SETVAL, max_clients);

    printf(">> waiting for clients...\n");
    // Enter the main server loop
    while(1) 
    {
        // Accept client connections and add them to the server queue
        int client_pid;
        int server_fifo = open(serverFifoName, O_RDONLY);

        if(server_fifo == -1) 
        {
            perror("Error opening server FIFO");
            exit(1);
        }
        
        if(read(server_fifo, &client_pid, sizeof(int)) > 0) 
        {
            
            if(sharedQueue->count == max_clients)
            {
                printf(">> Connection request PID %d... Que FULL\n",client_pid);
            }

            // Wait for a slot in the queue
            struct sembuf semaphoreOperation = { 0, -1, 0 };
            semop(semaphore, &semaphoreOperation, 1);
            
            char client_fifo_name[MAX_FILENAME];
            snprintf(client_fifo_name, MAX_FILENAME, "/tmp/biboClient_%d", client_pid);

            int client_fifo = open(client_fifo_name, O_RDONLY);
            if(client_fifo == -1) 
            {
                perror("Error opening client FIFO");
                continue;
            }
            
            char client_fifo_response_name[MAX_FILENAME];
            snprintf(client_fifo_response_name, MAX_FILENAME, "/tmp/biboClientResponse_%d", client_pid);
            int clientResponseFifo = open(client_fifo_response_name, O_WRONLY);

            if(clientResponseFifo == -1) 
            {
                perror("Error opening client Response Fifo FIFO");
            }
            
            close(log_file);

            sharedQueue->rear++;
            sharedQueue->clients[sharedQueue->rear].client_pid = client_pid;
            sharedQueue->clients[sharedQueue->rear].client_fifo = client_fifo;
            sharedQueue->count++;
            
            printf(">> Client PID %d connected as \"client%02d\"\n", client_pid, sharedQueue->count);
            
            // Fork child processes to handle client requests
            pid_t pid = fork();
            if(pid == 0) 
            {
                // Child process: Handle client requests
                while(1) 
                {
                    int check = handleClientRequests(client_fifo, sharedQueue->count, clientResponseFifo, currentPath);
                    if (check == 1)
                        break;
                    else if(check == 2)
                    {
                        handleSignal(SIGTERM);
                    }
                    
                }
                printf(">> \"client%02d\" disconnected...\n", sharedQueue->count);
                
                //Remove the client from the queue
                sharedQueue->clients[sharedQueue->rear] = sharedQueue->clients[sharedQueue->rear + 1];
                sharedQueue->rear--;
                // Release the slot in the queue
                semaphoreOperation.sem_op = 1;
                semop(semaphore, &semaphoreOperation, 1);
                
                close(client_fifo);
                exit(0);
            } 
            else 
            {
                // Parent process: continue listening for incoming client connections
                close(client_fifo);
                close(clientResponseFifo);
                continue;
            }
        }
    }
    return 0;
}


// Define your signal handler function here
void handleSignal(int signal){
    if(signal == SIGTERM)
    {
        printf(">> kill signal from \"client%02d\".. terminating..\n",sharedQueue->count);
        printf(">> bye");
    }
    terminateAllClients();
    kill(server_pid,SIGTERM);
}

void terminateAllClients(){
    while(sharedQueue->count > 0) 
    {
        // Get the client at the front of the sharedQueue
        ClientInfo* client = &sharedQueue->clients[sharedQueue->front];

        // Send termination message to the client
        kill(client->client_pid,SIGTERM);

        // Update the sharedQueue's front index and count
        sharedQueue->front = (sharedQueue->front + 1) % MAX_CLIENTS;
        sharedQueue->count--;
    }
}

int process(const char* request, int clientResponseFifo, char* programPath){

    int numTokens = 1;  // At least one token (even if the string is empty)
    for(size_t i = 0; i < strlen(request); i++) 
    {
        if(request[i] == ' ') 
        {
            (numTokens)++;
        }
    }
    
    // Allocate memory for the array of token strings
    char** tokens = (char**)malloc(numTokens * sizeof(char*));
    if(tokens == NULL) 
    {
        printf("Memory allocation failed.\n");
        exit(1);
    }
    
    // Copy each token into the array
    char* token = strtok((char*)request, " ");
    size_t index = 0;
    while(token != NULL) 
    {
        // Allocate memory for each token string
        tokens[index] = (char*)malloc((strlen(token) + 1) * sizeof(char));
        if(tokens[index] == NULL) 
        {
            printf("Memory allocation failed.\n");
            exit(1);
        }
        
        // Copy the token into the array
        strcpy(tokens[index], token);
        
        // Get the next token
        token = strtok(NULL, " ");
        index++;
    }

    char response[MAX_BUFFER] = "";

    if(strncmp(tokens[0], "help", 4) == 0) 
    {
        // Send the list of possible client requests as a response
        if(numTokens == 1)
        { 
            strcpy(response, "\n    Available commands:\nhelp, list, readF, writeT, upload, download, quit, killServer\n\n");
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);   
        }
        else if(numTokens == 2)
        {
            if(strncmp(tokens[1], "list", 4) == 0) 
            {
                strcpy(response,"\n    -list\n        sends a request to display the list of files in Servers directory\n\n"); 
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
            else if(strncmp(tokens[1], "readF", 5) == 0) 
            {
                strcpy(response,"\n    -readF <file> <line #>\n        requests to display the # line of the <file>, if no line number is given the whole contents of the file is requested\n\n"); 
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
            else if(strncmp(tokens[1], "writeF", 6) == 0) 
            {    
                strcpy(response,"\n    -writeF <file> <line #> <string>\n        request to write the content of “string” to the #th line the <file>, if the line # is not given writes to the end of file.\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
            else if(strncmp(tokens[1], "upload", 6) == 0) 
            {
                strcpy(response,"\n    -upload <file>\n        uploads the file from the current working directory of client to the Servers directory\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
            else if(strncmp(tokens[1], "download", 8) == 0) 
            {
                strcpy(response,"\n    -download <file>n        request to receive <file> from Servers directory to client side\n\n"); 
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
            else if(strncmp(tokens[1], "quit", 4) == 0) 
            {
                strcpy(response,"\n    -quit\n        Send write request to Server side log file and quits\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
            else if(strncmp(tokens[1], "killServer", 10) == 0) 
            {
                strcpy(response,"\n    -killServer\n        Sends a kill request to the Server\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
            else 
            {
                strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }
        }
        else
        {
            strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.\n\n");
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);
        }
        return 0;
    } 
    else if(strncmp(tokens[0], "list", 4) == 0) 
    {
        if(numTokens == 1)
        {
            DIR* directory = opendir(".");
            if(directory == NULL) 
            {
                printf("Failed to open the directory.\n");
                return 1;
            }
            
            // Read directory entries
            struct dirent* entry;
            while((entry = readdir(directory)) != NULL) 
            {
                strncat(response, entry->d_name, sizeof(response) - strlen(response) - 1);
                strncat(response, "\n", sizeof(response) - strlen(response) - 1);
            }
            closedir(directory);
            
            response[strlen(response)] = '\0';
            int bytes_written = write(clientResponseFifo, &response, MAX_BUFFER);
            if (bytes_written == -1) 
            {
                perror("Error writing to client FIFO");
            } 
            else if(bytes_written < MAX_BUFFER) 
            {
                printf(">> Warning: Only %d bytes written to client FIFO\n", bytes_written);
            } 
            return 0;
        }
        else
        {
            strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.\n\n");
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);
            return 0;
        }
    } 
    else if(strncmp(tokens[0], "readF", 5) == 0) 
    {
        if(numTokens == 2)
        {   
            char currentPath[MAX_FILENAME];
            getcwd(currentPath, sizeof(currentPath));
            char filePath[MAX_BUFFER];
            snprintf(filePath, sizeof(filePath), "%s/%s", currentPath, tokens[1]);

            int fd = open(filePath, O_RDONLY);
            if(fd == -1)
            {
                strcpy(response, "\n    File don't exist.\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, strlen(response));
                close(fd);
            } 
            else
            {
                int bytes_read = read(fd, response, MAX_BUFFER);
                if (bytes_read == -1) 
                {
                    strcpy(response, "\n    Error occured while reading file.\n\n");
                    response[strlen(response)] = '\0';
                    write(clientResponseFifo, &response, strlen(response));
                    close(fd);
                }
                else
                {
                    write(clientResponseFifo, &response, MAX_BUFFER);
                    close(fd);
                }
            }
        }
        else if(numTokens == 3)
        {
            char currentPath[MAX_FILENAME];
            getcwd(currentPath, sizeof(currentPath));
            char filePath[MAX_BUFFER];
            snprintf(filePath, sizeof(filePath), "%s/%s", currentPath, tokens[1]);

            int fd = open(filePath, O_RDONLY);
            if(fd == -1)
            {
                strcpy(response, "\n    File don't exist.\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
                close(fd);
            } 
            else
            {
                int bytes_read = read(fd, response, MAX_BUFFER);
                if(bytes_read == -1) 
                {
                    strcpy(response, "\n    Error occured while reading file.\n\n");
                    response[strlen(response)] = '\0';
                    write(clientResponseFifo, &response, MAX_BUFFER);
                    close(fd);
                }
                else
                {
                    char buffer[MAX_BUFFER];
                    strcpy(buffer, response);
                    char* line = strtok(buffer, "\n");
                    for(int i=0 ; i<atoi(tokens[2])-1 ; i++)
                    {
                        line = strtok(NULL, "\n");
                    }

                    strcpy(response, line);
                    write(clientResponseFifo, &response, MAX_BUFFER);
                    close(fd);
                }
            }
        }
        else
        {
            strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.\n\n");
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, strlen(response));
        }
        return 0;
    } 
    else if(strncmp(tokens[0], "writeF", 6) == 0) 
    {
        char* endptr;
        long int convertedValue = strtol(tokens[2], &endptr, 10);

        if(numTokens < 3)
        {
            strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.\n\n");
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);
        }
        else if(endptr == tokens[2])
        {
            char currentPath[MAX_FILENAME];
            getcwd(currentPath, sizeof(currentPath));
            char filePath[MAX_BUFFER];
            snprintf(filePath, sizeof(filePath), "%s/%s", currentPath, tokens[1]);

            int fd = open(filePath, O_WRONLY | O_APPEND | O_CREAT);
            if(fd == -1)
            {
                strcpy(response, "\n    Error occured when open file descriptor in write only mode.\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
                close(fd);
            } 
            else
            {
                for(int i=2 ; i<numTokens ; i++)
                {
                    ssize_t bytes_written = write(fd, tokens[i], strlen(tokens[i]));
                    ssize_t bytes_written2 = write(fd, " ", sizeof(char));
                    if(bytes_written + bytes_written2 == -1)
                    {
                        perror("Error writing to file");
                        close(fd);
                        return 1;
                    }
                    
                }
                strcpy(response, "\n    write process completed succesfully..\n");
                write(clientResponseFifo, &response, MAX_BUFFER);
                close(fd);
            }

        }
        else
        {
            char currentPath[MAX_FILENAME];
            getcwd(currentPath, sizeof(currentPath));
            char filePath[MAX_BUFFER];
            snprintf(filePath, sizeof(filePath), "%s/%s", currentPath, tokens[1]);

            // Step 1: Read the existing contents of the file into memory
            int fd = open(filePath, O_RDWR);
            if(fd == -1) 
            {
                perror("Error opening file");
                return 1;
            }

            off_t fileSize = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);

            char* fileContents = (char*)malloc(fileSize + 1);
            if(fileContents == NULL) 
            {
                perror("Memory allocation failed");
                close(fd);
                return 1;
            }

            ssize_t bytesRead = read(fd, fileContents, fileSize);
            if(bytesRead == -1) 
            {
                perror("Error reading file");
                free(fileContents);
                close(fd);
                return 1;
            }

            fileContents[fileSize] = '\0';  // Null-terminate the contents

            // Step 2: Modify the desired line in memory
            int lineIndex = atoi(tokens[2])-1;
            
            size_t totalLength = 0;
            for(int i = 3; i < numTokens; i++) 
            {
                totalLength += strlen(tokens[i]);
            }

            // Allocate memory for the concatenated string
            char* newline = (char*)malloc(totalLength + numTokens-2);  // +1 for the null terminator
            if(newline == NULL) 
            {
                perror("Memory allocation failed");
            }

            // Copy each string to the concatenated string buffer
            char* currentPosition = newline;
            for(int i = 3; i < numTokens; i++) 
            {
                size_t tokenLength = strlen(tokens[i]);
                memcpy(currentPosition, tokens[i], tokenLength);
                currentPosition += tokenLength;
                if(i < numTokens - 1) 
                {
                    *currentPosition = ' ';
                    currentPosition++;
                }
            }

            // Add the null terminator at the end
            *currentPosition = '\0';

            char* newlineStart = NULL;
            char* newlineEnd = NULL;
            int lineCount = 0;

            // Find the start and end positions of the desired line
            char* lineStart = fileContents;
            while(*lineStart != '\0') 
            {
                if(lineCount == lineIndex) 
                {
                    newlineStart = lineStart;
                    while(*lineStart != '\n' && *lineStart != '\0') 
                    {
                        lineStart++;
                    }
                    newlineEnd = lineStart;
                    break;
                }
                if(*lineStart == '\n') 
                {
                    lineCount++;
                }
                lineStart++;
            }

            // Replace the old line with the new line
            if(newlineStart != NULL && newlineEnd != NULL) 
            {
                size_t newLineLength = strlen(newline);
                size_t oldLineLength = newlineEnd - newlineStart;
                size_t shift = newLineLength - oldLineLength;

                if(shift > 0)
                {
                    // Increase the size of the file contents buffer if needed
                    size_t newFileSize = fileSize + shift;
                    char* newFileContents = (char*)realloc(fileContents, newFileSize + 1);
                    if(newFileContents == NULL) 
                    {
                        perror("Memory reallocation failed");
                        free(fileContents);
                        close(fd);
                        return 1;
                    }
                    fileContents = newFileContents;
                    fileSize = newFileSize;
                
                }

                if (shift != 0) 
                {
                    // Shift the content after the replaced line
                    memmove(newlineEnd + shift, newlineEnd, fileSize - (newlineEnd - fileContents) + 1);
                }

                // Copy the new line to the appropriate position
                memcpy(newlineStart, newline, newLineLength);

                // Adjust the file size accordingly
                fileSize += shift;
            }

            // Step 3: Write the modified contents back to the file
            lseek(fd, 0, SEEK_SET);
            ssize_t bytesWritten = write(fd, fileContents, fileSize);
            if(bytesWritten == -1) 
            {
                perror("Error writing to file");
            }

            strcpy(response, "\n    write process completed succesfully..\n");
            write(clientResponseFifo, &response, MAX_BUFFER);
            close(fd);
        }

        return 0;
    } 
    else if(strncmp(tokens[0], "upload", 6) == 0) 
    {
        if(numTokens == 2)
        {
            char currentPath[MAX_PATH];
            if(getcwd(currentPath, sizeof(currentPath)) == NULL) 
            {
                perror("Error getting current directory");
                return 1;
            }

            char oldFilePath[MAX_PATH];
            snprintf(oldFilePath, sizeof(oldFilePath)+1, "%s/%s", programPath, tokens[1]);
            
            char newFilePath[MAX_PATH];
            snprintf(newFilePath, sizeof(newFilePath)+1, "%s/%s", currentPath, tokens[1]);

            if(rename(oldFilePath, newFilePath) != 0) 
            {
                strcpy(response, "\n    No such a file or directory.\n\n");
                response[strlen(response)] = '\0';
                write(clientResponseFifo, &response, MAX_BUFFER);
            }

            int fd = open(newFilePath, O_RDONLY);
            off_t size = lseek(fd, 0, SEEK_END);
            
            char* message = NULL;
            int length = snprintf(NULL, 0, "\n    file transfer request received. Beginning file transfer:\n    %ld bytes transferred:\n", (long)size);
            message = malloc((length + 1) * sizeof(char));
            length = sprintf(message, "\n    file transfer request received. Beginning file transfer:\n    %ld bytes transferred:\n", (long)size);

            strcpy(response, message);
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);
            close(fd);
        }
        else
        {
            strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.\n\n");
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);
        }
    } 
    else if(strncmp(tokens[0], "download", 8) == 0) 
    {
        if(numTokens == 2)
        {
            char currentPath[MAX_PATH];
            if(getcwd(currentPath, sizeof(currentPath)) == NULL) 
            {
                perror("Error getting current directory");
                return 1;
            }

            char oldFilePath[MAX_PATH];
            snprintf(oldFilePath, sizeof(oldFilePath)+1, "%s/%s", currentPath, tokens[1]);
            
            char newFilePath[MAX_PATH];
            snprintf(newFilePath, sizeof(newFilePath)+1, "%s/%s", programPath, tokens[1]);

            if(rename(oldFilePath, newFilePath) != 0) 
            {
                perror("Error moving file");
                return 1;
            }

            int fd = open(newFilePath, O_RDONLY);
            off_t size = lseek(fd, 0, SEEK_END);
            
            char* message = NULL;
            int length = snprintf(NULL, 0, "\n    file transfer request received. Beginning file transfer:\n    %ld bytes transferred:\n", (long)size);
            message = malloc((length + 1) * sizeof(char));
            length = sprintf(message, "\n    file transfer request received. Beginning file transfer:\n    %ld bytes transferred:\n", (long)size);

            strcpy(response, message);
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);

            close(fd);
        }
        else
        {
            strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.\n\n");
            response[strlen(response)] = '\0';
            write(clientResponseFifo, &response, MAX_BUFFER);
        }
    } 
    else if(strncmp(tokens[0], "quit", 4) == 0) 
    {
        strcpy(response, "quit");
        response[strlen(response)] = '\0';

        write(clientResponseFifo, &response, MAX_BUFFER);
        return 1;
    } 
    else if(strncmp(tokens[0], "killServer", 10) == 0) 
    {
        strcpy(response,"");
        write(clientResponseFifo, response, MAX_BUFFER);
        return 2;
    } 
    else 
    {
        strcpy(response, "\n    Invalid command. Type 'help' for a list of available commands.");
        response[strlen(response)] = '\0';
        write(clientResponseFifo, &response, strlen(response));
        return 0;
    }
}

// Function to handle client requests
int handleClientRequests(int client_fifo, int client_number, int clientResponseFifo, char* currentPath){
    // Handle client requests
    char buffer[MAX_BUFFER];

    read(client_fifo, buffer, MAX_BUFFER);
    // Log client activity to file

    int log_file = open("client_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(log_file != -1) 
    {
        dprintf(log_file, "\"client%02d\": %s\n", client_number, buffer);
        close(log_file);
    }

    int check = process(buffer,clientResponseFifo,currentPath);
    if(check == 1)
    {
        return 1; 
    }
    else if(check == 2)
    {
        close(client_fifo);
        return 2;
    }
    
    return 0;
}