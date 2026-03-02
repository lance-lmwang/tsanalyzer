#!/bin/bash
# TsAnalyzer Pro - 监控栈一键清空重启脚本 (Auto-Purge)

MONITORING_DIR="monitoring"

function cleanup_all() {
    echo "[1/4] 正在强制清空旧数据和容器..."
    cd $MONITORING_DIR
    docker compose down -v > /dev/null 2>&1
    cd ..
    pkill -9 tsa_server > /dev/null 2>&1
    pkill -9 tsp > /dev/null 2>&1
}

function start_stack() {
    echo "[2/4] 启动全新监控栈..."
    cd $MONITORING_DIR
    docker compose up -d > /dev/null 2>&1
    cd ..

    # 等待 Grafana 就绪
    echo "等待 Grafana 服务初始化..."
    until curl -s http://localhost:3000/api/health | grep "ok" > /dev/null; do
        sleep 1
    done
}

function start_data_flow() {
    echo "[3/4] 启动高性能分析引擎 (tsa_server)..."
    ./build/tsa_server > server.log 2>&1 &
    sleep 2

    echo "[4/4] 注入 8 路测试流 (10 Mbps)..."
    SAMPLE_FILE="/tmp/dummy.ts"
    if [ ! -f "$SAMPLE_FILE" ]; then
        dd if=/dev/zero bs=188 count=10000 > "$SAMPLE_FILE" 2>/dev/null
        for i in {0..9999}; do printf '\x47' | dd of="$SAMPLE_FILE" bs=1 seek=$((i*188)) conv=notrunc 2>/dev/null; done
    fi

    for i in {1..8}; do
        ./build/tsp -i 127.0.0.1 -p $((19001 + i - 1)) -l -f "$SAMPLE_FILE" -b 10000000 > /dev/null 2>&1 &
    done
}

function verify() {
    echo "-------------------------------------------------------"
    echo "重启完成！检查结果："
    echo "- Grafana: http://localhost:3000"
    echo "- Metrics: http://localhost:8082/metrics"
    echo "- 看板状态: $(curl -s http://localhost:3000/api/search?type=dash-db | grep -o '"title":"[^"]*"' | head -n 1)"
    echo "- 采集状态: $(curl -s http://localhost:9090/api/v1/targets | grep -o '"health":"up"' | head -n 1)"
    echo "-------------------------------------------------------"
}

cleanup_all
start_stack
start_data_flow
sleep 5
verify
