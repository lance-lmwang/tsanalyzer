#ifndef TSA_SIMD_H
#define TSA_SIMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Professional SIMD Function Dispatch Table (FFmpeg-style)
 */
typedef struct {
    /* Find first 0x47 in buffer */
    intptr_t (*find_sync)(const uint8_t* buf, size_t len);

    /* Batch extract 8 PIDs from consecutive TS packets */
    void (*extract_pids_8)(const uint8_t* buf, uint16_t* pids);

    /* Batch check sync bytes for multiple packets */
    uint64_t (*check_sync_batch)(const uint8_t* buf, size_t packet_count);

    /* Check if current instance is using hardware acceleration */
    bool is_accelerated;
} tsa_simd_ops_t;

/**
 * @brief Global SIMD dispatch table. Initialized on first use or via tsa_simd_init().
 */
extern tsa_simd_ops_t tsa_simd;

/**
 * @brief Initialize the SIMD dispatch table based on runtime CPU detection.
 * This is thread-safe and can be called multiple times.
 */
void tsa_simd_init(void);

/**
 * @brief Find the first occurrence of TS sync byte (0x47) in a buffer using SIMD.
 */
intptr_t tsa_simd_find_sync(const uint8_t* buf, size_t len);

/**
 * @brief Batch extract PIDs from 8 consecutive TS packets.
 */
void tsa_simd_extract_pids_8(const uint8_t* buf, uint16_t* pids);

/**
 * @brief Batch check sync bytes for multiple 188-byte packets.
 */
uint64_t tsa_simd_check_sync_batch(const uint8_t* buf, size_t packet_count);

/**
 * @brief Check if current CPU supports required SIMD features.
 */
bool tsa_simd_capable(void);

#endif  // TSA_SIMD_H
