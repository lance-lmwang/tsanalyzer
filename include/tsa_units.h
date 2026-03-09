#ifndef TSA_UNITS_H
#define TSA_UNITS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Convert human readable bitrate (e.g. "15Mbps", "500k") to bps.
 */
uint64_t tsa_units_to_bitrate(const char* s);

/**
 * Convert human readable time (e.g. "10s", "500ms") to nanoseconds.
 */
uint64_t tsa_units_to_ns(const char* s);

/**
 * Convert human readable size (e.g. "32M", "1G") to bytes.
 */
uint64_t tsa_units_to_size(const char* s);

/**
 * Convert boolean strings (on/off, true/false, yes/no) to bool.
 */
bool tsa_units_to_bool(const char* s);

/**
 * Convert PCR ticks (27MHz) to nanoseconds.
 */
uint64_t tsa_pcr_to_ns(uint64_t pcr_ticks);

/**
 * Convert PCR ticks (27MHz) to nanoseconds (double).
 */
double tsa_pcr_to_ns_f(double pcr_ticks);

#endif
