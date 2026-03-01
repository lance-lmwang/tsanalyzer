#!/bin/bash
# TSA Chaos Injector - TC Scenario Wrapper

INTERFACE=${TSA_CHAOS_IFACE:-lo}

case "$1" in
    apply)
        # First clean up
        tc qdisc del dev $INTERFACE root 2>/dev/null

        case "$2" in
            loss)
                PERCENT=$3
                echo "Applying $PERCENT% loss to $INTERFACE"
                if ! tc qdisc add dev $INTERFACE root netem loss ${PERCENT}%; then
                    echo "FAILED to apply loss"
                    exit 1
                fi
                ;;
            jitter)
                DELAY=$3
                JITTER=$4
                echo "Applying ${DELAY}ms +/- ${JITTER}ms jitter to $INTERFACE"
                if ! tc qdisc add dev $INTERFACE root netem delay ${DELAY}ms ${JITTER}ms 25%; then
                    echo "FAILED to apply jitter"
                    exit 1
                fi
                ;;
            compound)
                LOSS=$3
                DELAY=$4
                JITTER=$5
                echo "Applying $LOSS% loss and ${DELAY}ms +/- ${JITTER}ms jitter to $INTERFACE"
                if ! tc qdisc add dev $INTERFACE root netem loss ${LOSS}% delay ${DELAY}ms ${JITTER}ms 25%; then
                    echo "FAILED to apply compound impairment"
                    exit 1
                fi
                ;;
            *)
                echo "Unknown scenario: $2"
                exit 1
                ;;
        esac
        ;;
    reset)
        echo "Resetting chaos on $INTERFACE"
        tc qdisc del dev $INTERFACE root 2>/dev/null
        ;;
    *)
        echo "Usage: $0 {apply <scenario> [params]|reset}"
        exit 1
        ;;
esac

exit 0
