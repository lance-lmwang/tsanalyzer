import socket
import threading
import time
import random
import json
import os

# Proxy Config
MAP = {
    20001: 19001, 20002: 19002, 20003: 19003, 20004: 19004
}

CONFIG_FILE = "chaos_config.json"
# Default State
state = {"drop_rates": {"19002": 0.0}} 

def load_config():
    global state
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as f:
                state = json.load(f)
        except:
            pass

def relay(in_port, out_port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', in_port))
    dest = ('127.0.0.1', out_port)
    print(f"CHAOS: Relay {in_port} -> {out_port}")
    
    while True:
        data, addr = sock.recvfrom(4096)
        # Dynamic drop check
        rate = state.get("drop_rates", {}).get(str(out_port), 0.0)
        if rate > 0 and random.random() < rate:
            continue 
        sock.sendto(data, dest)

# Start Relays
for p_in, p_out in MAP.items():
    threading.Thread(target=relay, args=(p_in, p_out), daemon=True).start()

print("=== TsAnalyzer Automation-Friendly Chaos Proxy Active ===")
while True:
    load_config()
    time.sleep(1)
