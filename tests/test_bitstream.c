#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "tsa_bitstream.h"

void test_basic_reading() {
    uint8_t data[] = {0b10101010, 0b11001100, 0b11110000, 0b00001111};
    bit_reader_t r;
    br_init(&r, data, sizeof(data));

    assert(br_read(&r, 1) == 1);
    assert(br_read(&r, 1) == 0);
    assert(br_read(&r, 2) == 2);   // 10
    assert(br_read(&r, 4) == 10);  // 1010

    assert(br_read(&r, 8) == 0b11001100);
    assert(br_read(&r, 16) == 0b1111000000001111);

    printf("test_basic_reading passed.\n");
}

void test_ue_golomb() {
    // Test cases for Exp-Golomb (ue)
    // 1 => 0
    // 010 => 1
    // 011 => 2
    // 00100 => 3
    // 00101 => 4
    // 00110 => 5
    // 00111 => 6
    // 0001000 => 7

    uint8_t data[] = {
        0b10100110,  // 1(0) | 010(1) | 011(2) | 0
        0b01000010,  // 0100(3) | 0010(left part of 4)
        0b10011000,  // 1(right part of 4) | 00110(5) | 00
        0b11100010,  // 111(6) | 00010(left part of 7)
        0b00000000   // 00(right part of 7)
    };

    bit_reader_t r;
    br_init(&r, data, sizeof(data));

    assert(br_read_ue(&r) == 0);
    assert(br_read_ue(&r) == 1);
    assert(br_read_ue(&r) == 2);
    assert(br_read_ue(&r) == 3);
    assert(br_read_ue(&r) == 4);
    assert(br_read_ue(&r) == 5);
    assert(br_read_ue(&r) == 6);
    assert(br_read_ue(&r) == 7);

    printf("test_ue_golomb passed.\n");
}

void test_se_golomb() {
    // Test cases for Signed Exp-Golomb (se)
    // Code Num -> value
    // 0 -> 0
    // 1 -> 1
    // 2 -> -1
    // 3 -> 2
    // 4 -> -2
    // 5 -> 3
    // 6 -> -3

    // Using the same ue codes: 0, 1, 2, 3, 4, 5, 6
    uint8_t data[] = {
        0b10100110,  // 1(0) | 010(1) | 011(2) | 0
        0b01000010,  // 0100(3) | 0010(left part of 4)
        0b10011000,  // 1(right part of 4) | 00110(5) | 00
        0b11100000   // 111(6) | pad
    };

    bit_reader_t r;
    br_init(&r, data, sizeof(data));

    assert(br_read_se(&r) == 0);
    assert(br_read_se(&r) == 1);
    assert(br_read_se(&r) == -1);
    assert(br_read_se(&r) == 2);
    assert(br_read_se(&r) == -2);
    assert(br_read_se(&r) == 3);
    assert(br_read_se(&r) == -3);

    printf("test_se_golomb passed.\n");
}

void test_peek_skip() {
    uint8_t data[] = {0xAA, 0x55};  // 10101010 01010101
    bit_reader_t r;
    br_init(&r, data, sizeof(data));

    assert(br_peek(&r, 4) == 0xA);
    assert(br_peek(&r, 4) == 0xA);  // Peek should not advance

    br_skip(&r, 4);
    assert(br_read(&r, 4) == 0xA);

    assert(br_peek(&r, 8) == 0x55);
    br_skip(&r, 8);

    assert(r.bits_left == 0);

    printf("test_peek_skip passed.\n");
}

void test_large_read() {
    uint8_t data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    bit_reader_t r;
    br_init(&r, data, sizeof(data));

    assert(br_read(&r, 32) == 0x11223344);
    assert(br_read(&r, 24) == 0x556677);
    assert(br_read(&r, 8) == 0x88);

    printf("test_large_read passed.\n");
}

int main() {
    test_basic_reading();
    test_ue_golomb();
    test_se_golomb();
    test_peek_skip();
    test_large_read();
    printf("All bitstream tests passed.\n");
    return 0;
}
