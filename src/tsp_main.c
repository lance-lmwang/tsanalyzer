#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

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
    printf("  -m, --pcr-pid <pid>   PID to extract PCR from (default: first PID with PCR)\n");
    printf("  -l, --loop            Loop input file infinitely\n");
    printf("  -f, --file <file>     Input TS file (default: stdin)\n");
    printf("  -t, --ts-per-udp <n>  TS packets per UDP packet (default: 7)\n");
    printf("  -c, --cpu <core>      CPU core to bind (default: -1)\n");
    printf("  -h, --help            Show this help\n");
}

static void print_stats(tsp_handle_t* h, uint32_t ts_per_udp) {
    uint64_t total_p, drops, det_rate, pps;
    int64_t max_j, min_j;
    if (tsp_get_stats(h, &total_p, &max_j, &min_j, &drops, &det_rate, &pps) != 0) return;
    double cur_mbps = (double)pps * ts_per_udp * 188.0 * 8.0 / 1000000.0;
    printf("Pkts: %lu, PPS: %lu, Cur Rate: %.2f Mbps, PCR Rate: %.2f Mbps, Jitter: %ld/%ld ns, Drops: %lu\n", total_p,
           pps, cur_mbps, (double)det_rate / 1000000.0, max_j, min_j, drops);
    fflush(stdout);
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
                                           {"pcr-pid", required_argument, 0, 'm'},
                                           {"loop", no_argument, 0, 'l'},
                                           {"file", required_argument, 0, 'f'},
                                           {"ts-per-udp", required_argument, 0, 't'},
                                           {"cpu", required_argument, 0, 'c'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "b:i:p:S:Pm:lf:t:c:h", long_options, &option_index)) != -1) {
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
                cfg.srt_url = optarg;
                break;
            case 'P':
                cfg.mode = TSPACER_MODE_PCR;
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

    if ((cfg.mode != TSPACER_MODE_PCR && cfg.bitrate == 0) || (cfg.dest_ip == NULL && cfg.srt_url == NULL)) {
        fprintf(stderr, "Error: Missing required arguments.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (cfg.dest_ip != NULL && cfg.port == 0) {
        fprintf(stderr, "Error: UDP port required when using IP.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("Warning: mlockall failed. Run with CAP_IPC_LOCK for strict RT compliance");
    }

    FILE* in = stdin;
    if (input_file) {
        in = fopen(input_file, "rb");
        if (!in) {
            perror("fopen failed");
            return 1;
        }
    }

    tsp_handle_t* h = tsp_create(&cfg);
    if (!h) {
        fprintf(stderr, "tsp_create failed\n");
        return 1;
    }
    if (tsp_start(h) != 0) {
        fprintf(stderr, "tsp_start failed\n");
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    uint8_t buf[188 * 100];
    uint64_t last_stats_ns = 0;
    printf("TsPacer started. Bitrate: %lu bps, Dest: %s:%d\n", cfg.bitrate, cfg.dest_ip, cfg.port);

    struct pollfd pfd = {.fd = fileno(in), .events = POLLIN};

    while (keep_running) {
        int ret = poll(&pfd, 1, 10);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            size_t n = fread(buf, 1, sizeof(buf), in);
            if (n > 0) {
                size_t pkts = n / 188;
                int enq = 0;
                while (keep_running && enq < (int)pkts) {
                    int r = tsp_enqueue(h, buf + (enq * 188), (size_t)((int)pkts - enq));
                    if (r > 0)
                        enq += r;
                    else
                        usleep(1000);
                }
            } else if (feof(in)) {
                if (input_file && loop_file)
                    fseek(in, 0, SEEK_SET);
                else
                    break;
            }
        }

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        if (now - last_stats_ns >= 1000000000ULL) {
            print_stats(h, cfg.ts_per_udp);
            last_stats_ns = now;
        }
    }

    print_stats(h, cfg.ts_per_udp);
    tsp_destroy(h);
    if (in != stdin) fclose(in);
    return 0;
}
