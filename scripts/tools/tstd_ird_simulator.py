#!/usr/bin/env python3
import sys
import random
import math

# ==============================================================
# MPEG-TS & T-STD Constants
# ==============================================================
TS_PACKET_SIZE = 188
PTS_TIMEBASE = 90000

# ==============================================================
# PCR PLL (Simulates Decoder System Time Clock Recovery)
# ==============================================================
class PCRPLL:
    def __init__(self):
        self.stc = 0.0
        self.drift = 0.0
        self.locked = False

    def update(self, pcr, discontinuity=False):
        if discontinuity or not self.locked:
            self.stc = pcr
            self.drift = 0.0
            self.locked = True
            return

        error = pcr - self.stc
        alpha = 0.01
        beta = 0.0001
        self.stc += alpha * error
        self.drift += beta * error
        self.stc += self.drift

# ==============================================================
# T-STD Buffer (Simulates Physical Decoding Buffers)
# ==============================================================
class TSTDBuffer:
    def __init__(self, max_size):
        self.size = 0
        self.max_size = max_size
        self.overflow = 0
        self.underflow = 0

    def push(self, size):
        self.size += size
        if self.size > self.max_size:
            self.overflow += 1

    def pop(self, size):
        if self.size < size:
            self.underflow += 1
            self.size = 0
        else:
            self.size -= size

# ==============================================================
# Bit-Accurate TS Packet Parser & Mutator
# ==============================================================
class TSPacket:
    def __init__(self, data):
        self.data = bytearray(data)
        self.pid = ((data[1] & 0x1F) << 8) | data[2]
        self.afc = (data[3] >> 4) & 0x3

    def has_adaptation(self): return self.afc in (2, 3)
    def has_payload(self): return self.afc in (1, 3)

    def has_pcr(self):
        if not self.has_adaptation() or self.data[4] < 7: return False
        return (self.data[5] & 0x10) != 0

    def read_pcr(self):
        if not self.has_pcr(): return None
        return ((self.data[6] << 25) | (self.data[7] << 17) |
                (self.data[8] << 9) | (self.data[9] << 1) | (self.data[10] >> 7))

    def write_pcr(self, pcr_base):
        if not self.has_pcr(): return
        self.data[6] = (pcr_base >> 25) & 0xFF
        self.data[7] = (pcr_base >> 17) & 0xFF
        self.data[8] = (pcr_base >> 9) & 0xFF
        self.data[9] = (pcr_base >> 1) & 0xFF
        self.data[10] = ((pcr_base & 0x1) << 7) | (self.data[10] & 0x7F)

    def payload_offset(self):
        if not self.has_payload(): return None
        return (4 + 1 + self.data[4]) if self.has_adaptation() else 4

# ==============================================================
# PES Header DTS Parser
# ==============================================================
def parse_dts(payload):
    if len(payload) < 14 or payload[0:3] != b'\x00\x00\x01': return None
    if (payload[7] & 0x40) == 0: return None
    return (((payload[9] >> 1) & 0x07) << 30 | (payload[10] << 22) |
            ((payload[11] >> 1) << 15) | (payload[12] << 7) | (payload[13] >> 1))

def write_dts(payload, dts):
    payload[9] = (payload[9] & 0xF0) | (((dts >> 30) & 0x07) << 1) | 1
    payload[10] = (dts >> 22) & 0xFF
    payload[11] = (((dts >> 15) & 0x7F) << 1) | 1
    payload[12] = (dts >> 7) & 0xFF
    payload[13] = ((dts & 0x7F) << 1) | 1

# ==============================================================
# IRD Simulator (Validation Core)
# ==============================================================
class IRDSim:
    def __init__(self):
        self.pll = PCRPLL()
        self.video_buf = TSTDBuffer(500000)
        self.audio_buf = TSTDBuffer(200000)
        self.stc_log = []
        self.pcr_log = []
        self.v_buf_log = []
        self.a_buf_log = []
        self.time_axis = []
        self.t = 0

    def feed(self, pid, dts=None, pcr=None, size=188, discontinuity=False):
        self.t += 1
        if pcr is not None:
            self.pll.update(pcr, discontinuity)
            self.pcr_log.append(pcr)
        else:
            self.pcr_log.append(None)

        stc = self.pll.stc
        self.stc_log.append(stc)
        self.time_axis.append(self.t)

        if pid == 'video' or pid == 0x100:
            self.video_buf.push(size)
            if dts is not None and dts <= stc: self.video_buf.pop(size)
        elif pid == 'audio' or pid == 0x101:
            self.audio_buf.push(size)
            if dts is not None and dts <= stc: self.audio_buf.pop(size)

        self.v_buf_log.append(self.video_buf.size)
        self.a_buf_log.append(self.audio_buf.size)

    def report(self):
        return {
            'video_ovf': self.video_buf.overflow, 'video_udf': self.video_buf.underflow,
            'audio_ovf': self.audio_buf.overflow, 'audio_udf': self.audio_buf.underflow
        }

    def plot(self):
        try:
            import matplotlib.pyplot as plt
            plt.figure(figsize=(10, 8))
            plt.subplot(2, 1, 1)
            plt.title("STC vs PCR Recovery")
            plt.plot(self.time_axis, self.stc_log, label="STC (PLL)")
            p_vals = [p if p is not None else float('nan') for p in self.pcr_log]
            plt.scatter(self.time_axis, p_vals, color='red', s=5, label="PCR Samples")
            plt.legend()

            plt.subplot(2, 1, 2)
            plt.title("T-STD Buffer Occupancy")
            plt.plot(self.time_axis, self.v_buf_log, label="Video Buffer")
            plt.plot(self.time_axis, self.a_buf_log, label="Audio Buffer")
            plt.legend()
            plt.tight_layout()
            plt.savefig("tstd_simulation.png")
            print("[INFO] Plot saved to tstd_simulation.png")
        except ImportError:
            print("[WARN] matplotlib not found, skipping plot.")

# ==============================================================
# Simulation Logic
# ==============================================================
def run_logic_test():
    sim = IRDSim()
    jump = 8 * 3600 * 90000
    print("[INFO] Running Logic Simulation with 8h Jump...")

    for i in range(5000):
        # Normal
        v_dts, a_dts, pcr = i, i+1, i+2
        discont = False

        # Inject disaster at step 2000
        if i >= 2000:
            v_dts += jump
            a_dts += jump
            pcr += jump
            if i == 2000: discont = True

        sim.feed('video', dts=v_dts)
        sim.feed('audio', dts=a_dts)
        sim.feed('pcr', pcr=pcr, discontinuity=discont)

    print("\n=== FINAL IRD REPORT ===")
    for k, v in sim.report().items():
        print(f" {k:12}: {v}")
    sim.plot()

if __name__ == '__main__':
    run_logic_test()
