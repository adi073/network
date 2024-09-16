#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#define MAX_EVENTS 64

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int make_socket_non_blocking(int sfd) {
    int flags, s;

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        perror("fcntl");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int listener, s;
    struct sockaddr_in address;
    int portno;
    struct epoll_event event;
    struct epoll_event *events;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == -1) {
        error("socket");
    }

    s = make_socket_non_blocking(listener);
    if (s == -1) {
        abort();
    }

    portno = atoi(argv[1]);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(portno);

    s = bind(listener, (struct sockaddr *)&address, sizeof(address));
    if (s == -1) {
        error("bind");
    }

    s = listen(listener, SOMAXCONN);
    if (s == -1) {
        error("listen");
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        error("epoll_create");
    }

    event.data.fd = listener;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener, &event);
    if (s == -1) {
        error("epoll_ctl");
    }

    events = calloc(MAX_EVENTS, sizeof(event));

    while (1) {
        int n, i;

        n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                continue;
            } else if (listener == events[i].data.fd) {
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof(in_addr);
                    infd = accept(listener, &in_addr, &in_len);
                    if (infd == -1) {
                        if ((errno == EAGAIN) ||
                            (errno == EWOULDBLOCK)) {
                            break;
                        } else {
                            perror("accept");
                            break;
                        }
                    }

                    s = make_socket_non_blocking(infd);
                    if (s == -1) {
                        abort();
                    }

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1) {
                        error("epoll_ctl");
                    }
                }
                continue;
            } else {
                int done = 0;

                while (1) {
                    ssize_t count;
                    char buf[512];

                    count = read(events[i].data.fd, buf, sizeof(buf));
                    if (count == -1) {
                        if (errno != EAGAIN) {
                            perror("read");
                            done = 1;
                        }
                        break;
                    } else if (count == 0) {
                        done = 1;
                        break;
                    }

                    s = write(events[i].data.fd, buf, count);
                    if (s == -1) {
                        perror("write");
                        done = 1;
                        break;
                    }
                }

                if (done) {
                    printf("Closed connection on descriptor %d\n",
                           events[i].data.fd);

                    close(events[i].data.fd);
                }
            }
        }
    }

    free(events);
    close(listener);

    return EXIT_SUCCESS;
}
