#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>

#define TIMEOUT_SEC 5
#define BUF_LEN 1024

int main(void) {
    char buf[BUF_LEN + 1];
    fd_set readfds;
    struct timeval timeout;
    int ret;
    ssize_t n;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;

    ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);

    if (ret < 0) {
        if (errno == EINTR) {
            fprintf(stderr, "select was interrupted\n");
            return 1;
        }

        perror("select");
        return 1;
    }

    if (ret == 0) {
        printf("timeout\n");
        return 0;
    }

    if (FD_ISSET(STDIN_FILENO, &readfds)) {
        n = read(STDIN_FILENO, buf, BUF_LEN);

        if (n < 0) {
            perror("read");
            return 1;
        }

        buf[n] = '\0';
        printf("read: %s", buf);
    }

    return 0;
}