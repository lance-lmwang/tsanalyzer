#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_internal.h"

static void process_pat(tsa_handle_t* h, const uint8_t* p, uint64_t now) {
    (void)now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    h->seen_pat = true;
    h->program_count = 0;
    for (int i = 8; i < sl + 3 - 4; i += 4) {
        uint16_t pn = (p[i] << 8) | p[i + 1], pp = ((p[i + 2] & 0x1F) << 8) | p[i + 3];
        if (pn != 0 && h->program_count < MAX_PROGRAMS) {
            h->pid_is_pmt[pp] = true;
            h->live->pid_is_referenced[pp] = true;
            h->programs[h->program_count].pmt_pid = pp;
            h->programs[h->program_count].stream_count = 0;
            h->program_count++;
        }
    }
}

static void process_pmt(tsa_handle_t* h, uint16_t pmt_pid, const uint8_t* p, uint64_t now) {
    (void)now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    uint16_t pcr = ((p[8] & 0x1F) << 8) | p[9];
    int pi = ((p[10] & 0x0F) << 8) | p[11];
    ts_program_info_t* pr = NULL;
    for (uint32_t i = 0; i < h->program_count; i++)
        if (h->programs[i].pmt_pid == pmt_pid) {
            pr = &h->programs[i];
            break;
        }
    if (pr) {
        for (uint32_t i = 0; i < pr->stream_count; i++) {
            h->live->pid_is_referenced[pr->streams[i].pid] = false;
            tsa_reset_pid_stats(h, pr->streams[i].pid);
        }
        pr->pcr_pid = pcr;
        if (!h->live->pid_is_referenced[pcr]) tsa_reset_pid_stats(h, pcr);
        h->live->pid_is_referenced[pcr] = true;
        pr->stream_count = 0;
    }
    for (int i = 12 + pi; i < sl + 3 - 4;) {
        uint8_t ty = p[i];
        uint16_t pid = ((p[i + 1] & 0x1F) << 8) | p[i + 2];
        int es = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
        h->pid_stream_type[pid] = ty;
        if (ty == 0x86) h->pid_is_scte35[pid] = true;
        tsa_precompile_pid_labels(h, pid);
        if (pr && pr->stream_count < MAX_STREAMS_PER_PROG) {
            pr->streams[pr->stream_count].pid = pid;
            pr->streams[pr->stream_count].stream_type = ty;
            pr->stream_count++;
        }
        if (!h->live->pid_is_referenced[pid]) tsa_reset_pid_stats(h, pid);
        h->live->pid_is_referenced[pid] = true;
        i += 5 + es;
    }
}

static void process_nit(tsa_handle_t* h, const uint8_t* p, uint64_t now) {
    (void)now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    if (sl < 10) return;

    uint16_t network_descriptors_length = ((p[8] & 0x0F) << 8) | p[9];
    const uint8_t* d = p + 10;
    for (int j = 0; j < network_descriptors_length;) {
        uint8_t tag = d[j];
        uint8_t len = d[j + 1];
        if (tag == 0x40) { // Network Name Descriptor
            if (len < 255) {
                memcpy(h->network_name, d + j + 2, len);
                h->network_name[len] = '\0';
            }
        }
        j += 2 + len;
    }
}

static void process_sdt(tsa_handle_t* h, const uint8_t* p, uint64_t now) {
    (void)now;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    if (sl < 8) return;

    for (int i = 11; i < sl + 3 - 4;) {
        uint16_t descriptors_loop_length = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
        const uint8_t* d = p + i + 5;
        for (int j = 0; j < descriptors_loop_length;) {
            uint8_t tag = d[j];
            uint8_t len = d[j + 1];
            if (tag == 0x48) {  // Service Descriptor
                uint8_t provider_len = d[j + 3];
                if (provider_len < 255) {
                    memcpy(h->provider_name, d + j + 4, provider_len);
                    h->provider_name[provider_len] = '\0';
                }
                uint8_t service_len = d[j + 4 + provider_len];
                if (service_len < 255) {
                    memcpy(h->service_name, d + j + 5 + provider_len, service_len);
                    h->service_name[service_len] = '\0';
                }
            }
            j += 2 + len;
        }
        i += 5 + descriptors_loop_length;
        break;  // Just process the first service for now
    }
}

static void process_scte35(tsa_handle_t* h, uint16_t pid, const uint8_t* p) {
    // SCTE-35 Splice Info Section
    // p[0]: table_id (0xFC)
    // p[1..2]: section_length
    // p[3]: protocol_version (0)
    // p[4]: encrypted_packet (1 bit), encryption_algorithm (6 bits), pts_adjustment (33 bits)
    // ...
    // p[13]: splice_command_type
    if (p[0] != 0xFC) return;
    int sl = ((p[1] & 0x0F) << 8) | p[2];
    if (sl < 11) return; // Minimal SCTE-35 header is ~11 bytes + CRC

    uint8_t cmd_type = p[13];
    const char* cmd_name = "Unknown";
    switch (cmd_type) {
        case 0x00:
            cmd_name = "Splice Null";
            break;
        case 0x04:
            cmd_name = "Splice Schedule";
            break;
        case 0x05:
            cmd_name = "Splice Insert";
            break;
        case 0x06:
            cmd_name = "Time Signal";
            break;
        case 0x07:
            cmd_name = "Bandwidth Reservation";
            break;
        case 0xff:
            cmd_name = "Private Command";
            break;
    }
    printf("[%s] SCTE-35: Detected %s on PID 0x%04x\n", h->config.input_label[0] ? h->config.input_label : "Unknown",
           cmd_name, pid);
}

void tsa_section_filter_push(tsa_handle_t* h, uint16_t pid, const uint8_t* pkt, const ts_decode_result_t* res) {
    if (!res->has_payload) return;
    ts_section_filter_t* f = &h->pid_filters[pid];
    const uint8_t* payload = pkt + 4 + res->af_len;
    int len = res->payload_len;

    if (res->pusi) {
        uint8_t pointer = payload[0];
        if (pointer + 1 > len) return;

        // Optimization: Fast-path for single-packet sections
        // If pointer is 0 and the whole section fits in this packet, 
        // we can process it directly if it's not currently reassembling.
        if (pointer == 0 && !f->active) {
            int section_len = ((payload[2] & 0x0F) << 8) | payload[3];
            if (section_len + 3 <= len - 1) {
                // Entire section is in this packet
                const uint8_t* full_section = payload + 1;
                
                bool is_long = (full_section[1] & 0x80);
                uint8_t ver = is_long ? ((full_section[5] & 0x3E) >> 1) : 0xFF;
                
                if (is_long && f->seen_before && f->last_version == ver && f->table_id == full_section[0]) {
                    return;
                }

                if (tsa_crc32_check(full_section, section_len + 3) == 0) {
                    if (full_section[0] == 0xFC) {
                        process_scte35(h, pid, full_section);
                    } else {
                        if (pid == 0) process_pat(h, full_section, h->stc_ns);
                        else if (h->pid_is_pmt[pid]) process_pmt(h, pid, full_section, h->stc_ns);
                        else if (pid == 0x11) process_sdt(h, full_section, h->stc_ns);
                        f->last_version = ver;
                        f->seen_before = true;
                        f->table_id = full_section[0];
                    }
                } else {
                    h->live->crc_error.count++;
                    tsa_push_event(h, TSA_EVENT_CRC_ERROR, pid, 0);
                }
                return;
            }
        }

        if (f->active && !f->complete && pointer > 0) {
            int to_copy = (pointer < (f->section_length + 3 - f->assembled_len))
                              ? pointer
                              : (f->section_length + 3 - f->assembled_len);
            memcpy(f->payload + f->assembled_len, payload + 1, to_copy);
            f->assembled_len += to_copy;
            if (f->assembled_len >= f->section_length + 3) f->complete = true;
        }

        payload += 1 + pointer;
        len -= 1 + pointer;
        if (len < 3) {
            f->active = false;
            return;
        }

        f->table_id = payload[0];
        f->section_length = ((payload[1] & 0x0F) << 8) | payload[2];
        if (f->section_length > 4093) {
            f->active = false;
            return;
        }

        f->assembled_len = 0;
        int to_copy = (len < f->section_length + 3) ? len : f->section_length + 3;
        memcpy(f->payload, payload, to_copy);
        f->assembled_len = to_copy;
        f->active = true;
        f->complete = (f->assembled_len >= f->section_length + 3);
    } else if (f->active && !f->complete) {
        int need = (f->section_length + 3) - f->assembled_len;
        int to_copy = (len < need) ? len : need;
        memcpy(f->payload + f->assembled_len, payload, to_copy);
        f->assembled_len += to_copy;
        if (f->assembled_len >= f->section_length + 3) f->complete = true;
    }

    if (f->complete) {
        // Fast Version Check: 
        // For long-form sections (PAT, PMT, SDT), the version_number is in byte 5.
        // Bit format: | reserved (2) | version_number (5) | current_next_indicator (1) |
        bool is_long_section = (f->payload[1] & 0x80);
        uint8_t ver = is_long_section ? ((f->payload[5] & 0x3E) >> 1) : 0xFF;
        
        if (is_long_section && f->seen_before && f->last_version == ver && f->table_id == f->payload[0]) {
            // Skip redundant CRC and processing
            f->active = false;
            f->complete = false;
            return;
        }

        if (tsa_crc32_check(f->payload, f->section_length + 3) == 0) {
            f->version_number = ver;
            if (f->table_id == 0xFC) {
                process_scte35(h, pid, f->payload);
            } else {
                if (pid == 0)
                    process_pat(h, f->payload, h->stc_ns);
                else if (h->pid_is_pmt[pid])
                    process_pmt(h, pid, f->payload, h->stc_ns);
                else if (pid == 0x10)
                    process_nit(h, f->payload, h->stc_ns);
                else if (pid == 0x11)
                    process_sdt(h, f->payload, h->stc_ns);
                f->last_version = ver;
                f->seen_before = true;
            }
        } else {
            h->live->crc_error.count++;
            tsa_push_event(h, TSA_EVENT_CRC_ERROR, pid, 0);
        }
        f->active = false;
        f->complete = false;
    }
}
