#!/bin/bash
# TsAnalyzer Pro - 8-Stream PCR-Locked Automated Stability Test (Detailed)

BASE_PORT=8088
BASE_UDP_PORT=19001
STREAMS=8
DURATION=300
CONF="tsa.conf.test"
SAMPLE="../sample/mpts_4prog.ts"

echo "=== [1/4] RESETTING ENVIRONMENT ==="
pkill -9 tsa_server_pro 2>/dev/null
pkill -9 tsp 2>/dev/null
sleep 2

echo "=== [2/4] STARTING SERVER (PRO) WITH $CONF ==="
./build/tsa_server_pro "$CONF" > server_8st.log 2>&1 &
sleep 3

echo "=== [3/4] REGISTERING & INJECTING 8 STREAMS ==="
for i in $(seq 1 $STREAMS); do
    UDP_PORT=$((BASE_UDP_PORT + i - 1))
    STREAM_ID="AUTO_ST_$i"
    curl -s -X POST -H "Content-Type: application/json" \
         -d "{\"id\":\"$STREAM_ID\",\"url\":\"udp://127.0.0.1:$UDP_PORT\"}" \
         "http://localhost:$BASE_PORT/api/v1/streams" > /dev/null
    ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE" -P > /dev/null 2>&1 &
done

echo "=== [4/4] MONITORING STABILITY ($DURATION s) ==="
printf "%-5s | %-6s | %-6s | %-8s | %-6s | %-6s | %-6s | %-10s\n" \
       "Time" "Health" "Drops" "Bitrate" "CC-Err" "PCR-Jit" "PAT-E" "PMT-E"
echo "---------------------------------------------------------------------------------------"

for t in $(seq 5 5 $DURATION); do
    sleep 5
    METRICS=$(curl -s http://localhost:$BASE_PORT/metrics)

    # Aggregate Metrics
    HEALTH=$(echo "$METRICS" | grep "tsa_system_health_score" | awk '{sum+=$2; n++} END {if(n>0) printf "%.1f", sum/n; else print "0.0"}')
    DROPS=$(echo "$METRICS" | grep "tsa_internal_analyzer_drop" | awk '{sum+=$2} END {print int(sum)}')
    BITRATE=$(echo "$METRICS" | grep "tsa_metrology_physical_bitrate_bps" | awk '{sum+=$2} END {printf "%.1f", sum/1000000}')

    # TR101 290 Key Metrics (Aggregated)
    CC_ERR=$(echo "$METRICS" | grep "tsa_tr101290_cc_error_count" | awk '{sum+=$2} END {print int(sum)}')
    PCR_JIT=$(echo "$METRICS" | grep "tsa_metrology_pcr_jitter_p99_ms" | awk '{sum+=$2; n++} END {if(n>0) printf "%.1f", sum/n; else print "0.0"}')
    PAT_ERR=$(echo "$METRICS" | grep "tsa_tr101290_pat_error_count" | awk '{sum+=$2} END {print int(sum)}')
    PMT_ERR=$(echo "$METRICS" | grep "tsa_tr101290_pmt_error_count" | awk '{sum+=$2} END {print int(sum)}')

    printf "%-5ss | %-6s | %-6s | %-6sM | %-6s | %-7s | %-6s | %-6s\n" \
           "$t" "$HEALTH" "$DROPS" "$BITRATE" "$CC_ERR" "$PCR_JIT" "$PAT_ERR" "$PMT_ERR"
done

pkill -9 tsa_server_pro
pkill -9 tsp
