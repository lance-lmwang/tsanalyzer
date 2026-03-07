#ifndef TSA_SIMD_H
#define TSA_SIMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Find the first occurrence of TS sync byte (0x47) in a buffer using SIMD.
 *
 * @param buf Pointer to the data
 * @param len Length of data to scan
 * @return offset to the first 0x47, or -1 if not found
 */
intptr_t tsa_simd_find_sync(const uint8_t* buf, size_t len);

/**
 * @brief Batch check sync bytes for multiple 188-byte packets.
 *
 * @param buf Pointer to the start of the first packet (must be aligned to 188)
 * @param packet_count Number of packets to check
 * @return uint64_t Bitmask where bit i is 1 if packet i has valid sync byte
 */
uint64_t tsa_simd_check_sync_batch(const uint8_t* buf, size_t packet_count);

/**
 * @brief Batch extract PIDs from 8 consecutive TS packets.
 *
 * @param buf Pointer to the start of the packets (must have 188*8 bytes available)
 * @param pids Output array for 8 extracted 13-bit PIDs
 */
void tsa_simd_extract_pids_8(const uint8_t* buf, uint16_t* pids);

/**
 * @brief Check if current CPU supports required SIMD features.
 */
bool tsa_simd_capable(void);

#endif  // TSA_SIMD_H
