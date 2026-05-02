#include "tss_fifo.h"
#include <stdlib.h>
#include <string.h>

tss_fifo_t* tss_fifo_alloc(size_t capacity) {
    tss_fifo_t *f = malloc(sizeof(tss_fifo_t));
    if (!f) return NULL;
    f->buffer = malloc(capacity);
    if (!f->buffer) {
        free(f);
        return NULL;
    }
    f->capacity = capacity;
    f->head = f->tail = f->size = 0;
    return f;
}

void tss_fifo_free(tss_fifo_t *f) {
    if (f) {
        free(f->buffer);
        free(f);
    }
}

size_t tss_fifo_size(const tss_fifo_t *f) {
    return f ? f->size : 0;
}

void tss_fifo_reset(tss_fifo_t *f) {
    if (f) f->head = f->tail = f->size = 0;
}

int tss_fifo_write(tss_fifo_t *f, const uint8_t *buf, size_t len) {
    if (!f || f->size + len > f->capacity) return -1;
    size_t to_end = f->capacity - f->tail;
    if (len <= to_end) {
        memcpy(f->buffer + f->tail, buf, len);
    } else {
        memcpy(f->buffer + f->tail, buf, to_end);
        memcpy(f->buffer, buf + to_end, len - to_end);
    }
    f->tail = (f->tail + len) % f->capacity;
    f->size += len;
    return 0;
}

int tss_fifo_read(tss_fifo_t *f, uint8_t *buf, size_t len) {
    if (!f || f->size < len) return -1;
    size_t to_end = f->capacity - f->head;
    if (buf) {
        if (len <= to_end) {
            memcpy(buf, f->buffer + f->head, len);
        } else {
            memcpy(buf, f->buffer + f->head, to_end);
            memcpy(buf + to_end, f->buffer, len - to_end);
        }
    }
    f->head = (f->head + len) % f->capacity;
    f->size -= len;
    return 0;
}

int tss_fifo_peek(const tss_fifo_t *f, uint8_t *buf, size_t len, size_t offset) {
    if (!f || f->size < offset + len) return -1;
    size_t start = (f->head + offset) % f->capacity;
    size_t to_end = f->capacity - start;
    if (len <= to_end) {
        memcpy(buf, f->buffer + start, len);
    } else {
        memcpy(buf, f->buffer + start, to_end);
        memcpy(buf + to_end, f->buffer, len - to_end);
    }
    return 0;
}

void tss_fifo_drain(tss_fifo_t *f, size_t len) {
    if (!f) return;
    if (len > f->size) len = f->size;
    f->head = (f->head + len) % f->capacity;
    f->size -= len;
}
