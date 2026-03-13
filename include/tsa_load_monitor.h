#ifndef TSA_LOAD_MONITOR_H
#define TSA_LOAD_MONITOR_H
#include <pthread.h>
#include <stdint.h>
typedef struct {
    uint64_t total_packets;
    float current_pps;
    uint64_t last_check_time_ms;
    pthread_mutex_t lock;
    float max_capacity_pps;
} tsa_load_monitor_t;
tsa_load_monitor_t *tsa_load_monitor_create(float max_capacity_pps);
void tsa_load_monitor_add_packets(tsa_load_monitor_t *mon, uint32_t count);
float tsa_load_monitor_get_pps(tsa_load_monitor_t *mon);
float tsa_load_monitor_get_load_factor(tsa_load_monitor_t *mon);
void tsa_load_monitor_destroy(tsa_load_monitor_t *mon);
#endif
