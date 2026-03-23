#ifndef LIBTSSHAPER_HAL_H
#define LIBTSSHAPER_HAL_H

#include <stdint.h>
#include <sys/socket.h>

struct mmsghdr;
typedef struct tsshaper_ctx tsshaper_t;

/**
 * @brief Initialize the HAL Ops table for a specific backend type.
 */
int hal_init_ops(tsshaper_t* ctx, int backend_type);

/**
 * @brief Get high-resolution monotonic time in nanoseconds.
 */
uint64_t hal_get_time_ns(void);

/**
 * @brief Multi-stage precision wait (nanosleep, yield, busy-wait).
 * @param target_ns The absolute time in nanoseconds to wait for.
 */
void hal_precision_wait(uint64_t target_ns);

/**
 * @brief Setup real-time scheduling policy and CPU affinity.
 * @param cpu_affinity CPU core ID to pin the thread to, or -1 to disable.
 * @param priority Real-time priority (0-99).
 * @return 0 on success, -1 on failure.
 */
int hal_setup_rt(int cpu_affinity, int priority);

#endif  // LIBTSSHAPER_HAL_H
