#include "tsa_event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

#define MAX_EVENTS 1024

struct tsa_reactor_s {
    int epoll_fd;
    bool running;
    int event_count;
};

tsa_reactor_t* tsa_reactor_create(void) {
    tsa_reactor_t* reactor = calloc(1, sizeof(tsa_reactor_t));
    if (!reactor) return NULL;

    reactor->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (reactor->epoll_fd < 0) {
        free(reactor);
        return NULL;
    }
    return reactor;
}

void tsa_reactor_destroy(tsa_reactor_t* reactor) {
    if (!reactor) return;
    if (reactor->epoll_fd >= 0) close(reactor->epoll_fd);
    free(reactor);
}

void tsa_reactor_stop(tsa_reactor_t* reactor) {
    if (reactor) reactor->running = false;
}

static uint32_t get_epoll_events(tsa_reactor_event_t* event) {
    uint32_t ev = 0;
    if (event->on_read) ev |= EPOLLIN;
    if (event->on_write) ev |= EPOLLOUT;
    return ev;
}

int tsa_reactor_add(tsa_reactor_t* reactor, tsa_reactor_event_t* event) {
    if (!reactor || !event || reactor->epoll_fd < 0) return -1;

    struct epoll_event ev = {0};
    ev.events = get_epoll_events(event);
    ev.data.ptr = event;

    if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, event->fd, &ev) < 0) {
        return -1;
    }
    reactor->event_count++;
    return 0;
}

int tsa_reactor_del(tsa_reactor_t* reactor, tsa_reactor_event_t* event) {
    if (!reactor || !event || reactor->epoll_fd < 0) return -1;

    if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_DEL, event->fd, NULL) < 0) {
        return -1;
    }
    reactor->event_count--;
    return 0;
}

int tsa_reactor_mod(tsa_reactor_t* reactor, tsa_reactor_event_t* event) {
    if (!reactor || !event || reactor->epoll_fd < 0) return -1;

    struct epoll_event ev = {0};
    ev.events = get_epoll_events(event);
    ev.data.ptr = event;

    if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_MOD, event->fd, &ev) < 0) {
        return -1;
    }
    return 0;
}

void tsa_reactor_run(tsa_reactor_t* reactor) {
    if (!reactor || reactor->epoll_fd < 0) return;

    struct epoll_event events[MAX_EVENTS];
    reactor->running = true;

    while (reactor->running && reactor->event_count > 0) {
        int n = epoll_wait(reactor->epoll_fd, events, MAX_EVENTS, 100);
        if (n < 0) {
            if (errno == EINTR) continue;
            break; // Error
        }

        for (int i = 0; i < n; i++) {
            tsa_reactor_event_t* event = (tsa_reactor_event_t*)events[i].data.ptr;
            uint32_t ev = events[i].events;

            if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                if (event->on_error) event->on_error(event->arg);
            } else {
                if ((ev & EPOLLIN) && event->on_read) {
                    event->on_read(event->arg);
                }
                if ((ev & EPOLLOUT) && event->on_write) {
                    event->on_write(event->arg);
                }
            }
        }
    }
}