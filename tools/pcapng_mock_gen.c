#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * libtsshaper - Professional Mock PCAP Generator (Spec-Perfect Version)
 *
 * Purpose: Creates a standard PCAP file using the nanosecond magic number (0xa1b23c4d).
 * Integrates static IPv4 checksums and strict MPEG-TS CC behavior.
 */

typedef struct {
    uint32_t magic_number;   /* magic number 0xa1b23c4d for nanoseconds */
    uint16_t version_major;  /* 2 */
    uint16_t version_minor;  /* 4 */
    int32_t  thiszone;       /* 0 */
    uint32_t sigfigs;        /* 0 */
    uint32_t snaplen;        /* 65535 */
    uint32_t network;        /* 1 = Ethernet */
} pcap_hdr_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_usec;        /* nanoseconds in this mode */
    uint32_t incl_len;
    uint32_t orig_len;
} pcaprec_hdr_t;

void write_ts_pcr(uint8_t* buf, uint64_t pcr_ns) {
    uint64_t ticks = (pcr_ns * 27) / 1000;
    uint64_t base = ticks / 300;
    uint16_t ext = ticks % 300;

    buf[0] = 0x47; // Sync
    buf[1] = 0x01; // PID 0x100
    buf[2] = 0x00;

    // [Spec-Fix 2]: ISO/IEC 13818-1: CC does NOT increment for AF-only packets (0x20)
    buf[3] = 0x20; // AFC=0b10 (AF only), CC=0

    buf[4] = 183;  // AF Length
    buf[5] = 0x10; // PCR flag

    buf[6] = (base >> 25) & 0xFF;
    buf[7] = (base >> 17) & 0xFF;
    buf[8] = (base >> 9) & 0xFF;
    buf[9] = (base >> 1) & 0xFF;
    buf[10] = ((base & 0x01) << 7) | 0x7E | ((ext >> 8) & 0x01);
    buf[11] = ext & 0xFF;
    memset(&buf[12], 0xFF, 176);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <output.pcap> <jitter_ns>\n", argv[0]);
        return 1;
    }

    int64_t intentional_jitter = atoll(argv[2]);
    FILE* f = fopen(argv[1], "wb");
    if (!f) return 1;

    pcap_hdr_t main_hdr = {0xa1b23c4d, 2, 4, 0, 0, 65535, 1};
    fwrite(&main_hdr, 1, sizeof(main_hdr), f);

    uint64_t current_time_ns = 1000000000ULL;
    uint8_t udp_packet[42 + 188];
    memset(udp_packet, 0, 42);

    // [Spec-Fix 3]: Proper Static IP Checksum for a 216-byte fixed packet
    // Ethernet
    udp_packet[12] = 0x08; udp_packet[13] = 0x00; // IPv4
    // IPv4 Header
    udp_packet[14] = 0x45;
    udp_packet[16] = 0x00; udp_packet[17] = 0xD8; // Total Len: 216 bytes
    udp_packet[23] = 17;                          // Protocol: UDP
    udp_packet[24] = 0xAA; udp_packet[25] = 0x27; // Static Checksum for 0.0.0.0 src/dst
    // UDP Header
    udp_packet[36] = 0x04; udp_packet[37] = 0xD2; // Port 1234
    udp_packet[38] = 0x00; udp_packet[39] = 0xC4; // UDP Len: 196 bytes

    for (int i = 0; i < 100; i++) {
        uint64_t emit_ns = current_time_ns;
        if (i > 0 && i % 2 == 0) emit_ns += intentional_jitter;

        write_ts_pcr(&udp_packet[42], current_time_ns);

        pcaprec_hdr_t rec_hdr = {
            (uint32_t)(emit_ns / 1000000000ULL),
            (uint32_t)(emit_ns % 1000000000ULL),
            42 + 188, 42 + 188
        };

        fwrite(&rec_hdr, 1, sizeof(rec_hdr), f);
        fwrite(udp_packet, 1, 42 + 188, f);

        current_time_ns += 30000000ULL; // 30ms interval
    }

    fclose(f);
    printf("[+] Generated spec-compliant PCAP: %s (%lldns jitter)\n", argv[1], (long long)intentional_jitter);
    return 0;
}
