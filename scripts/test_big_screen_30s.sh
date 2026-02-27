#!/bin/bash
# TsAnalyzer Pro - 30s Big Screen Monitoring Test

SAMPLE_FILE="/tmp/dummy.ts"
if [ ! -f "$SAMPLE_FILE" ]; then
    dd if=/dev/zero bs=188 count=10000 > "$SAMPLE_FILE"
    for i in {0..9999}; do printf '\x47' | dd of="$SAMPLE_FILE" bs=1 seek=$((i*188)) conv=notrunc 2>/dev/null; done
fi

function cleanup() {
    pkill -9 tsa_server
    pkill -9 tsp
}
trap cleanup EXIT

echo "=== [1/2] 启动监控服务器 (Port 8080) ==="
./build/tsa_server > server.log 2>&1 &
sleep 2

echo "=== [2/2] 注入 8 路高清流 (10 Mbps 每路) ==="
for i in {1..8}; do
    UDP_PORT=$((19001 + i - 1))
    ./build/tsp -i 127.0.0.1 -p $UDP_PORT -l -f "$SAMPLE_FILE" -b 10000000 > /dev/null 2>&1 &
done

echo "----------------------------------------------------"
echo "监控已开启! 你可以访问: http://localhost:8080/metrics"
echo "正在模拟大屏抓取数据 (持续 30 秒)..."
echo "----------------------------------------------------"

for i in {1..15}; do
    echo "[$(date +%H:%M:%S)] 抓取测试..."
    curl -s http://localhost:8080/metrics | grep "tsa_total_packets" | head -n 4
    sleep 2
done

echo "测试结束。"
