// test_poll_multi.c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device>\n", argv[0]);
        return 1;
    }

    /* Open the same device twice - two independent file descriptors */
    int fd1 = open(argv[1], O_RDONLY);
    int fd2 = open(argv[1], O_RDONLY);

    if (fd1 < 0 || fd2 < 0) {
        perror("open failed");
        return 1;
    }

    /* Monitor three fds simultaneously */
    struct pollfd fds[3] = {
        { .fd = STDIN_FILENO, .events = POLLIN },  /* fds[0]: stdin keybod input   */
        { .fd = fd1,          .events = POLLIN },  /* fds[1]: device instance 1 */
        { .fd = fd2,          .events = POLLIN },  /* fds[2]: device instance 2 */
    };

    char buffer[64];

    printf("Monitoring stdin + 2x /dev/my_cdev0\n");
    printf("Press the button OR type something and press Enter\n\n");

    while (1) {

        /* Single poll() call monitors all three fds at once */
        int ret = poll(fds, 3, -1);
        if (ret < 0) {
            perror("poll failed");
            break;
        }

        /* Check each fd independently */
        if (fds[0].revents & POLLIN) {
            int n = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                printf("[stdin]  Typed: %s", buffer);
            }
        }

        if (fds[1].revents & POLLIN) {
            int n = read(fd1, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                printf("[fd1]    Received: %s \n", buffer);
            }
        }

        if (fds[2].revents & POLLIN) {
            int n = read(fd2, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                printf("[fd2]    Received: %s \n", buffer);
            }
        }
    }

    close(fd1);
    close(fd2);
    return 0;
}
