import json
import os

# v4.3 - Advanced Filtering to handle multi-instance leftovers
dashboard = {
    'uid': 'tsanalyzer-appliance-noc-v4-3',
    'title': 'TsAnalyzer Pro - Appliance NOC (v4.3)',
    'refresh': '1s',
    'version': 430,
    'time': { 'from': 'now-5m', 'to': 'now' },
    'templating': {
        'list': [{
            'name': 'stream_id',
            'type': 'query',
            'query': 'label_values(tsa_health_score{instance="host.docker.internal:8081"}, stream_id)',
            'refresh': 1,
            'multi': True,
            'includeAll': True,
            'allValue': '.*',
            'current': {'text': 'All', 'value': '$__all'},
            'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' }
        }]
    },
    'panels': []
}

# TIER 0: GLOBAL FLEET STATUS
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 0: GLOBAL FLEET STATUS', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 0} })
dashboard['panels'].append({
    'type': 'stat', 'title': 'Live Ingress Bitrate', 'gridPos': {'h': 4, 'w': 12, 'x': 0, 'y': 1},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'sum(tsa_physical_bitrate_bps{instance="host.docker.internal:8081"})', 'legendFormat': 'Total'}],
    'fieldConfig': {'defaults': {'unit': 'bps'}}
})
dashboard['panels'].append({
    'type': 'stat', 'title': 'Active Appliance Streams', 'gridPos': {'h': 4, 'w': 12, 'x': 12, 'y': 1},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'count(tsa_health_score{instance="host.docker.internal:8081"})', 'legendFormat': 'Streams'}],
    'options': {'colorMode': 'background'}
})

# TIER 1: STREAM VITALITY
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 1: STREAM VITALITY', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 5} })
vitals = [
    ('HEALTH', 'tsa_health_score'),
    ('BITRATE', 'tsa_physical_bitrate_bps'),
    ('INTERNAL DROPS', 'tsa_internal_analyzer_drop'),
    ('OVERRUNS', 'tsa_worker_slice_overruns')
]
for i, (t, e) in enumerate(vitals):
    dashboard['panels'].append({
        'type': 'stat', 'title': t, 'gridPos': {'h': 4, 'w': 6, 'x': i*6, 'y': 6},
        'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
        'targets': [{'expr': f'{e}{{instance="host.docker.internal:8081", stream_id=~"$stream_id"}}', 'legendFormat': '{{stream_id}}'}],
        'options': {'colorMode': 'background' if 'HEALTH' in t else 'value'}
    })

# TIER 2: BITRATE HISTORY
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 2: BITRATE HISTORY', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 10} })
dashboard['panels'].append({
    'type': 'timeseries', 'title': 'Per-Stream Bitrate', 'gridPos': {'h': 8, 'w': 24, 'x': 0, 'y': 11},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'tsa_physical_bitrate_bps{instance="host.docker.internal:8081", stream_id=~"$stream_id"}', 'legendFormat': '{{stream_id}}'}],
    'fieldConfig': {'defaults': {'unit': 'bps'}}
})

# TIER 3: FLEET INVENTORY
dashboard['panels'].append({ 'type': 'row', 'title': 'TIER 3: FLEET INVENTORY', 'gridPos': {'h': 1, 'w': 24, 'x': 0, 'y': 19} })
dashboard['panels'].append({
    'type': 'table', 'title': 'Active Inventory', 'gridPos': {'h': 8, 'w': 24, 'x': 0, 'y': 20},
    'datasource': { 'type': 'prometheus', 'uid': 'PBFA97CFB590B2093' },
    'targets': [{'expr': 'tsa_health_score{instance="host.docker.internal:8081"}', 'format': 'table'}],
    'fieldConfig': {'defaults': {'custom': {'displayMode': 'color-background'}}}
})

output_path = 'monitoring/grafana/provisioning/dashboards/tsa_pro_noc.json'
with open(output_path, 'w') as f:
    json.dump(dashboard, f, indent=2)

print(f"✅ Dashboard v4.3 deployed to {output_path}")
