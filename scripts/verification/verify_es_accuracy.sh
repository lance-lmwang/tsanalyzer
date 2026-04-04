#!/bin/bash
set -e

TS_FILE="./sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="../../sample/cctvhd.ts"
[ ! -f "$TS_FILE" ] && TS_FILE="/home/lmwang/dev/sample/cctvhd.ts"

echo "=== Verifying ES Accuracy (GOP/FPS) ==="

# 1. Run in Replay Mode (Offline)
echo "1. Running in Replay mode..."
./build/tsa --mode=replay "$TS_FILE" > /dev/null 2>&1
# Extract first video PID's stats
GOP_REPLAY=$(python3 -c "import json; d=json.load(open('final_metrology.json')); print(next(p['video_metadata']['gop_ms'] for p in d['pids'] if 'video_metadata' in p))")
FPS_REPLAY=$(python3 -c "import json; d=json.load(open('final_metrology.json')); print(next(p['video_metadata']['exact_fps'] for p in d['pids'] if 'video_metadata' in p))")

echo "   Replay GOP: $GOP_REPLAY ms"
echo "   Replay FPS: $FPS_REPLAY"

# 2. Run in Live Mode (Simulated via tsp)
echo "2. Running in Live mode (via tsp)..."
./build/tsp -i 127.0.0.1 -p 19001 -b 10000000 -l -f "$TS_FILE" > /dev/null 2>&1 &
TSP_PID=$!

# Run tsa in live mode for 20s to ensure we get a few GOPs
timeout 20s ./build/tsa --mode=live --udp 19001 --json-report live_metrology.json > /dev/null 2>&1 || true

kill $TSP_PID || true

GOP_LIVE=$(python3 -c "import json; d=json.load(open('live_metrology.json')); print(next(p['video_metadata']['gop_ms'] for p in d['pids'] if 'video_metadata' in p))")
FPS_LIVE=$(python3 -c "import json; d=json.load(open('live_metrology.json')); print(next(p['video_metadata']['exact_fps'] for p in d['pids'] if 'video_metadata' in p))")

echo "   Live GOP: $GOP_LIVE ms"
echo "   Live FPS: $FPS_LIVE"

# 3. Compare
echo "3. Comparing results..."
# Use python for float comparison
python3 -c "
diff_fps = abs($FPS_REPLAY - $FPS_LIVE)
if diff_fps > 0.5:
    print(f'[FAIL] FPS mismatch! Replay={FPS_REPLAY}, Live={FPS_LIVE}')
    exit(1)
if abs($GOP_REPLAY - $GOP_LIVE) > 100: # Allow some jitter in live mode start/stop
    print(f'[FAIL] GOP mismatch! Replay={GOP_REPLAY}, Live={GOP_LIVE}')
    exit(1)
print('[PASS] GOP/FPS consistent across modes.')
"
