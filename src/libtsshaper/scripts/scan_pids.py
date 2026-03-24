import sys

def scan_pids(file_path):
    packet_size = 188
    pids = {}
    with open(file_path, 'rb') as f:
        while True:
            p = f.read(packet_size)
            if not p: break
            if p[0] != 0x47: continue
            pid = ((p[1] & 0x1F) << 8) | p[2]
            pids[pid] = pids.get(pid, 0) + 1

    print(f"PID Scan for {file_path}:")
    for pid, count in sorted(pids.items()):
        print(f"- PID 0x{pid:04x} ({pid}): {count} packets")

if __name__ == "__main__":
    scan_pids(sys.argv[1])
