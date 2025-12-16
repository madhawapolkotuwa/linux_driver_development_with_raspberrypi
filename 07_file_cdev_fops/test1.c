#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[]){

    if(argc < 0){
        printf("I need the file to open an argument!\n");
        return 0;
    }
    
    int fd;
    int ret;
    /* ***************************************** */
    fd = open(argv[1], O_RDONLY);
    if(fd < 0){
        perror("Failed to open 0\n");
        return fd;
    }
    printf("%s: file opend 0\n", argv[1]);

    ret = close(fd);
    if(ret < 0){
        perror("Failed to close 0\n");
        return ret;
    }
    printf("%s: file closed 0\n", argv[1]);

    /* ***************************************** */
    fd = open(argv[1], O_WRONLY);
    if(fd < 0){
        perror("Failed to open 1\n");
        return fd;
    }
    printf("%s: file opend 1\n", argv[1]);

    ret = close(fd);
    if(ret < 0){
        perror("Failed to close 1\n");
        return ret;
    }
    printf("%s: file closed 1\n", argv[1]);

    /* ***************************************** */
    fd = open(argv[1], O_RDWR);
    if(fd < 0){
        perror("Failed to open 2\n");
        return fd;
    }
    printf("%s: file opend 2\n", argv[1]);

    ret = close(fd);
    if(ret < 0){
        perror("Failed to close 2\n");
        return ret;
    }
    printf("%s: file closed 2\n", argv[1]);

    /* ***************************************** */
    fd = open(argv[1], O_RDWR | O_SYNC);
    if(fd < 0){
        perror("Failed to open 3\n");
        return fd;
    }
    printf("%s: file opend 3\n", argv[1]);

    ret = close(fd);
    if(ret < 0){
        perror("Failed to close 3\n");
        return ret;
    }
    printf("%s: file closed 3\n", argv[1]);

    return 0;
}