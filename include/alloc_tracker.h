#ifndef ALLOC_TRACKER_H
#define ALLOC_TRACKER_H

#include <stdint.h>

void start_allocation_tracking();
void stop_allocation_tracking();
uint64_t get_malloc_count();
void reset_malloc_count();

#endif
