#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_descriptors.h"
#include "tsa_internal.h"

static void process_pat(tsa_handle_t* h, const uint8_t* p, uint64_t now) {
    if (h->last_pat_ns > 0) {
        uint64_t diff = now - h->last_pat_ns;
        if (diff > TSA_TR101290_PAT_TIMEOUT_NS) {
            tsa_push_event(h, TSA_EVENT_PAT_TIMEOUT, 0, diff / 1000000);
        }
    }
    h->last_pat_ns = now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    h->seen_pat = true;
    for (int i = 8; i < sl + 3 - 4; i += 4) {
        uint16_t pn = (p[i] << 8) | p[i + 1];
        uint16_t ppid = ((p[i + 2] & 0x1F) << 8) | p[i + 3];
        if (pn != 0) {
            tsa_stream_model_update_program(&h->ts_model, pn, ppid);
            bool found = false;
            for (uint32_t j = 0; j < h->program_count; j++)
                if (h->programs[j].program_number == pn) {
                    h->programs[j].pmt_pid = ppid;
                    found = true;
                    break;
                }
            if (!found && h->program_count < MAX_PROGRAMS) {
                h->programs[h->program_count].program_number = pn;
                h->programs[h->program_count].pmt_pid = ppid;
                h->program_count++;
            }
            if (ppid < TS_PID_MAX) h->pid_is_pmt[ppid] = true;
        }
    }
}

static void process_pmt(tsa_handle_t* h, uint16_t pid, const uint8_t* p, uint64_t now) {
    if (h->last_pmt_ns > 0) {
        uint64_t diff = now - h->last_pmt_ns;
        if (diff > TSA_TR101290_PMT_TIMEOUT_NS) tsa_push_event(h, TSA_EVENT_PMT_TIMEOUT, pid, diff / 1000000);
    }
    h->last_pmt_ns = now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    uint16_t pn = (p[3] << 8) | p[4];
    uint16_t pcr = ((p[8] & 0x1F) << 8) | p[9];
    tsa_program_info_t* pr = NULL;
    for (uint32_t j = 0; j < h->program_count; j++)
        if (h->programs[j].program_number == pn) {
            pr = &h->programs[j];
            break;
        }
    if (!pr && h->program_count < MAX_PROGRAMS) {
        pr = &h->programs[h->program_count++];
        pr->program_number = pn;
    }
    if (pr) {
        tsa_stream_model_update_program(&h->ts_model, pn, pid);
        pr->pmt_pid = pid;
        pr->pcr_pid = pcr;
        if (pcr < TS_PID_MAX) {
            if (!h->live->pid_is_referenced[pcr]) tsa_reset_pid_stats(h, pcr);
            h->live->pid_is_referenced[pcr] = true;
        }
        pr->stream_count = 0;
    }
    int pi = ((p[10] & 0x0F) << 8) | p[11];
    for (int d = 12; d < 12 + pi && d < sl + 3 - 4;) {
        tsa_descriptors_process(h, pid, &p[d], NULL);
        d += 2 + p[d + 1];
    }
    for (int i = 12 + pi; i < sl + 3 - 4;) {
        uint8_t ty = p[i];
        uint16_t spid = ((p[i + 1] & 0x1F) << 8) | p[i + 2];
        int esl = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
        for (int d = i + 5; d < i + 5 + esl;) {
            tsa_descriptors_process(h, spid, &p[d], &ty);
            d += 2 + p[d + 1];
        }
        if (h->es_tracks[spid].stream_type != ty) {
            h->es_tracks[spid].stream_type = ty;
            tsa_stream_model_update_es(&h->ts_model, pid, spid, ty);
            tsa_precompile_pid_labels(h, spid);
        }
        if (ty == 0x86) h->pid_is_scte35[spid] = true;
        if (pr && pr->stream_count < MAX_STREAMS_PER_PROG) {
            pr->streams[pr->stream_count].pid = spid;
            pr->streams[pr->stream_count].stream_type = ty;
            pr->stream_count++;
        }
        if (!h->live->pid_is_referenced[spid]) tsa_reset_pid_stats(h, spid);
        h->live->pid_is_referenced[spid] = true;
        i += 5 + esl;
    }
}

static void process_nit(tsa_handle_t* h, const uint8_t* p, uint64_t now) {
    if (h->last_nit_ns > 0) {
        uint64_t diff = now - h->last_nit_ns;
        if (diff > TSA_TR101290_NIT_TIMEOUT_NS) tsa_push_event(h, TSA_EVENT_NIT_TIMEOUT, 0, diff / 1000000);
    }
    h->last_nit_ns = now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    int ndl = ((p[8] & 0x0F) << 8) | p[9];
    for (int j = 10; j < 10 + ndl && j < sl + 3 - 4;) {
        uint8_t dt = p[j], dl = p[j + 1];
        if (dt == 0x40 && dl > 0) {
            int nl = dl;
            memcpy(h->network_name, p + j + 2, (nl < 255) ? nl : 255);
            h->network_name[(nl < 255) ? nl : 255] = '\0';
        }
        j += 2 + dl;
    }
}

static void process_sdt(tsa_handle_t* h, const uint8_t* p, uint64_t now) {
    if (h->last_sdt_ns > 0) {
        uint64_t diff = now - h->last_sdt_ns;
        if (diff > TSA_TR101290_SDT_TIMEOUT_NS) tsa_push_event(h, TSA_EVENT_SDT_TIMEOUT, 0, diff / 1000000);
    }
    h->last_sdt_ns = now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    for (int i = 11; i < sl + 3 - 4;) {
        uint16_t sid = (p[i] << 8) | p[i + 1];
        int esl = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
        for (int j = i + 5; j < i + 5 + esl;) {
            tsa_descriptors_process(h, sid, &p[j], NULL);
            j += 2 + p[j + 1];
        }
        i += 5 + esl;
    }
}

void tsa_precompile_pid_labels(tsa_handle_t* h, uint16_t pid) {
    if (!h || pid >= TS_PID_MAX) return;
    const char* codec = tsa_get_pid_type_name(h, pid);
    const char* type = "Other";
    if (strcmp(codec, "H.264") == 0 || strcmp(codec, "HEVC") == 0 || strcmp(codec, "MPEG2-V") == 0)
        type = "Video";
    else if (strcmp(codec, "AAC") == 0 || strcmp(codec, "ADTS-AAC") == 0 || strcmp(codec, "AC3") == 0)
        type = "Audio";
    snprintf(h->pid_labels[pid], TSA_LABEL_MAX, "{stream_id=\"%s\",pid=\"0x%04x\",type=\"%s\",codec=\"%s\"}",
             h->config.input_label[0] ? h->config.input_label : "unknown", pid, type, codec);
}

void tsa_reset_pid_stats(tsa_handle_t* h, uint16_t pid) {
    h->pid_seen[pid] = false;
    h->live->pid_packet_count[pid] = 0;
    h->live->pid_bitrate_bps[pid] = 0;
    h->live->pid_cc_errors[pid] = 0;
    h->live->pid_scrambled_packets[pid] = 0;
    h->live->pid_pes_errors[pid] = 0;

    tsa_es_track_t* es = &h->es_tracks[pid];
    uint8_t st = es->stream_type;
    memset(es, 0, sizeof(tsa_es_track_t));
    es->pid = pid;
    es->stream_type = st;
    es->video.gop_min = 0xFFFFFFFF;
    es->last_cc = 0x10;
    es->pes.last_pts_33 = 0x1FFFFFFFFULL;
    es->status = TSA_STATUS_VALID;

    tsa_pcr_track_reset(&h->pcr_tracks[pid]);
}

int16_t tsa_update_pid_tracker(tsa_handle_t* h, uint16_t p) {
    int16_t idx = h->pid_to_active_idx[p];
    if (idx != -1 && idx == (int16_t)h->pid_tracker_count - 1) return idx;
    if (idx == -1) {
        if (h->pid_tracker_count < MAX_ACTIVE_PIDS) {
            h->pid_active_list[h->pid_tracker_count] = p;
            h->pid_to_active_idx[p] = h->pid_tracker_count;
            idx = h->pid_tracker_count++;
        } else {
            int ev = -1;
            for (int i = 0; i < MAX_ACTIVE_PIDS; i++) {
                uint16_t c = h->pid_active_list[i];
                if (c > 1 && !h->pid_is_pmt[c]) {
                    ev = i;
                    break;
                }
            }
            if (ev == -1) ev = 0;
            uint16_t ep = h->pid_active_list[ev];
            h->pid_to_active_idx[ep] = -1;
            tsa_reset_pid_stats(h, ep);
            for (int i = ev; i < MAX_ACTIVE_PIDS - 1; i++) {
                h->pid_active_list[i] = h->pid_active_list[i + 1];
                h->pid_to_active_idx[h->pid_active_list[i]] = i;
            }
            h->pid_active_list[MAX_ACTIVE_PIDS - 1] = p;
            h->pid_to_active_idx[p] = MAX_ACTIVE_PIDS - 1;
            idx = MAX_ACTIVE_PIDS - 1;
        }
        h->pid_seen[p] = true;
        tsa_precompile_pid_labels(h, p);
    } else {
        uint16_t cur = h->pid_active_list[idx];
        for (uint32_t i = (uint32_t)idx; i < h->pid_tracker_count - 1; i++) {
            h->pid_active_list[i] = h->pid_active_list[i + 1];
            h->pid_to_active_idx[h->pid_active_list[i]] = i;
        }
        h->pid_active_list[h->pid_tracker_count - 1] = cur;
        h->pid_to_active_idx[cur] = h->pid_tracker_count - 1;
        idx = h->pid_tracker_count - 1;
    }
    return idx;
}

void tsa_section_filter_push(tsa_handle_t* h, uint16_t pid, const uint8_t* pkt, const ts_decode_result_t* res) {
    if (!res->has_payload) return;
    ts_section_filter_t* f = &h->pid_filters[pid];
    const uint8_t* payload = pkt + 4 + res->af_len;
    int len = res->payload_len;
    if (res->pusi) {
        int pointer = payload[0];
        if (pointer + 1 < len) {
            if (f->active && f->len > 0) {
                memcpy(f->buffer + f->len, payload + 1, pointer);
                f->len += pointer;
                f->complete = true;
            }
            f->active = true;
            f->len = len - 1 - pointer;
            memcpy(f->buffer, payload + 1 + pointer, f->len);
        }
    } else if (f->active) {
        if (f->len + len < 4096) {
            memcpy(f->buffer + f->len, payload, len);
            f->len += len;
        } else
            f->active = false;
    }
    if (f->active && f->len >= 3) {
        int sl = ((f->buffer[1] & 0x0F) << 8) | f->buffer[2];
        if (f->len >= (uint32_t)sl + 3) f->complete = true;
    }
    if (f->complete) {
        uint8_t tid = f->buffer[0];
        uint8_t ver = (f->buffer[5] >> 1) & 0x1F;
        if (!f->seen_before || f->last_ver != ver) {
            if (tsa_crc32_check(f->buffer, (((f->buffer[1] & 0x0F) << 8) | f->buffer[2]) + 3) == 0) {
                if (tid == 0x00)
                    process_pat(h, f->buffer, h->stc_ns);
                else if (tid == 0x02)
                    process_pmt(h, pid, f->buffer, h->stc_ns);
                else if (tid == 0x40)
                    process_nit(h, f->buffer, h->stc_ns);
                else if (tid == 0x42)
                    process_sdt(h, f->buffer, h->stc_ns);
                f->last_ver = ver;
                f->seen_before = true;
            } else {
                h->live->crc_error.count++;
                tsa_push_event(h, TSA_EVENT_CRC_ERROR, pid, 0);
            }
        }
        f->active = false;
        f->complete = false;
    }
}
