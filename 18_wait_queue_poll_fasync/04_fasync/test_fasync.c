#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

static int fd;

static void sigio_handler(int sig){

    char buffer[64];

    int n = read(fd, buffer, sizeof(buffer) - 1);
    if(n > 0){
        buffer[n] = '\0';
        printf("\n[SIgIO] Button pressed - Received: %s", buffer);
        fflush(stdout);
    }
}

int main(int argc, char *argv[])
{
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        return 1;
    }

    /* Step 1: Install the SIGIO signal handler */
    signal(SIGIO, sigio_handler);

    /* Step 2: Tell the kernel which process should receive SIGIO for this fd */
    fcntl(fd, F_SETOWN, getpid());

    /* Step 3: Enable async notification by setting O_ASYNC on the fd */
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_ASYNC);

    printf("Async notification enabled. Press the button at any time.\n");
    printf("Process is running freely - not blocked.\n\n");

    int counter = 0;
    while(1){
        printf("Working... %d\r", counter++);
        fflush(stdout);
        sleep(1);  /* In a real app this would be actual work, not sleep */
    }

    close(fd);

    return 0;
}