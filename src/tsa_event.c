#include "tsa_event.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

#define MAX_EVENTS 1024

struct tsa_event_loop_s {
    int epoll_fd;
    bool running;
    int event_count;
};

tsa_event_loop_t* tsa_event_loop_create(void) {
    tsa_event_loop_t* loop = calloc(1, sizeof(tsa_event_loop_t));
    if (!loop) return NULL;
    
    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epoll_fd < 0) {
        free(loop);
        return NULL;
    }
    return loop;
}

void tsa_event_loop_destroy(tsa_event_loop_t* loop) {
    if (!loop) return;
    if (loop->epoll_fd >= 0) close(loop->epoll_fd);
    free(loop);
}

void tsa_event_loop_stop(tsa_event_loop_t* loop) {
    if (loop) loop->running = false;
}

static uint32_t get_epoll_events(tsa_event_t* event) {
    uint32_t ev = 0;
    if (event->on_read) ev |= EPOLLIN;
    if (event->on_write) ev |= EPOLLOUT;
    return ev;
}

int tsa_event_add(tsa_event_loop_t* loop, tsa_event_t* event) {
    if (!loop || !event || loop->epoll_fd < 0) return -1;
    
    struct epoll_event ev = {0};
    ev.events = get_epoll_events(event);
    ev.data.ptr = event;
    
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, event->fd, &ev) < 0) {
        return -1;
    }
    loop->event_count++;
    return 0;
}

int tsa_event_del(tsa_event_loop_t* loop, tsa_event_t* event) {
    if (!loop || !event || loop->epoll_fd < 0) return -1;
    
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, event->fd, NULL) < 0) {
        return -1;
    }
    loop->event_count--;
    return 0;
}

int tsa_event_mod(tsa_event_loop_t* loop, tsa_event_t* event) {
    if (!loop || !event || loop->epoll_fd < 0) return -1;
    
    struct epoll_event ev = {0};
    ev.events = get_epoll_events(event);
    ev.data.ptr = event;
    
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, event->fd, &ev) < 0) {
        return -1;
    }
    return 0;
}

void tsa_event_loop_run(tsa_event_loop_t* loop) {
    if (!loop || loop->epoll_fd < 0) return;
    
    struct epoll_event events[MAX_EVENTS];
    loop->running = true;
    
    while (loop->running && loop->event_count > 0) {
        int n = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break; // Error
        }
        
        for (int i = 0; i < n; i++) {
            tsa_event_t* event = (tsa_event_t*)events[i].data.ptr;
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