import json

dashboard = {
    'uid': 'tsanalyzer-vfinal-master',
    'title': 'TsAnalyzer Pro NOC (v1.0 Ready)',
    'refresh': '1s',
    'version': 300,
    'time': { 'from': 'now-15m', 'to': 'now' },
    'templating': {
        'list': [{
            'name': 'stream_id',
            'type': 'query',
            'query': 'label_values(tsa_srt_rca_rst_network_s, stream_id)',
            'refresh': 1,
            'multi': False,
            'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' }
        }]
    },
    'panels': []
}

# TIER 1: SURVIVAL MASTERS
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 1: SURVIVAL MASTERS', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 0} })
masters = [('MASTER HEALTH', 'tsa_stream_health_score'), ('NETWORK RST', 'tsa_srt_rca_rst_network_s'), ('ENCODER RST', 'tsa_srt_rca_rst_encoder_s'), ('FORWARDING SLA', 'tsa_stream_forwarding_sla_pct')]
for i, (t, e) in enumerate(masters):
    dashboard['panels'].append({
        'type': 'stat', 'title': t, 'gridPos': {'h': 4, 'w': 6, 'x': i*6, 'y': 1},
        'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
        'targets': [{'expr': f'{e}{{stream_id="$stream_id"}}', 'legendFormat': 'Value'}],
        'options': {'colorMode': 'background', 'justifyMode': 'center'},
        'fieldConfig': {'defaults': {'thresholds': {'mode': 'absolute', 'steps': [{'value': None, 'color': 'red'}, {'value': 80, 'color': 'orange'}, {'value': 95, 'color': 'green'}]}}}
    })

# TIER 2: ES CONTENT (srt_monitor Style)
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 2: ES CONTENT AUDIT', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 5} })
es = [('GOP Cadence', 'tsa_srt_es_gop_ms/1000', 'yellow'), ('Frame Rate (FPS)', 'tsa_srt_es_video_fps', 'cyan'), ('AV-Sync Offset', 'tsa_srt_es_av_sync_ms', 'white')]
for i, (t, e, c) in enumerate(es):
    dashboard['panels'].append({
        'type': 'timeseries', 'title': t, 'gridPos': {'h': 6, 'w': 8, 'x': i*8, 'y': 6},
        'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
        'targets': [{'expr': f'{e}{{stream_id="$stream_id"}}', 'legendFormat': 'Value'}],
        'fieldConfig': {'defaults': {'custom': {'lineWidth': 2}, 'color': {'fixedColor': c, 'mode': 'fixed'}}}
    })

# TIER 2.5: BITRATE METROLOGY
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 2.5: BITRATE METROLOGY', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 12} })
dashboard['panels'].append({
    'type': 'stat', 'title': 'Total Bitrate', 'gridPos': {'h': 4, 'w': 12, 'x': 0, 'y': 13},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'tsa_srt_mux_total_bitrate_bps{stream_id="$stream_id"}', 'legendFormat': 'Value'}],
    'fieldConfig': {'defaults': {'unit': 'bps'}}
})
dashboard['panels'].append({
    'type': 'gauge', 'title': 'Stuffing %', 'gridPos': {'h': 4, 'w': 12, 'x': 12, 'y': 13},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'tsa_srt_mux_stuffing_pct{stream_id="$stream_id"}', 'legendFormat': 'Value'}],
    'fieldConfig': {'defaults': {'unit': 'percent', 'min': 0, 'max': 50}}
})

# TIER 3: DIAGNOSTICS MATRIX
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 3: DIAGNOSTICS MATRIX', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 17} })
dashboard['panels'].append({
    'type': 'stat', 'title': 'TR 101 290 Grid', 'gridPos': {'h': 6, 'w': 24, 'x': 0, 'y': 18},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'tsa_srt_mux_cc_errors_total{stream_id="$stream_id"}', 'legendFormat': 'CC'}],
    'options': {'colorMode': 'background', 'orientation': 'horizontal', 'textMode': 'value_and_name'},
    'fieldConfig': {'defaults': {'mappings': [{'type': 'value', 'options': {'0': {'text': 'OK', 'color': 'green'}}}, {'type': 'range', 'options': {'from': 0.001, 'to': 1e9, 'result': {'color': 'red'}}}]}}
})

# TIER 0: FLEET LOBBY
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 0: GLOBAL FLEET LOBBY', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 24} })
dashboard['panels'].append({
    'type': 'table', 'title': 'Fleet Incident Lobby', 'gridPos': {'h': 8, 'w': 24, 'x': 0, 'y': 25},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'tsa_stream_health_score', 'format': 'table'}],
    'fieldConfig': {'defaults': {'custom': {'displayMode': 'color-background', 'align': 'center'}}}
})

with open('monitoring/grafana/provisioning/dashboards/tsa_pro_noc.json', 'w') as f:
    json.dump(dashboard, f, indent=2)
