#ifndef TSA_EVENT_H
#define TSA_EVENT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct tsa_reactor_s tsa_reactor_t;
typedef struct tsa_reactor_event_s tsa_reactor_event_t;

typedef void (*tsa_reactor_cb_t)(void* arg);

struct tsa_reactor_event_s {
    int fd;
    tsa_reactor_cb_t on_read;
    tsa_reactor_cb_t on_write;
    tsa_reactor_cb_t on_error;
    void* arg;
};

tsa_reactor_t* tsa_reactor_create(void);
void tsa_reactor_destroy(tsa_reactor_t* reactor);

// Run the reactor. Returns if there are no more active events or stop is requested.
void tsa_reactor_run(tsa_reactor_t* reactor);
void tsa_reactor_stop(tsa_reactor_t* reactor);

// Add an event to the reactor. Returns 0 on success.
int tsa_reactor_add(tsa_reactor_t* reactor, tsa_reactor_event_t* event);
// Remove an event from the reactor.
int tsa_reactor_del(tsa_reactor_t* reactor, tsa_reactor_event_t* event);

// Update event subscriptions (e.g. if you added on_write callback dynamically).
int tsa_reactor_mod(tsa_reactor_t* reactor, tsa_reactor_event_t* event);

#endif  // TSA_EVENT_H