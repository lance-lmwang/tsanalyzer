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
    dash = create_dashboard_base('global-wall', 'TsAnalyzer Pro - GLOBAL STREAM WALL')
    
    dash['panels'].append({
        'type': 'stat',
        'title': '',
        'gridPos': {'h': 3, 'w': 1, 'x': 0, 'y': 0},
        'repeat': 'stream_id',
        'repeatDirection': 'h',
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{
            'expr': 'max_over_time(dominant_failure_domain[5m])',
            'legendFormat': '{{stream_id}}'
        }],
        'fieldConfig': {
            'defaults': {
                'mappings': [
                    {'type': 'value', 'options': {'0': {'text': '🟢', 'color': 'green', 'index': 0}}},
                    {'type': 'value', 'options': {'1': {'text': '🟡', 'color': 'yellow', 'index': 1}}},
                    {'type': 'value', 'options': {'2': {'text': '🔴', 'color': 'red', 'index': 2}}},
                    {'type': 'value', 'options': {'3': {'text': '🚨', 'color': 'dark-red', 'index': 3}}}
                ],
                'thresholds': {
                    'mode': 'absolute',
                    'steps': [{'value': None, 'color': 'transparent'}]
                }
            }
        },
        'options': {
            'colorMode': 'background',
            'justifyMode': 'center',
            'textMode': 'name',
        },
        'links': [{
            'title': 'Focus View',
            'url': '/d/stream-focus?var-stream=${__value.text}&from=${__from}&to=${__to}',
            'targetBlank': False
        }]
    })

    dash['panels'].insert(0, {
        'type': 'stat',
        'title': 'FLEET INSTABILITY',
        'gridPos': {'h': 4, 'w': 24, 'x': 0, 'y': 0},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [
            {'expr': 'sum(dominant_failure_domain > 0) / count(dominant_failure_domain) * 100', 'legendFormat': 'Instability %'}
        ],
        'fieldConfig': {'defaults': {'unit': 'percent', 'decimals': 1}}
    })

    return dash

# -----------------------------------------------------------------------------
# PLANE 2: STREAM FOCUS VIEW (Tiers 1–5)
# -----------------------------------------------------------------------------
def deploy_stream_focus():
    dash = create_dashboard_base('stream-focus', 'TsAnalyzer Pro - STREAM FOCUS')
    dash['templating']['list'].append({
        'name': 'stream',
        'type': 'query',
        'query': 'label_values(tsa_health_score, stream_id)',
        'refresh': 1,
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'label': 'Focus Stream'
    })

    # TIER 1: FAILURE DOMAIN
    dash['panels'].append({
        'type': 'stat', 'title': 'FAILURE DOMAIN', 'gridPos': {'h': 4, 'w': 10, 'x': 2, 'y': 0},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'avg_over_time(dominant_failure_domain{stream_id="$stream"}[15s])'}],
        'fieldConfig': {
            'defaults': {
                'mappings': [
                    {'type': 'value', 'options': {'0': {'text': '✅ SIGNAL OPTIMAL', 'color': 'green'}}},
                    {'type': 'value', 'options': {'1': {'text': '⚠️ NETWORK IMPAIRMENT', 'color': 'yellow'}}},
                    {'type': 'value', 'options': {'2': {'text': '☢️ ENCODER INSTABILITY', 'color': 'orange'}}},
                    {'type': 'value', 'options': {'3': {'text': '🚨 MULTI-CAUSAL CRITICAL', 'color': 'red'}}}
                ]
            }
        },
        'options': {'colorMode': 'background', 'justifyMode': 'center'}
    })

    # TIER 2: DIAGNOSTICS MATRIX
    metrics = [
        ('Link Capacity', 'tsa_link_capacity', 6, 4),
        ('SRT NAK', 'tsa_srt_nak_count', 6, 5),
        ('Buffer Margin', 'tsa_buffer_margin_ms', 6, 6),
        ('SRT RTT', 'tsa_srt_rtt_ms', 6, 7),
        ('Sync Loss', 'tsa_sync_loss_count', 11, 4),
        ('PAT Error', 'tsa_pat_error_count', 11, 5),
        ('PMT Error', 'tsa_pmt_error_count', 11, 6),
        ('CC Error', 'tsa_cc_error_count', 11, 7)
    ]
    for label, metric, x, y in metrics:
        dash['panels'].append({
            'type': 'stat', 'title': label, 'gridPos': {'h': 2, 'w': 3, 'x': x, 'y': y},
            'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
            'targets': [{'expr': f'{metric}{{stream_id="$stream"}}'}],
            'options': {'colorMode': 'background', 'justifyMode': 'center'}
        })

    # TIER 3: BITRATE & ESSENCE
    dash['panels'].append({
        'type': 'timeseries', 'title': 'BITRATE ENVELOPE', 'gridPos': {'h': 10, 'w': 15, 'x': 0, 'y': 8},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [
            {'expr': 'avg_over_time(tsa_physical_bitrate_bps{stream_id="$stream"}[$__interval])', 'legendFormat': 'Avg'},
            {'expr': 'max_over_time(tsa_physical_bitrate_bps{stream_id="$stream"}[$__interval])', 'legendFormat': 'Max'},
            {'expr': 'min_over_time(tsa_physical_bitrate_bps{stream_id="$stream"}[$__interval])', 'legendFormat': 'Min'}
        ],
        'fieldConfig': {'defaults': {'unit': 'bps', 'custom': {'fillOpacity': 20, 'gradientMode': 'opacity'}}}
    })

    dash['panels'].append({
        'type': 'timeseries', 'title': 'AV SYNC / GOP', 'gridPos': {'h': 10, 'w': 9, 'x': 15, 'y': 8},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [
            {'expr': 'tsa_av_sync_ms{stream_id="$stream"}', 'legendFormat': 'AV Sync (ms)'}
        ]
    })

    # TIER 4: PREDICTIVE HORIZON
    dash['panels'].append({
        'type': 'stat', 'title': 'RST SURVIVAL', 'gridPos': {'h': 4, 'w': 11, 'x': 0, 'y': 18},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'tsa_rst_survival_sec{stream_id="$stream"}'}],
        'options': {'textMode': 'value_and_name', 'colorMode': 'value'}
    })
    
    dash['panels'].append({
        'type': 'timeseries', 'title': 'PID INVENTORY', 'gridPos': {'h': 4, 'w': 13, 'x': 11, 'y': 18},
        'datasource': { 'type': 'prometheus', 'uid': DATASOURCE_UID },
        'targets': [{'expr': 'sum by (pid) (tsa_pid_bitrate_bps{stream_id="$stream"})'}]
    })

    # TIER 5: OPERATIONAL AUDIT TRAIL
    dash['panels'].append({
        'type': 'logs', 'title': 'OPERATIONAL AUDIT TRAIL', 'gridPos': {'h': 6, 'w': 24, 'x': 0, 'y': 22},
        'datasource': { 'type': 'loki' },
        'targets': [{'expr': '{stream_id="$stream"}'}]
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
        print(f"✅ Deployed: {path}")

if __name__ == "__main__":
    main()
