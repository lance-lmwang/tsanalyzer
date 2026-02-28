#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9001);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        return 1;
    }

    printf("Waiting for packets on port 9001...\n");
    uint8_t buf[2048];
    for (int i = 0; i < 10; i++) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            printf("Received %zd bytes\n", n);
        } else {
            perror("recv");
        }
    }
    close(fd);
    return 0;
}
