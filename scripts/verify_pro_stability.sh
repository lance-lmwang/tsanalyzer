#!/bin/bash
# TSAnalyzer Pro - Professional Stability & Stress Verification Suite
# Targets: Memory leaks, CPU spikes, FD exhaustion, and API concurrency.

# Configuration
DURATION_SEC=${1:-300}  # Default 5 minutes
STREAMS=16
PORT=8080
METRICS_URL="http://127.0.0.1:$PORT/metrics"
SNAPSHOT_URL="http://127.0.0.1:$PORT/api/v1/snapshot"
LOG_FILE="stability_report.log"
CSV_FILE="stability_metrics.csv"
SAMPLE_FILE="demo_stream.ts"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

function log() {
    echo -e "[$(date +'%Y-%m-%d %H:%M:%S')] $1" | tee -a $LOG_FILE
}

function cleanup() {
    log "${YELLOW}Cleaning up processes...${NC}"
    pkill -9 tsa_server 2>/dev/null
    pkill -9 tsp 2>/dev/null
    rm -f $SAMPLE_FILE
    log "Cleanup complete."
}

trap "cleanup; exit" SIGINT SIGTERM

# 1. Prepare Environment
log "=== TSANALYZER PRO STABILITY SUITE ==="
log "Duration: $DURATION_SEC seconds"
log "Streams:  $STREAMS"

cleanup

# Generate dummy TS data if missing
if [ ! -f "$SAMPLE_FILE" ]; then
    log "Generating mock TS content..."
    python3 -c "with open('$SAMPLE_FILE', 'wb') as f: [f.write(b'\x47' + b'\x00'*(187)) for _ in range(100000)]"
fi

# 2. Build and Start Server
log "Building latest binaries..."
./build.sh > /dev/null 2>&1 || { log "${RED}Build failed!${NC}"; exit 1; }

log "Starting tsa_server..."
./build/tsa_server http://0.0.0.0:$PORT > server_internal.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    log "${RED}Server failed to start! Check server_internal.log${NC}"
    exit 1
fi

# 3. Inject Load (16 Streams)
log "Injecting $STREAMS streams..."
for i in $(seq 1 $STREAMS); do
    UDP_PORT=$((19000 + i))
    SID="STR-$i"
    # Register stream with retry
    MAX_RETRY=3
    for attempt in $(seq 1 $MAX_RETRY); do
        curl -s -X POST -H "Content-Type: application/json" \
             -d "{\"stream_id\":\"$SID\",\"url\":\"udp://127.0.0.1:$UDP_PORT\"}" \
             "http://127.0.0.1:$PORT/api/v1/config/streams" > /dev/null
        
        # Verify creation
        if curl -s "http://127.0.0.1:$PORT/api/v1/config/streams" | grep -q "$SID"; then
            log "Stream $SID created successfully."
            break
        fi
        log "Retry creating $SID (attempt $attempt)..."
        sleep 1
    done
    
    # Start generator
    nohup ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" -b 5000000 > /dev/null 2>&1 &
done
log "Load injection complete. Total streams registered: $(curl -s http://127.0.0.1:$PORT/api/v1/config/streams | grep -o 'STR-' | wc -l)"

# 4. API Stress Generator (Background)
log "Starting API stress workers..."
(
    while true; do
        curl -s "$METRICS_URL" > /dev/null
        curl -s "$SNAPSHOT_URL?id=STR-1" > /dev/null
        sleep 0.1 # 10 requests per second
    done
) &
STRESS_PID=$!

# 5. Monitoring Loop
log "Monitoring started. Results saved to $CSV_FILE"
echo "timestamp,cpu_pct,rss_kb,fd_count,streams_active" > $CSV_FILE

START_TIME=$(date +%s)
END_TIME=$((START_TIME + DURATION_SEC))

while [ $(date +%s) -lt $END_TIME ]; do
    # Get process metrics
    # %CPU, RSS (KB)
    STATS=$(ps -p $SERVER_PID -o %cpu,rss --no-headers)
    CPU=$(echo $STATS | awk '{print $1}')
    RSS=$(echo $STATS | awk '{print $2}')
    
    # File Descriptors
    FD_COUNT=$(lsof -p $SERVER_PID | wc -l)
    
    # Logic check: Active streams in metrics
    ACTIVE_STREAMS=$(curl -s "$METRICS_URL" | grep "tsa_physical_bitrate_bps" | wc -l)
    
    TS=$(date +'%H:%M:%S')
    echo "$TS,$CPU,$RSS,$FD_COUNT,$ACTIVE_STREAMS" >> $CSV_FILE
    
    # Progress indication
    REMAINING=$((END_TIME - $(date +%s)))
    log "Status: CPU:${CPU}% RSS:${RSS}KB FD:${FD_COUNT} Active:${ACTIVE_STREAMS} (Rem: ${REMAINING}s)"
    
    # Immediate Failure Detection
    if [ "$ACTIVE_STREAMS" -lt "$STREAMS" ]; then
        log "${RED}CRITICAL: Stream drop detected! Expected $STREAMS, got $ACTIVE_STREAMS${NC}"
        # We don't exit immediately to see if it recovers, but mark as failure
        STABILITY_FAILED=1
    fi
    
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        log "${RED}CRITICAL: Server CRASHED!${NC}"
        exit 1
    fi
    
    sleep 10
done

# 6. Final Validation
log "Stopping stress workers..."
kill $STRESS_PID 2>/dev/null

log "Running final metrics audit..."
python3 scripts/tsa_metrics_validator.py || STABILITY_FAILED=1

# 7. Summary Report
log "=== STABILITY REPORT ==="
MAX_RSS=$(awk -F, 'NR>1 {if($3>max) max=$3} END {print max}' $CSV_FILE)
AVG_CPU=$(awk -F, 'NR>1 {sum+=$2; count++} END {print sum/count}' $CSV_FILE)
START_RSS=$(awk -F, 'NR==2 {print $3}' $CSV_FILE)
RSS_GROWTH=$((MAX_RSS - START_RSS))

log "Max RSS: ${MAX_RSS} KB"
log "RSS Growth: ${RSS_GROWTH} KB"
log "Avg CPU: ${AVG_CPU}%"

if [ "$RSS_GROWTH" -gt 50000 ]; then
    log "${YELLOW}WARNING: Significant memory growth detected (>50MB)${NC}"
    # Potential leak
fi

if [ "$STABILITY_FAILED" == "1" ]; then
    log "${RED}STABILITY TEST FAILED${NC}"
    exit 1
else
    log "${GREEN}STABILITY TEST PASSED${NC}"
    exit 0
fi
