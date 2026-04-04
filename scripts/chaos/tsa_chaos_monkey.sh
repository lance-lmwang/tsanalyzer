#!/bin/bash
# TSA Chaos Monkey - Random impairment rotation

INJECTOR="./scripts/tsa_chaos_injector.sh"
SCENARIOS=("loss" "jitter" "compound")

echo "Starting Chaos Monkey... Press Ctrl+C to stop."

while true; do
    SCENARIO=${SCENARIOS[$RANDOM % ${#SCENARIOS[@]}]}
    DURATION=$(( (RANDOM % 60) + 30 )) # 30-90 seconds

    case $SCENARIO in
        loss)
            VAL=$(( (RANDOM % 10) + 1 ))
            $INJECTOR apply loss $VAL
            ;;
        jitter)
            D=$(( (RANDOM % 200) + 50 ))
            J=$(( D / 2 ))
            $INJECTOR apply jitter $D $J
            ;;
        compound)
            L=$(( (RANDOM % 5) + 1 ))
            D=$(( (RANDOM % 100) + 20 ))
            J=$(( D / 2 ))
            $INJECTOR apply compound $L $D $J
            ;;
    esac

    echo "Chaos active for ${DURATION}s..."
    sleep $DURATION

    $INJECTOR reset
    IDLE=$(( (RANDOM % 30) + 10 ))
    echo "Reset. Idling for ${IDLE}s..."
    sleep $IDLE
done
