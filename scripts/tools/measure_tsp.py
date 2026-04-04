import socket
import time
import sys

UDP_IP = "127.0.0.1"
UDP_PORT = 19006

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.settimeout(1.0)

print(f">>> Listening on {UDP_PORT}...")
total_bytes = 0
start_time = 0

# Skip first few packets to avoid warm-up transients
for _ in range(10):
    try:
        data, addr = sock.recvfrom(2048)
    except:
        pass

print(">>> Starting 10s measurement...")
measure_start = time.time()
while time.time() - measure_start < 10.0:
    try:
        data, addr = sock.recvfrom(2048)
        total_bytes += len(data)
    except socket.timeout:
        break

actual_duration = time.time() - measure_start
bitrate_bps = (total_bytes * 8) / actual_duration

print(f"Total Bytes: {total_bytes}")
print(f"Actual Duration: {actual_duration:.4f}s")
print(f"Measured Bitrate: {bitrate_bps/1000000:.4f} Mbps")
