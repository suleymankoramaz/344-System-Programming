#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

#define MAX_FILENAME_LENGTH 256
#define MAX_PATH_LENGTH 4096

//structure to hold file information
typedef struct 
{
    int src_fd;                                //source file descriptor
    int dest_fd;                               //destination file descriptor
    char src_filename[MAX_FILENAME_LENGTH];    //source filename
    char dest_filename[MAX_FILENAME_LENGTH];   //destination filename
} FileData;

//global variables
FileData* buffer;              //buffer for file information
int buffer_count = 0;          //number of items in the buffer
int buffer_in = 0;             //index to put the next item in the buffer
int buffer_out = 0;            //index to take the next item from the buffer
int done = 0;                  //flag indicating if producer has finished
int buffer_size = 0;           //buffer size
int num_files_copied = 0;      //number of files copied
int num_dirs_copied = 0;       //number of directories copied
off_t total_bytes_copied = 0;  //total bytes copied

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;            //mutex for buffer access
pthread_cond_t buffer_not_full_cond = PTHREAD_COND_INITIALIZER;      //condition variable for buffer not full
pthread_cond_t buffer_not_empty_cond = PTHREAD_COND_INITIALIZER;     //condition variable for buffer not empty


void cleanup() 
{
    //free allocated memory
    free(buffer);

    //close open file descriptors
    for(int i = 0; i < buffer_size; i++) 
    {
        if(buffer[i].src_fd != -1) 
        {
            close(buffer[i].src_fd);
        }
        if(buffer[i].dest_fd != -1) 
        {
            close(buffer[i].dest_fd);
        }
    }
}

void handle_sigint(int signum) 
{
    printf("\nCtrl+C received. Cleaning up and exiting.\n");
    cleanup();
    exit(signum);
}

void handle_sigtstp(int signum) 
{
    printf("\nCtrl+Z received. Cleaning up and exiting.\n");
    cleanup();
    exit(signum);
}

void* copyFile(void* arg) 
{
    sleep(5);
    char** paths = (char**)arg;
    char* src_filename = paths[0];
    char* dest_filename = paths[1];

    int src_fd2 = open(src_filename, O_RDONLY);
    if(src_fd2 == -1) 
    {
        perror("Failed to open source file");
    }

    int dest_fd = open(dest_filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(dest_fd == -1) 
    {
        perror("Failed to open destination file");
        close(src_fd2);
    }

    char buffer[4096];
    ssize_t bytes_read, bytes_written;

    while((bytes_read = read(src_fd2, buffer, sizeof(buffer))) > 0) 
    {
        bytes_written = write(dest_fd, buffer, bytes_read);
        total_bytes_copied += bytes_written;
        if(bytes_written == -1) 
        {
            perror("Failed to write to destination file");
            break;
        }
        
    }

    if(bytes_read == -1) 
    {
        perror("Failed to read from source file");
    }

    close(src_fd2);
    close(dest_fd);

    //print completion status
    if(bytes_read == 0)
    {
        printf("Copied: %s\n", dest_filename);
        num_files_copied++;
    } 
    else 
    {
        printf("Failed to copy: %s\n", dest_filename);
    }
}

void* producer(void* arg) 
{
    char** directories = (char**)arg;
    char* src_dir = directories[0];
    char* dest_dir = directories[1];
    printf("PRODUCER:  %s - %s\n", src_dir , dest_dir);

    DIR* dir = opendir(src_dir);
    if(dir == NULL) 
    {
        perror("Failed to open source directory");
        return NULL;
    }

    struct dirent* entry;
    struct stat st;
    char src_path[MAX_PATH_LENGTH];
    char dest_path[MAX_PATH_LENGTH];
    

    while((entry = readdir(dir)) != NULL) 
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
        {
            continue;  //skip current and parent directories
        }

        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry->d_name);

        if(stat(src_path, &st) == -1) 
        {
            perror("Failed to get file information");
            continue;
        }

        if(S_ISDIR(st.st_mode)) 
        {
            //recursively copy subdirectories
            num_dirs_copied++;
            mkdir(dest_path, 0777);
            char* subdirectories[] = {src_path, dest_path};
            producer(subdirectories);  //recursive call

        } 
        else 
        {
            pthread_mutex_lock(&buffer_mutex);
            //copy regular files

            while(buffer_count == buffer_size) 
            {
                pthread_cond_wait(&buffer_not_full_cond, &buffer_mutex);
            }

            buffer[buffer_in].src_fd = -1;
            buffer[buffer_in].dest_fd = -1;
            strcpy(buffer[buffer_in].src_filename, src_path);
            strcpy(buffer[buffer_in].dest_filename, dest_path);
            buffer_in = (buffer_in + 1) % buffer_size;
            buffer_count++;

            pthread_mutex_unlock(&buffer_mutex);
        }
    }

    closedir(dir);
    done = 1;
    pthread_cond_broadcast(&buffer_not_empty_cond);  //signal all consumers
    

    return NULL;
}

void* consumer(void* arg) 
{
    while(1) 
    {
        pthread_mutex_lock(&buffer_mutex);

        while(buffer_count == 0 && !done) 
        {
            pthread_cond_wait(&buffer_not_empty_cond, &buffer_mutex);
        }

        if(buffer_count == 0 && done) 
        {
            pthread_mutex_unlock(&buffer_mutex);
            return NULL;
        }

        int src_fd = buffer[buffer_out].src_fd;
        int dest_fd = buffer[buffer_out].dest_fd;
        char src_filename[MAX_PATH_LENGTH];
        char dest_filename[MAX_PATH_LENGTH];
        strcpy(src_filename, buffer[buffer_out].src_filename);
        strcpy(dest_filename, buffer[buffer_out].dest_filename);
        buffer_out = (buffer_out + 1) % buffer_size;
        buffer_count--;

        pthread_cond_signal(&buffer_not_full_cond);
        pthread_mutex_unlock(&buffer_mutex);

        if(src_fd == -1 && dest_fd == -1) 
        {
            pthread_t copy_thread;
            char** files = malloc(2 * sizeof(char*));
            files[0] = src_filename;
            files[1] = dest_filename;
            pthread_create(&copy_thread, NULL, copyFile, files);

            //wait for copy thread to complete
            pthread_join(copy_thread, NULL);
        } 
        else 
        {
            //copy files from the buffer
            int src_file = dup(src_fd);
            int dest_file = dup(dest_fd);
            off_t file_size = lseek(src_file, 0, SEEK_END);
            lseek(src_file, 0, SEEK_SET);

            char buffer[4096];
            ssize_t bytes_read, bytes_written;

            while((bytes_read = read(src_file, buffer, sizeof(buffer))) > 0) 
            {
                bytes_written = write(dest_file, buffer, bytes_read);
                if(bytes_written == -1) 
                {
                    perror("Failed to write to destination file");
                    break;
                }
            }

            close(src_file);
            close(dest_file);

            //print completion status
            if(bytes_read == 0 && file_size > 0) 
            {
                printf("Copied: %s\n", dest_filename);
                num_files_copied++;
                total_bytes_copied += file_size;
            } 
            else 
            {
                printf("Failed to copy: %s\n", dest_filename);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    
    if(argc < 4) 
    {
        printf("Usage: %s <buffer_size> <num_consumers> <src_dir> <dest_dir>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, handle_sigtstp);

    buffer_size = atoi(argv[1]);
    int num_consumers = atoi(argv[2]);
    char* src_dir = argv[3];
    char* dest_dir = argv[4];

    //set buffer size
    if(buffer_size <= 0) 
    {
        printf("Invalid buffer size. Using default size: 10\n");
        buffer_size = 10;
    }

    buffer = malloc(buffer_size * sizeof(FileData));

    // Set number of consumers
    if(num_consumers <= 0) 
    {
        printf("Invalid number of consumers. Using default: 1\n");
        num_consumers = 1;
    }

    //allocate memory for directory arguments
    char** directories = malloc(2 * sizeof(char*));
    directories[0] = src_dir;
    directories[1] = dest_dir;

    //create consumer threads
    pthread_t consumers[num_consumers];
    for(int i = 0; i < num_consumers; i++) 
    {
        pthread_create(&consumers[i], NULL, consumer, NULL);
    }

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    //create producer thread
    pthread_t producer_thread;
    pthread_create(&producer_thread, NULL, producer, directories);

    //wait for producer thread to complete
    pthread_join(producer_thread, NULL);

    //wait for consumer threads to complete
    for(int i = 0; i < num_consumers; i++) 
    {
        pthread_join(consumers[i], NULL);
    }

    gettimeofday(&end_time, NULL);
    double execution_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    //display statistics
    printf("Number of files copied: %d\n", num_files_copied);
    printf("Number of directories copied: %d\n", num_dirs_copied);
    printf("Total bytes copied: %ld\n", total_bytes_copied);
    printf("Total execution time: %.2f seconds\n", execution_time);

    free(directories);

    return 0;
    
}