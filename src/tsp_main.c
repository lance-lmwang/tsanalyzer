#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "tsa_log.h"
#include "tsp.h"

static volatile sig_atomic_t keep_running = 1;

void sig_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  -b, --bitrate <bps>   Bitrate in bits per second (ignored if -P is used)\n");
    printf("  -i, --ip <ip>         Destination IP address (UDP)\n");
    printf("  -p, --port <port>     Destination UDP port\n");
    printf("      --srt-url <url>   SRT destination URL (e.g., srt://127.0.0.1:9000)\n");
    printf("  -P, --pcr             PCR-locked pacing mode\n");
    printf("  -C, --cbr             Strict CBR pacing mode (ignore PCR timing)\n");
    printf("  -m, --pcr-pid <pid>   PID to extract PCR from (default: first PID with PCR)\n");
    printf("  -l, --loop            Loop input file infinitely\n");
    printf("  -f, --file <file>     Input TS file (default: stdin)\n");
    printf("  -t, --ts-per-udp <n>  TS packets per UDP packet (default: 7)\n");
    printf("  -c, --cpu <core>      CPU core to bind (default: -1)\n");
    printf("  -h, --help            Show this help\n");
}

static void print_stats(tsp_handle_t* h, uint32_t ts_per_udp) {
    uint64_t total_ts, drops, det_rate, udp_pps;
    int64_t max_j, min_j;
    if (tsp_get_stats(h, &total_ts, &max_j, &min_j, &drops, &det_rate, &udp_pps) != 0) return;

    uint64_t est_rate = tsp_get_estimated_bitrate(h);
    double cur_mbps = (double)udp_pps * ts_per_udp * 188.0 * 8.0 / 1000000.0;

    tsa_info("STATS", "Pkts: %lu, Source: %.2f Mbps, Output: %.2f Mbps (Target: %.2f Mbps)", total_ts,
             (double)est_rate / 1000000.0, cur_mbps, (double)det_rate / 1000000.0);
}

int main(int argc, char* argv[]) {
    tsp_config_t cfg = {0};
    cfg.ts_per_udp = 7;
    cfg.mode = TSPACER_MODE_BASIC;
    cfg.cpu_core = -1;
    const char* input_file = NULL;
    int loop_file = 0;

    static struct option long_options[] = {{"bitrate", required_argument, 0, 'b'},
                                           {"ip", required_argument, 0, 'i'},
                                           {"port", required_argument, 0, 'p'},
                                           {"srt-url", required_argument, 0, 'S'},
                                           {"pcr", no_argument, 0, 'P'},
                                           {"cbr", no_argument, 0, 'C'},
                                           {"pcr-pid", required_argument, 0, 'm'},
                                           {"loop", no_argument, 0, 'l'},
                                           {"file", required_argument, 0, 'f'},
                                           {"ts-per-udp", required_argument, 0, 't'},
                                           {"cpu", required_argument, 0, 'c'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "b:i:p:S:PCm:lf:t:c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b':
                cfg.bitrate = strtoull(optarg, NULL, 10);
                break;
            case 'i':
                cfg.dest_ip = optarg;
                break;
            case 'p':
                cfg.port = (uint16_t)atoi(optarg);
                break;
            case 'S':
                cfg.url = optarg;
                break;
            case 'P':
                cfg.mode = TSPACER_MODE_PCR;
                break;
            case 'C':
                cfg.mode = TSPACER_MODE_CBR;
                break;
            case 'm':
                cfg.pcr_pid = (uint16_t)strtol(optarg, NULL, 0);
                break;
            case 'l':
                loop_file = 1;
                break;
            case 'f':
                input_file = optarg;
                break;
            case 't':
                cfg.ts_per_udp = (uint32_t)atoi(optarg);
                break;
            case 'c':
                cfg.cpu_core = atoi(optarg);
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    if ((cfg.mode != TSPACER_MODE_PCR && cfg.bitrate == 0) || (cfg.dest_ip == NULL && cfg.url == NULL)) {
        tsa_error("MAIN", "Error: Missing required arguments (bitrate/pcr mode and destination).");
        print_usage(argv[0]);
        return 1;
    }

    FILE* in = stdin;
    if (input_file) {
        in = fopen(input_file, "rb");
        if (!in) {
            tsa_error("MAIN", "Cannot open input file: %s", strerror(errno));
            return 1;
        }
    }

    tsp_handle_t* h = tsp_create(&cfg);
    if (!h || tsp_start(h) != 0) {
        tsa_error("MAIN", "tsp start failed");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    uint8_t buf[188 * 100];
    uint64_t last_stats_ns = 0;
    tsa_info("MAIN", "TsPacer Started. Mode: %d, Bitrate: %lu", cfg.mode, cfg.bitrate);

    while (keep_running) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n == 0) {
            if (loop_file && input_file) {
                fseek(in, 0, SEEK_SET);
                continue;
            }
            break;
        }

        size_t pkts = n / 188;
        size_t enq = 0;
        while (keep_running && enq < pkts) {
            int r = tsp_enqueue(h, buf + (enq * 188), pkts - enq);
            if (r > 0) {
                enq += r;
            } else {
                usleep(1000);  // Buffer full
            }

            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
            if (now > last_stats_ns + 1000000000ULL) {
                print_stats(h, cfg.ts_per_udp);
                last_stats_ns = now;
            }
        }
    }

    tsa_info("MAIN", "Stopping pacer...");
    tsp_stop(h);
    tsp_destroy(h);
    if (in != stdin) fclose(in);
    return 0;
}
