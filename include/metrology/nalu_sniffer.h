#ifndef TSA_METROLOGY_NALU_SNIFFER_H
#define TSA_METROLOGY_NALU_SNIFFER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NALU_TYPE_UNKNOWN = 0,
    NALU_TYPE_SPS,
    NALU_TYPE_PPS,
    NALU_TYPE_IDR,      // I-Frame
    NALU_TYPE_NON_IDR,  // P/B-Frame
    NALU_TYPE_SEI
} tsa_nalu_type_t;

typedef struct {
    uint8_t nalu_type_raw;
    tsa_nalu_type_t nalu_type_abstract;
    bool is_h265;

    // For SPS
    uint16_t width;
    uint16_t height;
    uint8_t profile;
    uint8_t level;
    uint8_t chroma_format;
    uint8_t bit_depth;

    // For SEI
    bool has_cea708;

    // For Video Frames
    bool is_slice;
    int slice_type;  // -1 if unknown, 0=P, 1=B, 2=I
} tsa_nalu_info_t;

/**
 * @brief Zero-copy NALU parser for H.264/H.265.
 *
 * @param buf Pointer to the first byte AFTER the start code (e.g., after 00 00 01)
 * @param size Size of the remaining buffer
 * @param is_h265 True if stream is known to be H.265
 * @param out_info Parsed metadata output
 */
void tsa_nalu_sniff(const uint8_t* buf, int size, bool is_h265, tsa_nalu_info_t* out_info);

#ifdef __cplusplus
}
#endif

#endif  // TSA_METROLOGY_NALU_SNIFFER_H
