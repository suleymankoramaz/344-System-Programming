#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/sem.h>


#define MAX_BUFFER 1024
#define MAX_FILENAME 256

void handleSignal(int);
void handleUserInput(int, int, int);

int main(int argc, char *argv[]) {
    // Parse command-line arguments (connection type, server PID)
    if(argc != 3) 
    {
        printf("Usage: biboClient <Connect/tryConnect> ServerPID\n");
        return 1;
    }

    char *connection_type = argv[1];
    int server_pid = atoi(argv[2]);

    // Set up signal handlers for graceful termination
    signal(SIGTERM, handleSignal);
    signal(SIGINT, handleSignal);

    // Connect to the server using FIFO
    char client_fifo_name[MAX_FILENAME];
    snprintf(client_fifo_name, MAX_FILENAME, "/tmp/biboClient_%d", server_pid);

    char client_fifo_response_name[MAX_FILENAME];
    snprintf(client_fifo_response_name, MAX_FILENAME, "/tmp/biboClientResponse_%d", getpid());
    
    mkfifo(client_fifo_name, 0666);
    mkfifo(client_fifo_response_name, 0666);

    char server_fifo_name[MAX_FILENAME];
    snprintf(server_fifo_name, MAX_FILENAME, "/tmp/biboServer_%d", server_pid);

    // Send client PID to the server
    printf(">> Waiting for Queue.. ");

    int server_fifo = open(server_fifo_name, O_WRONLY);
    if(server_fifo == -1) 
    {
        perror("Error opening server FIFO");
        unlink(client_fifo_name);
        return 1;
    }

    fflush(stdout);

    int client_pid = getpid();
    write(server_fifo, &client_pid, sizeof(client_pid));

    key_t key = ftok("/tmp", 'S');
    int semaphore = semget(key, 1, IPC_CREAT | 0666);
    if (semaphore == -1) {
        perror("Error creating semaphore");
        return 1;
    }

    int client_fifo = open(client_fifo_name, O_WRONLY);
    if(client_fifo == -1) 
    {
        perror("Error opening client FIFO");
        return 1;
    }

    int clientResponseFifo = open(client_fifo_response_name,O_RDONLY);
    if(clientResponseFifo == -1) 
    {
        perror("Error opening clientResponse FIFO");
    }
     

    // Enter the main client loop
    printf("Connection established:\n");
    handleUserInput(client_fifo,server_fifo,clientResponseFifo);

    // Clean up resources and exit gracefully
    close(server_fifo);
    unlink(client_fifo_name);
    return 0;
}

// Signal handler for graceful termination
void handleSignal(int signal) {
    // Handle the signal and terminate gracefully
    printf(">> bye..\n");
    exit(0);
}

// Function to handle user input and send requests to the server
void handleUserInput(int clientFifo, int serverFifo, int clientResponse) {
    // Handle user input and send requests to the server
    char buffer[MAX_BUFFER-8];
    char formatted_buffer[MAX_BUFFER];
    char response[MAX_BUFFER];

    while(1) 
    {
        printf(">> Enter command: ");
        fgets(buffer, MAX_BUFFER-8, stdin);
        buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline character
        snprintf(formatted_buffer, sizeof(formatted_buffer) , "%d %s",getpid(),buffer);

        // Send command to the server
        int bytes_written = write(clientFifo, formatted_buffer, MAX_BUFFER);
        if(bytes_written == -1) 
        {
            perror("Error writing to client FIFO");
            break;
        } 
        else if(bytes_written < MAX_BUFFER) 
        {
            printf(">> Warning: Only %d bytes written to client FIFO\n", bytes_written);
        } 

        // Read response from the server
        int bytes_read = read(clientResponse, response, MAX_BUFFER);
        if(bytes_read == -1) 
        {
            perror("Error reading from client response FIFO");
            break;
        } 
        else if(bytes_read == 0) 
        {
            printf(">> Server closed the connection\n");
            break;
        } 
        else 
        {
            printf("%s\n", response);
        }
        if(strncmp(response, "quit", 4) == 0)
            handleSignal(SIGINT);
    }

    // Close the FIFO after the server has read the data
    close(clientFifo);
}