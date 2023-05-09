#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define WITHOUT_X_FLASG (O_WRONLY | O_CREAT | O_APPEND) //FLAGS FOR WITHOUT X COMMAND
#define WITH_X_FLASG    (O_WRONLY | O_CREAT)            //FLAGS FOR WITH X COMMAND

int main(int argc, char const *argv[])
{   
    //command line input control
    if(argc<3 || argc>4)
    {   
        perror("Command input is not valid");
        exit(1);
    }


    int fd;                       //file descriptor
    int i;                        //integer for 'for' loop
    int numbytes = atoi(argv[2]); //number of bytes to write
    char byte = ' ';              //byte to write
    
    //WITHOUT X COMMAND 
    if(argc == 3)
    {
        //open file without x flags (they contain O_APPEND)
        fd = open(argv[1] , WITHOUT_X_FLASG , 0644);

        //control error in opening
        if(fd == -1)
        {
            perror("Error occured while opening");
            exit(1);
        }

        //writing
        for(i = 0 ; i < numbytes ; i++)
        {
            write(fd , "" , 1);
        }
    }

    //WITH X COMMAND
    else if(argc == 4)
    {
        //control command input contain 'x'
        if(strcmp(argv[3],"x"))
        {
            perror("Command input is not valid");
            exit(1);
        }

        //open file
        fd = open(argv[1] , WITH_X_FLASG , 0644);

        //control error in opening
        if(fd == -1)
        {
            perror("Error occured while opening");
            exit(1);
        }
        
        //writing
        for(i = 0 ; i < numbytes ; i++)
        {
            lseek(fd , 0 , SEEK_END);
            write(fd , "", 1);
        }
    }

    //close the file
    close(fd);
    return 0;
}
