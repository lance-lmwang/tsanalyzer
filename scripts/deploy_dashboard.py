import json
import os
import math

# -----------------------------------------------------------------------------
# TsAnalyzer Pro NOC - Three-Plane Appliance Architecture Deployment (v5.5.5)
# -----------------------------------------------------------------------------

DATASOURCE_UID = 'PBFA97CFB590B2093' # Prometheus DS UID

def create_dashboard_base(uid, title, refresh='5s'):
    return {
        'uid': uid,
        'title': title,
        'refresh': refresh,
        'schemaVersion': 36,
        'version': 1,
        'time': { 'from': 'now-15m', 'to': 'now' },
        'graphTooltip': 1,
        'templating': { 'list': [] },
        'panels': [],
        'editable': True
    }

# -----------------------------------------------------------------------------
# PLANE 1: GLOBAL STREAM WALL (Tier 0)
# -----------------------------------------------------------------------------
def deploy_global_wall():
    dash = create_dashboard_base('global-wall', 'TsAnalyzer Pro - GLOBAL STREAM WALL (PLANE 1)')

    dash['panels'].append({
        'title': 'GLOBAL MONITORING MATRIX (CLICK TO FOCUS)',
        'type': 'stat',
        'gridPos': {'h': 12, 'w': 24, 'x': 0, 'y': 4},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{
            'expr': 'tsa_system_health_score',
            'legendFormat': '{{stream_id}}'
        }],
        'fieldConfig': {
            'defaults': {
                'mappings': [
                    {'type': 'range', 'options': {'from': 90, 'to': 100, 'result': {'color': 'green', 'text': 'STABLE'}}},
                    {'type': 'range', 'options': {'from': 70, 'to': 90, 'result': {'color': 'yellow', 'text': 'DEGRADED'}}},
                    {'type': 'range', 'options': {'from': 1, 'to': 70, 'result': {'color': 'red', 'text': 'CRITICAL'}}},
                    {'type': 'value', 'options': {'0': {'color': 'dark-red', 'text': 'LOST'}}}
                ],
                'links': [{
                    'title': 'Jump to ${__series.name} Focus View',
                    'url': '/d/stream-focus?var-stream=${__series.name}',
                    'targetBlank': False
                }]
            }
        },
        'options': {
            'colorMode': 'background',
            'justifyMode': 'center',
            'textMode': 'name',
            'orientation': 'auto'
        }
    })

    dash['panels'].insert(0, {
        'type': 'stat',
        'title': 'FLEET INSTABILITY (%)',
        'gridPos': {'h': 4, 'w': 24, 'x': 0, 'y': 0},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [
            {'expr': '((count(tsa_system_health_score < 100) / count(tsa_system_health_score)) * 100) or on() vector(0)'}
        ],
        'fieldConfig': {
            'defaults': {
                'unit': 'percent',
                'thresholds': {
                    'mode': 'absolute',
                    'steps': [{'color': 'green', 'value': None}, {'color': 'red', 'value': 1}]
                }
            }
        }
    })

    return dash

# -----------------------------------------------------------------------------
# PLANE 2: STREAM FOCUS VIEW (7-Tier Architecture)
# -----------------------------------------------------------------------------
def deploy_stream_focus():
    dash = create_dashboard_base('stream-focus', 'TsAnalyzer Pro - STREAM FOCUS')
    dash['templating']['list'].append({
        'name': 'stream',
        'type': 'query',
        'query': 'label_values(tsa_system_health_score, stream_id)',
        'refresh': 1,
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'label': 'Focus Stream'
    })

    # TIER 1: MASTER CONTROL CONSOLE
    dash['panels'].append({
        'type': 'row', 'title': 'TIER 1: MASTER CONTROL CONSOLE (SIGNAL STATUS)', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 0}, 'collapsed': False
    })
    dash['panels'].append({
        'type': 'stat', 'title': 'SIGNAL PRESENCE', 'gridPos': {'h': 4, 'w': 6, 'x': 0, 'y': 1},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'tsa_system_signal_locked{stream_id="$stream"}'}],
        'fieldConfig': {
            'defaults': {
                'mappings': [{'type': 'value', 'options': {'0': {'text': 'LOST', 'color': 'red'}, '1': {'text': 'LOCKED', 'color': 'green'}}}]
            }
        },
        'options': {'colorMode': 'background', 'justifyMode': 'center'}
    })
    dash['panels'].append({
        'type': 'gauge', 'title': 'SIGNAL FIDELITY', 'gridPos': {'h': 4, 'w': 12, 'x': 6, 'y': 1},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'tsa_system_health_score{stream_id="$stream"}'}],
        'fieldConfig': {'defaults': {'min': 0, 'max': 100, 'unit': 'percent'}}
    })
    dash['panels'].append({
        'type': 'stat', 'title': 'ENGINE DETERMINISM', 'gridPos': {'h': 4, 'w': 6, 'x': 18, 'y': 1},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [
            {'expr': 'tsa_internal_analyzer_drop{stream_id="$stream"}', 'legendFormat': 'Drops'},
            {'expr': 'tsa_worker_slice_overruns{stream_id="$stream"}', 'legendFormat': 'Overruns'}
        ]
    })

    # TIER 2: TRANSPORT & LINK INTEGRITY
    dash['panels'].append({
        'type': 'row', 'title': 'TIER 2: TRANSPORT & LINK INTEGRITY (SRT/MDI)', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 5}, 'collapsed': False
    })
    # ... (Simplified Matrix implementation for brevity in this script)
    metrics = [
        ('Link Capacity', 'tsa_metrology_physical_bitrate_bps', 0, 6, 6),
        ('SRT NAK', 'tsa_srt_nak_count', 6, 6, 6),
        ('SRT RTT', 'tsa_srt_rtt_ms', 12, 6, 6),
        ('MDI DF', 'mdi_df', 18, 6, 6)
    ]
    for label, metric, x, y, w in metrics:
        dash['panels'].append({
            'type': 'stat', 'title': label, 'gridPos': {'h': 3, 'w': w, 'x': x, 'y': y},
            'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
            'targets': [{'expr': f'{metric}{{stream_id="$stream"}}'}]
        })

    # TIER 3: ETR 290 P1
    dash['panels'].append({
        'type': 'row', 'title': 'TIER 3: ETR 290 P1 (CRITICAL COMPLIANCE)', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 9}, 'collapsed': False
    })
    p1_metrics = [
        ('Sync Loss', 'tsa_tr101290_p1_sync_loss', 0, 10),
        ('PAT Error', 'tsa_tr101290_p1_pat_error', 6, 10),
        ('PMT Error', 'tsa_tr101290_p1_pmt_error', 12, 10),
        ('CC Error', 'tsa_tr101290_p1_cc_error', 18, 10)
    ]
    for label, metric, x, y in p1_metrics:
        dash['panels'].append({
            'type': 'stat', 'title': label, 'gridPos': {'h': 3, 'w': 6, 'x': x, 'y': y},
            'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
            'targets': [{'expr': f'{metric}{{stream_id="$stream"}}'}]
        })

    # TIER 4: ETR 290 P2
    dash['panels'].append({
        'type': 'row', 'title': 'TIER 4: ETR 290 P2 (CLOCK & TIMING)', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 13}, 'collapsed': False
    })
    dash['panels'].append({
        'type': 'timeseries', 'title': 'PCR JITTER & ACCURACY', 'gridPos': {'h': 4, 'w': 24, 'x': 0, 'y': 14},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'tsa_metrology_pcr_jitter_ms{stream_id="$stream"}'}]
    })

    # TIER 5: SERVICE PAYLOAD DYNAMICS
    dash['panels'].append({
        'type': 'row', 'title': 'TIER 5: SERVICE PAYLOAD DYNAMICS (MUX)', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 18}, 'collapsed': False
    })
    dash['panels'].append({
        'type': 'timeseries', 'title': 'PID BITRATE REVENUE', 'gridPos': {'h': 15, 'w': 18, 'x': 0, 'y': 19},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'sum by (pid, type) (tsa_metrology_pid_bitrate_bps{stream_id="$stream"})'}],
        'options': {'legend': {'displayMode': 'table', 'placement': 'right'}}
    })

    # TIER 6: ESSENCE QUALITY
    dash['panels'].append({
        'type': 'row', 'title': 'TIER 6: ESSENCE QUALITY & TEMPORAL STABILITY', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 34}, 'collapsed': False
    })
    # (FPS, GOP, AV SYNC)

    # TIER 7: ALARM RECAP
    dash['panels'].append({
        'type': 'row', 'title': 'TIER 7: ALARM RECAP & FORENSIC AUDIT TRAIL', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 44}, 'collapsed': False
    })
    dash['panels'].append({
        'type': 'table', 'title': 'ALARM EVENT LOG', 'gridPos': {'h': 16, 'w': 24, 'x': 0, 'y': 45},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'increase(tsa_tr101290_p1_cc_error{stream_id="$stream"}[1h]) > 0', 'format': 'table'}]
    })

    return dash

# -----------------------------------------------------------------------------
# PLANE 3: FORENSIC REPLAY
# -----------------------------------------------------------------------------
def deploy_forensic_replay():
    dash = create_dashboard_base('forensic-replay', 'TsAnalyzer Pro - FORENSIC REPLAY')
    dash['panels'].append({
        'type': 'text', 'title': 'FORENSIC TIMELINE', 'gridPos': {'h': 6, 'w': 24, 'x': 0, 'y': 0},
        'options': {'content': '## FORENSIC DATA REPLAY\nMillisecond-aligned packet trace for selected event.'}
    })
    return dash

# -----------------------------------------------------------------------------
# MAIN DEPLOYMENT
# -----------------------------------------------------------------------------
def main():
    dashboards = [
        ('tsa_global_wall.json', deploy_global_wall()),
        ('tsa_stream_focus.json', deploy_stream_focus()),
        ('tsa_forensic_replay.json', deploy_forensic_replay())
    ]

    base_path = 'monitoring/grafana/provisioning/dashboards'
    os.makedirs(base_path, exist_ok=True)

    for filename, content in dashboards:
        path = os.path.join(base_path, filename)
        with open(path, 'w') as f:
            json.dump(content, f, indent=2)
        print(f"[PASS] Deployed: {path}")

if __name__ == "__main__":
    main()
