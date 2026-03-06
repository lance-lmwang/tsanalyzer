/**
 * @file tsa_slicer.c
 * @brief Utility to slice MPEG-TS files based on PCR time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>

#include "tsa.h"
#include "tsa_internal.h"

void print_usage(const char* prog) {
    printf("Usage: %s [options] <input.ts> <output.ts>\n", prog);
    printf("Options:\n");
    printf("  -s, --start <sec>   Start time in seconds (default: 0)\n");
    printf("  -d, --duration <sec> Duration in seconds (default: all)\n");
    printf("  -p, --pid <pid>     PCR PID to use for timing (default: auto-detect)\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char** argv) {
    double start_sec = 0;
    double duration_sec = -1;
    int pcr_pid = -1;

    static struct option long_options[] = {
        {"start", required_argument, 0, 's'},
        {"duration", required_argument, 0, 'd'},
        {"pid", required_argument, 0, 'p'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:d:p:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 's': start_sec = atof(optarg); break;
            case 'd': duration_sec = atof(optarg); break;
            case 'p': pcr_pid = strtol(optarg, NULL, 0); break;
            case 'h': print_usage(argv[0]); return 0;
            default: return 1;
        }
    }

    if (optind + 2 > argc) {
        print_usage(argv[0]);
        return 1;
    }

    const char* in_path = argv[optind];
    const char* out_path = argv[optind + 1];

    FILE* ifh = fopen(in_path, "rb");
    if (!ifh) { perror("fopen input"); return 1; }

    /* Phase 1: Scan for first PCR and auto-detect PID if needed */
    uint8_t pkt[188];
    uint64_t first_pcr = INVALID_PCR;
    int detected_pcr_pid = -1;

    printf("Scanning for timing baseline...\n");
    while (fread(pkt, 1, 188, ifh) == 1) {
        if (pkt[0] != 0x47) continue;
        uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        uint64_t pcr = extract_pcr(pkt);
        if (pcr != INVALID_PCR) {
            if (pcr_pid == -1 || pid == pcr_pid) {
                first_pcr = pcr;
                detected_pcr_pid = pid;
                break;
            }
        }
    }

    if (first_pcr == INVALID_PCR) {
        fprintf(stderr, "Error: No PCR found in file.\n");
        fclose(ifh);
        return 1;
    }

    printf("Found baseline PCR on PID 0x%04x: %lu\n", detected_pcr_pid, first_pcr);

    rewind(ifh);
    FILE* ofh = fopen(out_path, "wb");
    if (!ofh) { perror("fopen output"); fclose(ifh); return 1; }

    uint64_t start_pcr = first_pcr + (uint64_t)(start_sec * 27000000.0);
    uint64_t end_pcr = (duration_sec > 0) ? (start_pcr + (uint64_t)(duration_sec * 27000000.0)) : 0xFFFFFFFFFFFFFFFFULL;

    printf("Slicing from %.2fs to %.2fs...\n", start_sec, start_sec + (duration_sec > 0 ? duration_sec : 0));

    uint64_t pkts_written = 0;
    bool in_range = false;
    uint64_t last_known_pcr = first_pcr;
    (void)last_known_pcr;

    while (fread(pkt, 1, 188, ifh) == 1) {
        if (pkt[0] != 0x47) continue;
        uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        uint64_t pcr = extract_pcr(pkt);

        if (pcr != INVALID_PCR && pid == detected_pcr_pid) {
            last_known_pcr = pcr;
            if (pcr >= start_pcr && pcr <= end_pcr) {
                in_range = true;
            } else if (pcr > end_pcr) {
                break; /* Finished */
            }
        }

        if (in_range) {
            fwrite(pkt, 1, 188, ofh);
            pkts_written++;
        }
    }

    printf("Slice complete. Written %lu packets (%.2f MB).\n",
           pkts_written, (double)pkts_written * 188 / (1024 * 1024));

    fclose(ifh);
    fclose(ofh);
    return 0;
}
