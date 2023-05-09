#ifndef MYDUBS_H
#define MYDUBS_H
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

//-------dup() funtion implementation-------//
int my_dup(int oldfd){

    //return file descriptor
    int newfd; 

    //control is oldfd is valid or not
    if(fcntl(oldfd , F_GETFL) == -1)
    {
        perror("oldfd is not valid");
        errno = EBADF;
        return -1;
    }

    //duplication
    newfd = fcntl(oldfd , F_DUPFD , 0);

    //success control
    if(newfd == -1)
    {
        perror("Error occured while duplication");
        return -1;
    }
    return newfd;
}

//-------dup2() funtion implementation-------//
int my_dup2(int oldfd, int newfd){

    //control is oldfd is valid
    if(fcntl(oldfd , F_GETFL) == -1)
    {
        perror("oldfd is not valid");
        errno = EBADF;
        return -1;
    }

    //control oldfd and newfd equal or not
    if(oldfd == newfd)
        return newfd;

    //control newfd is open or not
    if(fcntl(newfd , F_GETFL) != -1)
    {
        //if is open close it
        close(newfd);

        //control error while opening
        if(fcntl(newfd , F_GETFL) != -1)
        {
            perror("Error occured while closing");
            exit(1);
        }
    }

    //return duplicated value
    return fcntl(oldfd , F_DUPFD , newfd);
}

#endif // MY_DUBS