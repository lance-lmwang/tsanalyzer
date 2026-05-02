#ifndef TSS_FIFO_H
#define TSS_FIFO_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *buffer;
    size_t head;
    size_t tail;
    size_t size;
    size_t capacity;
} tss_fifo_t;

tss_fifo_t* tss_fifo_alloc(size_t capacity);
void tss_fifo_free(tss_fifo_t *f);
int tss_fifo_write(tss_fifo_t *f, const uint8_t *buf, size_t len);
int tss_fifo_read(tss_fifo_t *f, uint8_t *buf, size_t len);
size_t tss_fifo_size(const tss_fifo_t *f);
void tss_fifo_reset(tss_fifo_t *f);
int tss_fifo_peek(const tss_fifo_t *f, uint8_t *buf, size_t len, size_t offset);
void tss_fifo_drain(tss_fifo_t *f, size_t len);

#endif
