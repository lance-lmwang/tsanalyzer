void tsa_push_event(tsa_handle_t* h, tsa_event_type_t type, uint16_t pid, uint64_t val) {
    if (!h || !h->event_q) return;
    size_t head = atomic_load_explicit(&h->event_q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&h->event_q->tail, memory_order_acquire);
    if (head - tail >= MAX_EVENT_QUEUE) return;
    size_t idx = head % MAX_EVENT_QUEUE;
    h->event_q->events[idx].type = type;
    h->event_q->events[idx].pid = pid;
    h->event_q->events[idx].timestamp_ns = h->stc_ns;
    h->event_q->events[idx].value = val;
    atomic_store_explicit(&h->event_q->head, head + 1, memory_order_release);

    /* Hierarchical Suppression: If we have sync loss, ignore minor protocol errors for the state machine and webhooks */
    if (type != TSA_EVENT_SYNC_LOSS && !h->signal_lock) {
        return;
    }

    /* Drive the Alert State Machine from events */
    switch(type) {
        case TSA_EVENT_SYNC_LOSS:   tsa_update_alert_state(h, TSA_ALERT_SYNC, true, "SYNC", pid); break;
        case TSA_EVENT_PAT_TIMEOUT: tsa_update_alert_state(h, TSA_ALERT_PAT, true, "PAT", pid); break;
        case TSA_EVENT_PMT_TIMEOUT: tsa_update_alert_state(h, TSA_ALERT_PMT, true, "PMT", pid); break;
        case TSA_EVENT_CC_ERROR:    tsa_update_alert_state(h, TSA_ALERT_CC, true, "CC", pid); break;
        case TSA_EVENT_CRC_ERROR:   tsa_update_alert_state(h, TSA_ALERT_CRC, true, "CRC", pid); break;
        case TSA_EVENT_PCR_REPETITION:
        case TSA_EVENT_PCR_JITTER:  tsa_update_alert_state(h, TSA_ALERT_PCR, true, "PCR", pid); break;
        default: break;
    }
}
