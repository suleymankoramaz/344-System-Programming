#include "mydubs.h"
#include <stdio.h>
#include <string.h>

#define FLAGS (O_WRONLY | O_CREAT) //OPEN FILE FLAGS

int main(int argc, char const *argv[])
{
    //----------FILE DESCRIPTORS-----------//
    int fd1 , fd2 , fd3 , fd4 , fd5;

    // STRINGS FOR WRITING TO THE FILES
    char *c1 = "fd1 wrote that\n";
    char *c2 = "fd2 wrote that\n";
    char *c3 = "fd3 wrote that\n";
    char *c4 = "fd4 wrote that\n";
    char *c5 = "fd5 wrote that\n";

    //-----------OPENING FILES-----------//

    if((fd1 = open("test1.txt",FLAGS,0666)) == -1)
    {
        perror("Error occured while opening");
        exit(1);
    }
    if((fd2 = open("test2.txt",FLAGS,0666)) == -1)
    {
        perror("Error occured while opening");
        exit(1);
    }
    if((fd3 = open("test3.txt",FLAGS,0666)) == -1)
    {
        perror("Error occured while opening");
        exit(1);
    }
    if((fd4 = open("test4.txt",FLAGS,0666)) == -1)
    {
        perror("Error occured while opening");
        exit(1);
    }

    //-----------TEST CASES-----------//
    printf("\nBefore the duplications:\n");
    printf("fd1 is = %d , fd2 is = %d\n",fd1,fd2);
    printf("fd3 is = %d , fd4 is = %d\n",fd3,fd4);

    printf("\n fd1 and fd2 wrote 1 line. \n");
    write(fd1 , c1 , strlen(c1));
    write(fd2 , c2 , strlen(c2));

    fd2 = my_dup(fd1);

    printf("\n fd1 and fd2 wrote 1 line. \n");
    write(fd1 , c1 , strlen(c1));
    write(fd2 , c2 , strlen(c2));

    printf("\n fd3 and fd4 wrote 1 line. \n");
    write(fd3 , c3 , strlen(c3));
    write(fd4 , c4 , strlen(c4));

    fd4 = my_dup2(fd3,10);

    printf("\n fd3 and fd4 wrote 1 line. \n");
    write(fd3 , c3 , strlen(c3));
    write(fd4 , c4 , strlen(c4));

    printf("\nAfter the duplications:\n");
    printf("fd1 is = %d , fd2 is = %d\n",fd1,fd2);
    printf("fd3 is = %d , fd4 is = %d\n",fd3,fd4);

    printf("\nBefore duplication fd5 is = %d\n",fd5);
    fd5 = my_dup2(fd3,fd1);
    printf("\nAfter duplicationfd5 is = %d\n",fd5);

    printf("\n fd5 wrote 1 line, fd1 wrote 1 line to the test3.txt \n");
    write(fd1 , c1 , strlen(c1));
    write(fd5 , c5 , strlen(c5));

    printf("\n\n NOW YOU CAN CONTROL TEST.TXT FILES, THANK YOU\n\n");

    return 0;
}