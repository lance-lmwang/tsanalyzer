#!/usr/bin/env python3
import sys
import re
import statistics
import json

class TSTDTelemetryAnalyzer:
    def __init__(self, log_path):
        self.log_path = log_path
        self.events = []
        # Modern V6 Regex: [T-STD SEC] 180s | In: 478k | InMax: 478k | Out: 706k (Nom:725k, -2.4%) | VBV: 15% | Pace:0.9750 | Tok:397 | Mode:LOCK | Null:21% | Total:1100k
        self.pattern = re.compile(
            r"\[T-STD SEC\]\s+(\d+)s\s+\|\s+In:\s*(\d+)k\s+\|\s+InMax:\s*(\d+)k\s+\|\s+Out:\s*(\d+)k\s+\(Nom:([\d.]+)k,\s*([+-]?[\d.]+)%\)\s+\|\s+VBV:\s*(\d+)%\s+\|\s+Pace:([\d.]+)\s+\|\s+Tok:(-?\d+)\s+\|\s+Mode:(\w+)\s+\|\s+Null:\s*(\d+)%\s+\|\s+Total:\s*(\d+)k"
        )

    def parse(self):
        with open(self.log_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                match = self.pattern.search(line)
                if match:
                    self.events.append({
                        'sec': int(match.group(1)),
                        'in_k': int(match.group(2)),
                        'in_max_k': int(match.group(3)),
                        'out_k': int(match.group(4)),
                        'nom_k': float(match.group(5)),
                        'dev_pct': float(match.group(6)),
                        'vbv_pct': int(match.group(7)),
                        'pace': float(match.group(8)),
                        'mode': match.group(10),
                        'null_pct': int(match.group(11))
                    })
        return len(self.events) > 0

    def generate_report(self):
        if not self.events:
            return "No T-STD events found."

        # Analysis Windows
        outs = [e['out_k'] for e in self.events[5:-2]] # Skip transients
        vbvs = [e['vbv_pct'] for e in self.events[5:-2]]
        modes = [e['mode'] for e in self.events]

        report = {
            "summary": {
                "duration_sampled": len(self.events),
                "mean_bitrate": round(statistics.mean(outs), 2) if outs else 0,
                "max_deviation": round(max([abs(e['dev_pct']) for e in self.events]), 2),
                "avg_vbv": round(statistics.mean(vbvs), 1) if vbvs else 0,
                "max_vbv": max(vbvs) if vbvs else 0,
                "mode_distribution": {m: modes.count(m) for m in set(modes)}
            },
            "health_check": {
                "bitrate_stability": "PASS" if (max([abs(e['dev_pct']) for e in self.events]) < 5.0) else "WARN",
                "vbv_safety": "PASS" if max(vbvs) < 150 else "WARN",
                "clock_drift": "PASS" if "DRIVE FUSE" not in open(self.log_path).read() else "FAIL"
            }
        }
        return report

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 tstd_telemetry_analyzer.py <ffmpeg.log>")
        sys.exit(1)

    analyzer = TSTDTelemetryAnalyzer(sys.argv[1])
    if analyzer.parse():
        report = analyzer.generate_report()
        print(json.dumps(report, indent=4))
    else:
        print("Error: No telemetry found in log.")
        sys.exit(1)
