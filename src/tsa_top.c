/**
 * @file tsa_top.c
 * @brief High-Density Terminal UI for TsAnalyzer Pro.
 *
 * Provides a real-time monitor using ncurses, reading metrics from
 * shared memory with sequence-lock protection for data consistency.
 */

#include <fcntl.h>
#include <math.h>
#include <ncurses.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "../include/tsa_top_shm.h"

/* --- Configuration & Constants --- */
#define REFRESH_INTERVAL_US 100000 /* 100ms */

static volatile sig_atomic_t g_keep_running = 1;

/**
 * @brief Renders a single stream's metrics across two lines.
 */
static void draw_stream_row(int row, int max_x, const tsa_top_stream_info_t* s) {
    if (!s->is_active) return;

    /* Line 1: Primary Transmission Metrics */
    int color_pair = 2; /* Default: Green */
    if (s->master_health < 80.0) {
        color_pair = 4; /* Critical: Red */
    } else if (s->master_health < 95.0) {
        color_pair = 3; /* Warning: Yellow */
    }

    attron(COLOR_PAIR(color_pair));
    char err_p123[32], flags[16];
    snprintf(err_p123, sizeof(err_p123), "%lu/%lu/%lu", s->p1_errors, s->p2_errors, s->p3_errors);
    snprintf(flags, sizeof(flags), "%s%s", s->has_scte35 ? "S35 " : "", s->has_cea708 ? "CC " : "");

    mvprintw(row, 0, "%-20.20s %7.2fM %9.1f%% %10lu %12s %5.1fms %5.1fms %8s", s->stream_id, s->current_bitrate_mbps,
             s->master_health, s->cc_errors, err_p123, s->pcr_jitter_p99_ms, s->mdi_df_ms, flags);
    attroff(COLOR_PAIR(color_pair));

    /* Line 2: Deep Diagnostics & Metadata (Indented) */
    attron(COLOR_PAIR(6)); /* Magenta for Timing/Drift */
    mvprintw(row + 1, 2, "RST(N/E): %5.1fs / %5.1fs  Drift(S/L): %5.1f / %5.1f ppm", s->rst_net_s, s->rst_enc_s,
             s->drift_ppm, s->drift_long_ppm);
    attroff(COLOR_PAIR(6));

    if (s->width > 0) {
        attron(COLOR_PAIR(5)); /* Cyan for Video Metadata */
        printw("  RES: %.0fx%.0f @ %.1f fps (GOP: %.0fms)", s->width, s->height, s->fps, s->gop_ms);
        attroff(COLOR_PAIR(5));
    }
}

int main(void) {
    int fd = shm_open(TSA_TOP_SHM_NAME, O_RDONLY, 0666);
    if (fd < 0) {
        fprintf(stderr, "Error: Unable to access shared memory (%s). Ensure tsa_server_pro is running.\n",
                TSA_TOP_SHM_NAME);
        return EXIT_FAILURE;
    }

    tsa_top_shm_block_t* shm = mmap(NULL, sizeof(tsa_top_shm_block_t), PROT_READ, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return EXIT_FAILURE;
    }

    /* Initialize Ncurses */
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();

    init_pair(1, COLOR_WHITE, COLOR_BLUE); /* Header */
    init_pair(2, COLOR_GREEN, -1);         /* OK */
    init_pair(3, COLOR_YELLOW, -1);        /* Warning */
    init_pair(4, COLOR_RED, -1);           /* Error */
    init_pair(5, COLOR_CYAN, -1);          /* Metadata */
    init_pair(6, COLOR_MAGENTA, -1);       /* Timing */

    uint64_t last_rendered_seq = 0;

    while (g_keep_running) {
        int input = getch();
        if (input == 'q' || input == 'Q') break;

        /* Sequence Lock Check: Only read if data is stable (even) and new */
        uint64_t seq_start = atomic_load_explicit((_Atomic uint64_t*)&shm->seq_lock, memory_order_acquire);

        if (seq_start != last_rendered_seq && (seq_start % 2 == 0)) {
            tsa_top_shm_block_t local;
            memcpy(&local, shm, sizeof(tsa_top_shm_block_t));

            uint64_t seq_end = atomic_load_explicit((_Atomic uint64_t*)&shm->seq_lock, memory_order_acquire);
            if (seq_start == seq_end) {
                last_rendered_seq = seq_start;

                int max_y, max_x;
                getmaxyx(stdscr, max_y, max_x);
                clear();

                /* Render Banner */
                attron(COLOR_PAIR(1) | A_BOLD);
                mvprintw(0, 0, " TsAnalyzer Pro [Top] | Active Streams: %lu | System Health: %.1f%% ",
                         local.num_active_streams, local.global_health);
                for (int i = getcurx(stdscr); i < max_x; i++) addch(' ');
                attroff(COLOR_PAIR(1) | A_BOLD);

                /* Render Header Labels */
                attron(A_BOLD);
                mvprintw(1, 0, "%-20s %10s %10s %10s %12s %8s %8s %8s", "STREAM ID", "BITRATE", "HEALTH", "CC_ERR",
                         "TR_P1/2/3", "PCR_JIT", "MDI_DF", "FLAGS");
                attroff(A_BOLD);

                /* Render Streams */
                int current_row = 2;
                for (uint64_t i = 0; i < local.num_active_streams && current_row < max_y - 2; i++) {
                    draw_stream_row(current_row, max_x, &local.streams[i]);
                    current_row += 3; /* Two lines of data + one line spacer */
                }

                /* Render Footer */
                attron(A_DIM);
                mvprintw(max_y - 1, 0, " [q] Quit | Deterministic Metrology Engine Active | Sequence: %lu",
                         last_rendered_seq);
                attroff(A_DIM);

                refresh();
            }
        }
        usleep(REFRESH_INTERVAL_US);
    }

    endwin();
    munmap(shm, sizeof(tsa_top_shm_block_t));
    close(fd);

    return EXIT_SUCCESS;
}
