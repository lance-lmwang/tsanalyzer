import urllib.request, time, sys

def audit():
    print("=== SUSTAINED STABILITY AUDIT (8 STREAMS) ===")
    history = {f"S{i}": [] for i in range(1, 9)}
    for cycle in range(1, 13):
        time.sleep(5)
        try:
            with urllib.request.urlopen("http://127.0.0.1:8080/metrics", timeout=2) as r:
                data = r.read().decode('utf-8')
                out = f"Cycle {cycle:02d}: "
                for i in range(1, 9):
                    sid = f"S{i}"
                    val = 0
                    for line in data.splitlines():
                        if f'tsa_physical_bitrate_bps{{mode="{sid}"}}' in line:
                            val = int(float(line.split()[-1]))
                            break
                    history[sid].append(val)
                    out += f"{sid}:{val/1e6:3.1f}M "
                print(out + "| ALIVE")
        except Exception as e:
            print(f"Cycle {cycle:02d}: [CRASHED] {e}")
            return
    print("\n=== FINAL REPORT ===")
    for sid, rates in history.items():
        avg = sum(rates)/len(rates)
        print(f"Stream {sid}: Avg={avg/1e6:3.2f}Mbps")

if __name__ == "__main__":
    audit()
