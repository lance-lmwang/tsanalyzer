#!/bin/bash
# 实时 CBR 直播流压力测试脚本
# 同时运行 FFmpeg 推流器 + TSDuck 合规性分析器

OUT_DIR="$(pwd)/output"
mkdir -p "$OUT_DIR"
LOG_FFMPEG="$OUT_DIR/ffmpeg_push.log"
LOG_TSDUCK="$OUT_DIR/tsduck_monitor.log"
UDP_URL="udp://239.0.0.1:1234?pkt_size=1316&buffer_size=10000000"

echo "[*] 正在清除旧的监控容器 (如果存在)..."
docker stop ts_monitor > /dev/null 2>&1 || true

echo "[*] 启动监控器 (TSDuck TR101290)..."
docker run -d --rm --name ts_monitor \
  miravallesg/tsduck:v3.21-1693 \
  tsp -I ip 239.0.0.1:1234 --buffer-size 10000000 \
  -P tr101290 \
  -P pcrbitrate --log-file /dev/stderr \
  -O drop > "$LOG_TSDUCK" 2>&1

echo "[*] 启动 FFmpeg CBR 推流器..."
# 确保在当前目录下调用 build/ffmpeg
./build/ffmpeg -y -re -i "../sample/input.mp4" \
      -c:v libwz264 -b:v 2M -maxrate 2M -minrate 2M -bufsize 2M -preset ultrafast \
      -c:a aac -b:a 64k \
      -f mpegts \
      -mpegts_tstd_mode 1 \
      -muxrate 2500000 \
      "$UDP_URL" > "$LOG_FFMPEG" 2>&1 &

FFMPEG_PID=$!
echo "[*] 正在运行测试 (FFmpeg PID: $FFMPEG_PID)..."
echo "[*] 实时监控日志位于: $LOG_TSDUCK"
echo "[*] 按 Ctrl+C 停止测试..."

# Trap SIGINT to cleanup
trap 'echo -e "\n[*] 正在停止测试..."; kill $FFMPEG_PID; docker stop ts_monitor; exit' SIGINT

# 保持脚本运行，同时不断刷新监控日志输出
tail -f "$LOG_TSDUCK"
