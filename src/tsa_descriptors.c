#include "tsa_descriptors.h"

#include <string.h>

#include "tsa_internal.h"

#define MAX_HANDLERS 256
static tsa_descriptor_handler_t handlers[MAX_HANDLERS];

void tsa_descriptors_register_handler(uint8_t tag, tsa_descriptor_handler_t handler) {
    handlers[tag] = handler;
}

void tsa_descriptors_process(struct tsa_handle *h, uint16_t pid, const uint8_t *data, size_t max_len,
                             uint8_t *stream_type) {
    uint8_t tag = data[0];
    uint8_t len = data[1];
    if ((size_t)2 + len > max_len) return;
    if (handlers[tag]) {
        handlers[tag](h, pid, tag, &data[2], len, stream_type);
    }
}

// Specialized Handlers

static tsa_program_model_t *find_program(struct tsa_handle *h, uint16_t pid) {
    if (!h) return NULL;
    tsa_ts_model_t *m = &h->ts_model;

    // First, try direct PID lookup (for PMT PIDs)
    if (pid < 8192) {
        int16_t idx = m->pid_to_program_idx[pid];
        if (idx >= 0 && idx < (int16_t)m->program_count) {
            return &m->programs[idx];
        }
    }

    // Second, search by program number (for SDT Service IDs)
    for (uint32_t i = 0; i < m->program_count; i++) {
        if (m->programs[i].program_number == pid) {
            return &m->programs[i];
        }
    }

    return NULL;
}

static void trim_spaces(char *s) {
    if (!s) return;
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void handle_service(struct tsa_handle *h, uint16_t pid, uint8_t tag, const uint8_t *data, uint8_t len,
                           uint8_t *stream_type) {
    (void)tag;
    (void)stream_type;
    tsa_program_model_t *p = find_program(h, pid);
    if (!p || len < 3) return;

    uint8_t provider_len = data[1];
    if (provider_len + 2 > len) return;

    size_t copy_prov = (provider_len < 255) ? provider_len : 255;
    memcpy(p->provider_name, &data[2], copy_prov);
    p->provider_name[copy_prov] = '\0';
    trim_spaces(p->provider_name);

    uint8_t service_len = data[2 + provider_len];
    if (provider_len + service_len + 3 > len) return;

    size_t copy_serv = (service_len < 255) ? service_len : 255;
    memcpy(p->service_name, &data[3 + provider_len], copy_serv);
    p->service_name[copy_serv] = '\0';
    trim_spaces(p->service_name);
}

static void handle_lcn(struct tsa_handle *h, uint16_t pid, uint8_t tag, const uint8_t *data, uint8_t len,
                       uint8_t *stream_type) {
    (void)pid;
    (void)tag;
    (void)stream_type;

    // Logical Channel Number Descriptor (0x83) - Typically in NIT
    // Loop over all services in the descriptor
    for (int i = 0; i + 4 <= len; i += 4) {
        uint16_t service_id = (data[i] << 8) | data[i + 1];
        uint16_t lcn = ((data[i + 2] & 0x03) << 8) | data[i + 3];
        tsa_program_model_t *p = find_program(h, service_id);
        if (p) {
            p->lcn = lcn;
        }
    }
}

static void handle_ac3(struct tsa_handle *h, uint16_t pid, uint8_t tag, const uint8_t *data, uint8_t len,
                       uint8_t *stream_type) {
    (void)h;
    (void)pid;
    (void)tag;
    (void)data;
    (void)len;
    // Only update if it's currently signaled as private data
    if (stream_type && *stream_type == 0x06) {
        *stream_type = TSA_TYPE_AUDIO_AC3;
    }
}

static void handle_subtitle(struct tsa_handle *h, uint16_t pid, uint8_t tag, const uint8_t *data, uint8_t len,
                            uint8_t *stream_type) {
    (void)h;
    (void)pid;
    (void)tag;
    (void)data;
    (void)len;
    if (stream_type && *stream_type == 0x06) {
        *stream_type = 0x59;  // Subtitle
    }
}

static void handle_teletext(struct tsa_handle *h, uint16_t pid, uint8_t tag, const uint8_t *data, uint8_t len,
                            uint8_t *stream_type) {
    (void)h;
    (void)pid;
    (void)tag;
    (void)data;
    (void)len;
    if (stream_type && *stream_type == 0x06) {
        *stream_type = 0x56;  // Teletext
    }
}

void tsa_descriptors_init(void) {
    memset(handlers, 0, sizeof(handlers));

    // Register Service/LCN handlers
    tsa_descriptors_register_handler(0x48, handle_service);
    tsa_descriptors_register_handler(0x83, handle_lcn);

    // Register AC3/EAC3 handlers
    tsa_descriptors_register_handler(0x6a, handle_ac3);  // AC3
    tsa_descriptors_register_handler(0x7a, handle_ac3);  // EAC3
    tsa_descriptors_register_handler(0x81, handle_ac3);  // AC3-Legacy

    // Register DVB Subtitle
    tsa_descriptors_register_handler(0x59, handle_subtitle);

    // Register Teletext
    tsa_descriptors_register_handler(0x56, handle_teletext);
}
