#include "tsa_descriptors.h"
#include "tsa_internal.h"
#include <assert.h>
#include <stdio.h>

void test_descriptor_registry() {
    printf("Testing descriptor registry...\n");
    tsa_descriptors_init();

    uint8_t stream_type = 0x06;
    uint8_t ac3_desc[] = { 0x6a, 0x01, 0x00 }; // Tag 0x6a, len 1

    tsa_descriptors_process(NULL, 100, ac3_desc, &stream_type);
    assert(stream_type == TSA_TYPE_AUDIO_AC3);

    stream_type = 0x06;
    uint8_t sub_desc[] = { 0x59, 0x01, 0x00 };
    tsa_descriptors_process(NULL, 101, sub_desc, &stream_type);
    assert(stream_type == 0x59);

    stream_type = 0x06;
    uint8_t tele_desc[] = { 0x56, 0x01, 0x00 };
    tsa_descriptors_process(NULL, 102, tele_desc, &stream_type);
    assert(stream_type == 0x56);

    // Test that it doesn't change if not 0x06
    stream_type = 0x02; // MPEG2 Video
    tsa_descriptors_process(NULL, 100, ac3_desc, &stream_type);
    assert(stream_type == 0x02);

    printf("Registry tests passed.\n");
}

static void custom_handler(struct tsa_handle *h, uint16_t pid, uint8_t tag, const uint8_t *data, uint8_t len, uint8_t *stream_type) {
    (void)h; (void)pid; (void)tag; (void)data; (void)len;
    *stream_type = 0xFF;
}

void test_custom_handler() {
    printf("Testing custom handler...\n");
    tsa_descriptors_register_handler(0xEE, custom_handler);

    uint8_t stream_type = 0x06;
    uint8_t custom_desc[] = { 0xEE, 0x02, 0xAA, 0xBB };
    tsa_descriptors_process(NULL, 200, custom_desc, &stream_type);
    assert(stream_type == 0xFF);

    printf("Custom handler tests passed.\n");
}

int main() {
    test_descriptor_registry();
    test_custom_handler();
    printf("All descriptor tests passed!\n");
    return 0;
}
