#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>

#define PORT 8080
#define MAX_EVENTS 64
#define BUFFER_SIZE 1024

// Set a file descriptor to non-blocking mode (Required for Edge-Triggered mode)
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) { perror("socket failed"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen failed"); exit(EXIT_FAILURE);
    }
    set_nonblocking(listen_fd);

    // 1. Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { perror("epoll_create1 failed"); exit(EXIT_FAILURE); }

    // 2. Register listening socket to epoll
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN | EPOLLET; // Read event + Edge-Triggered
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        perror("epoll_ctl listen_fd failed"); exit(EXIT_FAILURE);
    }

    printf("Epoll TCP Server running on port %d...\n", PORT);

    // 3. Event Loop
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue; // Interrupted by system signal
            perror("epoll_wait failed"); break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listen_fd) {
                // Handle new incoming connections (Loop needed for ET mode)
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break; // Processed all pending connections
                        perror("accept failed"); break;
                    }

                    set_nonblocking(client_fd);
                    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT; // EPOLLONESHOT avoids multithreading race conditions
                    ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            } else {
                // Handle data from existing clients
                int client_fd = events[i].data.fd;
                char buf[BUFFER_SIZE];
                int close_conn = 0;

                // Loop read because of ET mode
                while (1) {
                    ssize_t count = read(client_fd, buf, sizeof(buf));
                    if (count == -1) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("read error"); close_conn = 1;
                        }
                        break; // Data fully read for this turn
                    } else if (count == 0) {
                        close_conn = 1; // Client disconnected
                        break;
                    }

                    // Echo back the received data
                    write(client_fd, buf, count);
                }

                if (close_conn) {
                    printf("Client disconnected (FD: %d)\n", client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                } else {
                    // Re-arm the event if you used EPOLLONESHOT
                    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev);
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    return 0;
}