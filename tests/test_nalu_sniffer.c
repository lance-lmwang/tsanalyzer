#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "metrology/nalu_sniffer.h"

void test_h264_sps() {
    printf("Testing H.264 SPS parsing...\n");
    // Example 1920x1080 High Profile SPS
    uint8_t sps_data[] = {
        0x67, 0x64, 0x00, 0x28, 0xac, 0xd9, 0x40, 0x78, 0x02, 0x27, 0xe5, 0x84, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf0, 0x3c, 0x60, 0xc6, 0x58
    };

    tsa_nalu_info_t info;
    tsa_nalu_sniff(sps_data, sizeof(sps_data), false, &info);

    assert(info.is_h265 == false);
    assert(info.nalu_type_abstract == NALU_TYPE_SPS);
    assert(info.profile == 100); // High Profile
    assert(info.width == 1920);
    assert(info.height == 1080);
}

void test_h264_slice() {
    printf("Testing H.264 Slice parsing...\n");
    // P-slice example
    uint8_t p_slice_data[] = {
        0x41, 0x9a, 0x12, 0x34
    };

    tsa_nalu_info_t info;
    tsa_nalu_sniff(p_slice_data, sizeof(p_slice_data), false, &info);

    assert(info.is_h265 == false);
    assert(info.nalu_type_abstract == NALU_TYPE_NON_IDR);
    assert(info.is_slice == true);
    assert(info.slice_type == 0); // P-slice
}

int main() {
    test_h264_sps();
    test_h264_slice();
    printf("ALL NALU SNIFFER TESTS PASSED!\n");
    return 0;
}
