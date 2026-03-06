#ifndef TSA_EVENT_H
#define TSA_EVENT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct tsa_event_loop_s tsa_event_loop_t;
typedef struct tsa_event_s tsa_event_t;

typedef void (*tsa_event_cb_t)(void* arg);

struct tsa_event_s {
    int fd;
    tsa_event_cb_t on_read;
    tsa_event_cb_t on_write;
    tsa_event_cb_t on_error;
    void* arg;
};

tsa_event_loop_t* tsa_event_loop_create(void);
void tsa_event_loop_destroy(tsa_event_loop_t* loop);

// Run the event loop. Returns if there are no more active events or stop is requested.
void tsa_event_loop_run(tsa_event_loop_t* loop);
void tsa_event_loop_stop(tsa_event_loop_t* loop);

// Add an event to the loop. Returns 0 on success.
int tsa_event_add(tsa_event_loop_t* loop, tsa_event_t* event);
// Remove an event from the loop.
int tsa_event_del(tsa_event_loop_t* loop, tsa_event_t* event);

// Update event subscriptions (e.g. if you added on_write callback dynamically).
int tsa_event_mod(tsa_event_loop_t* loop, tsa_event_t* event);

#endif // TSA_EVENT_H