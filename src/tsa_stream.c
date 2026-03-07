#include "tsa_stream.h"

#include <string.h>

void tsa_stream_init(tsa_stream_t* stream, void* self_ctx, void (*on_ts_cb)(void*, const uint8_t*)) {
    if (!stream) return;
    memset(stream, 0, sizeof(tsa_stream_t));
    stream->self = self_ctx;
    stream->on_ts = on_ts_cb;
}

void tsa_stream_destroy(tsa_stream_t* stream) {
    if (!stream) return;
    for (int i = 0; i < TSA_MAX_PID; ++i) {
        if (stream->pid_list[i] > 0) {
            tsa_stream_demux_leave_pid(stream, i);
        }
    }
}

int tsa_stream_attach(tsa_stream_t* parent, tsa_stream_t* child) {
    if (!parent || !child) return -1;
    if (parent->child_count >= TSA_MAX_STREAM_CHILDREN) return -1;

    parent->children[parent->child_count++] = child;
    child->parent = parent;

    // When attaching, child might need to inform parent about its existing PIDs
    for (int i = 0; i < TSA_MAX_PID; ++i) {
        if (child->pid_list[i] > 0) {
            // Propagate join upwards manually for initial state
            if (parent->join_pid) {
                parent->join_pid(parent->self, i);
            }
        }
    }

    return 0;
}

int tsa_stream_detach(tsa_stream_t* parent, tsa_stream_t* child) {
    if (!parent || !child) return -1;
    for (int i = 0; i < parent->child_count; ++i) {
        if (parent->children[i] == child) {
            // Propagate leave upwards for child's PIDs
            for (int p = 0; p < TSA_MAX_PID; ++p) {
                if (child->pid_list[p] > 0 && parent->leave_pid) {
                    parent->leave_pid(parent->self, p);
                }
            }

            // Shift array
            for (int j = i; j < parent->child_count - 1; ++j) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            child->parent = NULL;
            return 0;
        }
    }
    return -1;
}

void tsa_stream_send(tsa_stream_t* stream, const uint8_t* ts) {
    if (!stream) return;
    if (stream->on_ts) {
        stream->on_ts(stream->self, ts);
    }
    for (int i = 0; i < stream->child_count; ++i) {
        tsa_stream_send(stream->children[i], ts);
    }
}

void tsa_stream_demux_set_callbacks(tsa_stream_t* stream, void (*join_pid)(void*, uint16_t),
                                    void (*leave_pid)(void*, uint16_t)) {
    if (!stream) return;
    stream->join_pid = join_pid;
    stream->leave_pid = leave_pid;
}

void tsa_stream_demux_join_pid(tsa_stream_t* stream, uint16_t pid) {
    if (!stream || pid >= TSA_MAX_PID) return;

    stream->pid_list[pid]++;
    if (stream->pid_list[pid] == 1 && stream->parent && stream->parent->join_pid) {
        stream->parent->join_pid(stream->parent->self, pid);
    }
}

void tsa_stream_demux_leave_pid(tsa_stream_t* stream, uint16_t pid) {
    if (!stream || pid >= TSA_MAX_PID) return;

    if (stream->pid_list[pid] > 0) {
        stream->pid_list[pid]--;
        if (stream->pid_list[pid] == 0 && stream->parent && stream->parent->leave_pid) {
            stream->parent->leave_pid(stream->parent->self, pid);
        }
    }
}

bool tsa_stream_demux_check_pid(tsa_stream_t* stream, uint16_t pid) {
    if (!stream || pid >= TSA_MAX_PID) return false;
    return stream->pid_list[pid] > 0;
}
