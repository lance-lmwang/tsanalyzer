#!/bin/bash
# TsAnalyzer Pro - ASCII NOC Monitor (v4.0)
# Fixed Port: 8088

while true; do
    clear
    echo -e "\e[1;36m======================================================================\e[0m"
    echo -e "\e[1;36m   TsAnalyzer Pro - UNIFIED NOC ASCII VIEW (Port 8088) - $(date)  \e[0m"
    echo -e "\e[1;36m======================================================================\e[0m"
    echo -e "\e[1;37m STREAM ID  |  STATUS  |  HEALTH  |  MOSAIC VIEW (8-STREAM CONCURRENCY)\e[0m"
    echo -e "----------------------------------------------------------------------"

    # Fetch Core Metrics
    METRICS=$(curl -s http://localhost:8088/metrics/core)

    if [ -z "$METRICS" ]; then
        echo -e "\e[31m  [ERROR] Backend Offline @ 8088. Attempting to check process...\e[0m"
        ps aux | grep tsa_server | grep -v grep | head -n 1
    else
        # Grid Display for 8 Streams
        for i in {1..8}; do
            ID="ST-$i"
            # Parse metrics
            HEALTH=$(echo "$METRICS" | grep "tsa_system_health_score{stream_id=\"$ID\"}" | awk '{print $2}')
            LOCKED=$(echo "$METRICS" | grep "tsa_system_signal_locked{stream_id=\"$ID\"}" | awk '{print $2}')

            # Default values
            HEALTH=${HEALTH:-0.0}
            LOCKED=${LOCKED:-0}

            # Status Label
            if [ "$LOCKED" == "1" ]; then
                S_LABEL="\e[42m\e[30m LOCKED \e[0m"
                if (( $(echo "$HEALTH > 90" | bc -l) )); then H_COLOR="\e[32m";
                elif (( $(echo "$HEALTH > 70" | bc -l) )); then H_COLOR="\e[33m";
                else H_COLOR="\e[31m"; fi
            else
                S_LABEL="\e[41m\e[37m NO SIG \e[0m"
                H_COLOR="\e[31m"
            fi

            # Mosaic Bar
            BAR=""
            FILL=$(( ${HEALTH%.*} / 5 ))
            for ((j=0; j<20; j++)); do
                if [ $j -lt $FILL ]; then BAR="${BAR}#"; else BAR="${BAR}-"; fi
            done

            printf " %-10s | %b | %b%6.1f%%\e[0m | %b[%-20s]\e[0m\n" "$ID" "$S_LABEL" "$H_COLOR" "$HEALTH" "$H_COLOR" "$BAR"
        done
    fi

    echo -e "----------------------------------------------------------------------"
    echo -e " \e[1;34m[TIP]\e[0m Open big_screen_noc.html for 7-Layer Deep Insight Grid."
    echo -e " \e[1;34m[CMD]\e[0m curl -s http://localhost:8088/api/v1/snapshot?id=ST-1 | jq"

    # Run once if not interactive
    if [[ ! -t 0 ]]; then exit 0; fi
    sleep 1
done
