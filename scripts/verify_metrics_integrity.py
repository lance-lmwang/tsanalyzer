import urllib.request
import sys
from collections import defaultdict

def verify():
    print("=== 指标唯一性与完整性自动审计 ===")
    try:
        response = urllib.request.urlopen("http://localhost:8082/metrics")
        content = response.read().decode('utf-8')
    except Exception as e:
        print(f"FAILED: 无法访问 metrics 接口: {e}")
        return False

    lines = content.splitlines()
    metrics_map = defaultdict(list)
    errors = 0

    for line in lines:
        if line.startswith("#") or not line.strip():
            continue

        # 解析指标名和标签
        parts = line.split('{')
        name = parts[0]
        label_part = parts[1].split('}')[0] if len(parts) > 1 else ""

        # 记录该指标名下的所有标签集
        metrics_map[name].append(label_part)

    # 检查核心看板指标的唯一性
    critical_metrics = [
        "tsa_signal_lock_status",
        "tsa_health_score",
        "tsa_rst_encoder_seconds",
        "tsa_essence_video_fps"
    ]

    for m in critical_metrics:
        labels = metrics_map.get(m, [])
        if not labels:
            print(f"[ERROR] 缺失关键指标: {m}")
            errors += 1
            continue

        # 针对每个 stream_id 检查
        stream_ids = defaultdict(int)
        for lp in labels:
            sid = ""
            for pair in lp.split(','):
                if 'stream_id=' in pair:
                    sid = pair.split('=')[1].strip('"')
            if sid:
                stream_ids[sid] += 1

        for sid, count in stream_ids.items():
            if count > 1:
                print(f"[CRITICAL] 指标重复: {m} 在 {sid} 下出现了 {count} 次！")
                errors += 1
            else:
                print(f"[PASS] {m} ({sid}) 唯一性验证通过")

    if errors == 0:
        print(">>> 审计通过：所有核心指标全局唯一且对齐看板。")
        return True
    else:
        print(f">>> 审计失败：发现 {errors} 处严重冲突。")
        return False

if __name__ == "__main__":
    if not verify():
        sys.exit(1)
