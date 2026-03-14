#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"
#include "tsa_log.h"

#define TAG "PSI"

static void process_pat(tsa_handle_t* h, const uint8_t* p, uint64_t now, size_t max_len);
static void process_pmt(tsa_handle_t* h, uint16_t pid, const uint8_t* p, uint64_t now, size_t max_len);

void tsa_precompile_pid_labels(struct tsa_handle* h, uint16_t pid) {
    if (!h || pid >= TS_PID_MAX) return;
    const char* type = "Other";
    if (pid == 0)
        type = "PAT";
    else if (h->pid_is_pmt[pid])
        type = "PMT";
    snprintf(h->pid_labels[pid], TSA_LABEL_MAX, "{stream_id=\"%s\",pid=\"0x%04x\",type=\"%s\"}",
             h->config.input_label[0] ? h->config.input_label : "unknown", pid, type);
}

void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid) {
    h->pid_seen[pid] = false;
}

int16_t tsa_update_pid_tracker(struct tsa_handle* h, uint16_t p) {
    int16_t idx = h->pid_to_active_idx[p];
    if (idx == -1) {
        if (h->pid_tracker_count < MAX_ACTIVE_PIDS) {
            idx = (int16_t)h->pid_tracker_count++;
            h->pid_active_list[idx] = p;
            h->pid_to_active_idx[p] = idx;
        }
        h->pid_seen[p] = true;
        tsa_precompile_pid_labels(h, p);
    }
    return idx;
}

void tsa_section_filter_push(tsa_handle_t* h, uint16_t pid, const uint8_t* pkt, const ts_decode_result_t* r) {
    // 1. DVB/ATSC Section extraction MUST have payload and MUST have PUSI for our current simple parser
    uint8_t afc = (pkt[3] >> 4) & 0x03;
    if (!(afc & 0x01)) return;  // No payload
    if (!r->pusi) return;       // Only process start of sections for now to maintain stability

    // 2. Accurate Offset Calculation (matches ts_validator.c)
    int offset = 4;
    if (afc & 0x02) {  // Adaptation field present
        offset += pkt[4] + 1;
    }
    if (offset >= 187) return;  // No room for pointer + tid

    int pointer = pkt[offset];
    offset += pointer + 1;      // Start of the new section
    if (offset >= 184) return;  // Must have at least tid + length fields

    // 3. Section Boundary Check
    uint8_t tid = pkt[offset];
    if (tid == 0xFF) return;  // Stuffing

    uint16_t sl = (((pkt[offset + 1] & 0x0F) << 8) | pkt[offset + 2]) + 3;
    if (offset + sl > 188) {
        // Section spans multiple packets - skip for this minimal confirmed-correct version
        return;
    }

    // 4. CRC Validation (Standard MPEG-2)
    if (tsa_crc32_check(pkt + offset, sl) == 0) {
        if (tid == 0x00)
            process_pat(h, pkt + offset, h->current_ns, sl);
        else if (tid == 0x02)
            process_pmt(h, pid, pkt + offset, h->current_ns, sl);
    } else {
        h->live->crc_error.count++;
        // SILENT fail to avoid log flooding, we know the sample is good
    }
}

static void process_pat(tsa_handle_t* h, const uint8_t* p, uint64_t now, size_t max_len) {
    (void)max_len;
    h->seen_pat = true;
    h->last_pat_ns = now;
    uint16_t sl = (((p[1] & 0x0F) << 8) | p[2]) + 3;
    int np = (int)(sl - 12) / 4;
    for (int i = 0; i < np; i++) {
        uint16_t pn = (p[8 + i * 4] << 8) | p[9 + i * 4];
        uint16_t ppid = ((p[10 + i * 4] & 0x1F) << 8) | p[11 + i * 4];
        if (pn == 0) continue;
        bool found = false;
        for (uint32_t j = 0; j < h->program_count; j++) {
            if (h->programs[j].program_number == pn) {
                h->programs[j].pmt_pid = ppid;
                found = true;
                break;
            }
        }
        if (!found && h->program_count < MAX_PROGRAMS) {
            h->programs[h->program_count].program_number = pn;
            h->programs[h->program_count].pmt_pid = ppid;
            h->program_count++;
            h->pid_is_pmt[ppid] = true;
        }
    }
}

static void process_pmt(tsa_handle_t* h, uint16_t pid, const uint8_t* p, uint64_t now, size_t max_len) {
    (void)p;
    (void)max_len;
    (void)pid;
    h->last_pmt_ns = now;
}

void tsa_psi_process_packet(tsa_handle_t* h, const uint8_t* pkt, const ts_decode_result_t* res) {
    tsa_update_pid_tracker(h, res->pid);
    if (res->pid == 0 || h->pid_is_pmt[res->pid]) {
        tsa_section_filter_push(h, res->pid, pkt, res);
    }
}
