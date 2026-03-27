#include "tsa_job_manager.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
tsa_job_instance_t *tsa_job_create(const char *job_id) {
    tsa_job_instance_t *job = calloc(1, sizeof(tsa_job_instance_t));
    if (job_id) strncpy(job->job_id, job_id, 63);
    pthread_mutex_init(&job->lock, NULL);
    return job;
}
int tsa_job_set_status(tsa_job_instance_t *job, tsa_job_status_t new_status) {
    if (!job) return -1;
    pthread_mutex_lock(&job->lock);
    job->status = new_status;
    pthread_mutex_unlock(&job->lock);
    return 0;
}
void tsa_job_destroy(tsa_job_instance_t *job) {
    if (!job) return;
    pthread_mutex_destroy(&job->lock);
    free(job);
}
const char *tsa_job_status_to_string(tsa_job_status_t status) {
    return "MOCK";
}
