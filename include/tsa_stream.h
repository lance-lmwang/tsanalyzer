#ifndef TSA_STREAM_H
#define TSA_STREAM_H

#include <stdint.h>
#include <stdbool.h>

#define TSA_MAX_PID 8192
#define TSA_MAX_STREAM_CHILDREN 16

typedef struct tsa_stream_s tsa_stream_t;

struct tsa_stream_s {
    void* self;               // Pointer to the owner module/plugin context
    tsa_stream_t* parent;     // Upstream parent

    // Stream callback for processing 188-byte TS packets
    void (*on_ts)(void* self, const uint8_t* ts);

    // List of child stream nodes (subscribers)
    tsa_stream_t* children[TSA_MAX_STREAM_CHILDREN];
    int child_count;

    // Demux PID filtering (Reactive PID Management)
    void (*join_pid)(void* self, uint16_t pid);
    void (*leave_pid)(void* self, uint16_t pid);

    // PID subscription counters (0 means drop, >0 means keep)
    uint16_t pid_list[TSA_MAX_PID];
};

// Stream Core APIs
void tsa_stream_init(tsa_stream_t* stream, void* self_ctx, void (*on_ts_cb)(void*, const uint8_t*));
void tsa_stream_destroy(tsa_stream_t* stream);
int tsa_stream_attach(tsa_stream_t* parent, tsa_stream_t* child);
int tsa_stream_detach(tsa_stream_t* parent, tsa_stream_t* child);

// Send a TS packet to this stream node, which will invoke `on_ts` and optionally forward to children.
void tsa_stream_send(tsa_stream_t* stream, const uint8_t* ts);

// Reactive Demux APIs
void tsa_stream_demux_set_callbacks(tsa_stream_t* stream, 
                                    void (*join_pid)(void*, uint16_t), 
                                    void (*leave_pid)(void*, uint16_t));
void tsa_stream_demux_join_pid(tsa_stream_t* stream, uint16_t pid);
void tsa_stream_demux_leave_pid(tsa_stream_t* stream, uint16_t pid);
bool tsa_stream_demux_check_pid(tsa_stream_t* stream, uint16_t pid);

#endif // TSA_STREAM_H
