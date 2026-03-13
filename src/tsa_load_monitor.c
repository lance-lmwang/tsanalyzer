#include "tsa_load_monitor.h"
#include <stdlib.h>
#include <time.h>
tsa_load_monitor_t *tsa_load_monitor_create(float max_capacity_pps) {
    tsa_load_monitor_t *mon = calloc(1, sizeof(tsa_load_monitor_t));
    mon->max_capacity_pps = max_capacity_pps;
    pthread_mutex_init(&mon->lock, NULL);
    return mon;
}
void tsa_load_monitor_add_packets(tsa_load_monitor_t *mon, uint32_t count) {
    if (!mon) return;
    pthread_mutex_lock(&mon->lock);
    mon->total_packets += count;
    pthread_mutex_unlock(&mon->lock);
}
float tsa_load_monitor_get_pps(tsa_load_monitor_t *mon) {
    return 0.0f;
}
float tsa_load_monitor_get_load_factor(tsa_load_monitor_t *mon) {
    return 0.0f;
}
void tsa_load_monitor_destroy(tsa_load_monitor_t *mon) {
    if (!mon) return;
    pthread_mutex_destroy(&mon->lock);
    free(mon);
}
