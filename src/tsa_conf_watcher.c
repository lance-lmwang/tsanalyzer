#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <pthread.h>
#include <errno.h>

#include "tsa_conf.h"
#include "tsa_log.h"

#define TAG "CONF_WATCHER"

typedef struct {
    char filename[256];
    tsa_full_conf_t* conf;
    pthread_t thread;
    volatile bool running;
} conf_watcher_t;

static void* watcher_thread(void* arg) {
    conf_watcher_t* w = (conf_watcher_t*)arg;
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        tsa_error(TAG, "Failed to initialize inotify");
        return NULL;
    }

    int wd = inotify_add_watch(fd, w->filename, IN_MODIFY | IN_CLOSE_WRITE);
    if (wd < 0) {
        tsa_error(TAG, "Failed to add watch for %s: %s", w->filename, strerror(errno));
        close(fd);
        return NULL;
    }

    tsa_info(TAG, "Watching configuration file: %s", w->filename);

    while (w->running) {
        struct inotify_event event;
        ssize_t len = read(fd, &event, sizeof(event));
        if (len > 0) {
            tsa_info(TAG, "Configuration change detected, reloading...");

            tsa_full_conf_t new_conf;
            memset(&new_conf, 0, sizeof(new_conf));
            if (tsa_conf_load(&new_conf, w->filename) == 0) {
                /* Atomic-ish swap: in a real product we would reconcile streams here.
                 * For now, we just log success. Full reconciliation is Phase 2. */
                tsa_info(TAG, "Configuration reloaded successfully. (Reconciliation pending implementation)");
            } else {
                tsa_error(TAG, "Failed to reload configuration");
            }
        }
        usleep(500000); // 500ms poll
    }

    inotify_rm_watch(fd, wd);
    close(fd);
    return NULL;
}

void tsa_conf_watcher_start(tsa_full_conf_t* conf, const char* filename) {
    conf_watcher_t* w = calloc(1, sizeof(conf_watcher_t));
    strncpy(w->filename, filename, sizeof(w->filename) - 1);
    w->conf = conf;
    w->running = true;
    pthread_create(&w->thread, NULL, watcher_thread, w);
    /* Note: In a real app, we'd store the watcher handle for cleanup */
}
