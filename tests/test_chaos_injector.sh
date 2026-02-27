#!/bin/bash
# Test Chaos Injector

SCRIPT="./scripts/tsa_chaos_injector.sh"

# Mock tc command
tc() {
    echo "[MOCK TC] $@"
    return 0
}
export -f tc

# Need to tell the script to use our mock tc
# But scripts usually call the real binary.
# I'll create a dummy tc in the path.
mkdir -p .bin
cat <<EOF > .bin/tc
#!/bin/bash
echo "[MOCK TC] \$@"
exit 0
EOF
chmod +x .bin/tc
export PATH=$(pwd)/.bin:$PATH

echo "Testing Scenario A: Network Collapse (Loss)..."
if ! $SCRIPT apply loss 5; then
    echo "FAILED: Scenario A apply"
    exit 1
fi

echo "Testing Scenario B: Severe Jitter..."
if ! $SCRIPT apply jitter 100 50; then
    echo "FAILED: Scenario B apply"
    exit 1
fi

echo "Testing Scenario C: Compound Failure..."
if ! $SCRIPT apply compound 5 100 50; then
    echo "FAILED: Scenario C apply"
    exit 1
fi

echo "Testing Reset..."
if ! $SCRIPT reset; then
    echo "FAILED: Reset"
    exit 1
fi

echo "All tests passed!"
