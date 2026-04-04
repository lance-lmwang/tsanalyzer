#!/usr/bin/env python3
import sys
import json
import matplotlib.pyplot as plt

TS_PACKET_SIZE = 188
PTS_TIMEBASE = 90000

class TSPacket:
    def __init__(self, data):
        self.data = bytearray(data)
        self.pid = ((data[1] & 0x1F) << 8) | data[2]
        self.afc = (data[3] >> 4) & 0x3
    def has_adaptation(self): return self.afc in (2, 3)
    def has_payload(self): return self.afc in (1, 3)
    def has_discontinuity(self):
        if not self.has_adaptation() or self.data[4] < 1: return False
        return (self.data[5] & 0x80) != 0
    def has_pcr(self):
        if not self.has_adaptation() or self.data[4] < 7: return False
        return (self.data[5] & 0x10) != 0
    def read_pcr(self):
        if not self.has_pcr(): return None
        b = self.data
        return ((b[6]<<25)|(b[7]<<17)|(b[8]<<9)|(b[9]<<1)|(b[10]>>7))
    def payload_offset(self):
        if not self.has_payload(): return None
        return (4 + 1 + self.data[4]) if self.has_adaptation() else 4

class PLL:
    def __init__(self): self.stc = 0; self.drift = 0; self.locked = False
    def update(self, p, discontinuity=False):
        if discontinuity or not self.locked: self.stc = p; self.drift = 0; self.locked = True; return
        e = p - self.stc; self.stc += 0.01 * e; self.drift += 0.0001 * e; self.stc += self.drift

class Buf:
    def __init__(self, limit): self.s = 0; self.limit = limit; self.uf = 0; self.of = 0
    def push(self, n): self.s += n; self.of += (self.s > self.limit)
    def pop(self, n):
        if self.s < n: self.uf += 1; self.s = 0
        else: self.s -= n

def parse_pts_dts(p):
    if len(p) < 14 or p[0:3] != b'\x00\x00\x01': return None, None
    flags = p[7]
    pts, dts = None, None
    if (flags & 0x80):
        pts = (((p[9]>>1)&7)<<30)|(p[10]<<22)|((p[11]>>1)<<15)|(p[12]<<7)|(p[13]>>1)
    if (flags & 0x40) and len(p) >= 19:
        dts = (((p[14]>>1)&7)<<30)|(p[15]<<22)|((p[16]>>1)<<15)|(p[17]<<7)|(p[18]>>1)
    if dts is None: dts = pts
    return pts, dts

class IRD:
    def __init__(self, pcr_pid, v_pid, a_pid):
        self.pcr_pid = pcr_pid; self.v_pid = v_pid; self.a_pid = a_pid
        self.pll = PLL(); self.v = Buf(2000000); self.a = Buf(200000) # Increased VBV size
        self.stc_log = []; self.pcr_log = []; self.vlog = []; self.alog = []
        self.v_queue = []; self.a_queue = []
        self.cpb_delay = 0

    def drain_queues(self, stc):
        while self.v_queue and stc >= self.v_queue[0]:
            self.v.pop(TS_PACKET_SIZE)
            self.v_queue.pop(0)
        while self.a_queue and stc >= self.a_queue[0]:
            self.a.pop(TS_PACKET_SIZE)
            self.a_queue.pop(0)

    def feed(self, pkt):
        if pkt.has_pcr() and pkt.pid == self.pcr_pid:
            p = pkt.read_pcr(); self.pll.update(p, pkt.has_discontinuity()); self.pcr_log.append(p)
        else: self.pcr_log.append(None)
        stc = self.pll.stc; self.stc_log.append(stc)

        self.drain_queues(stc)

        po = pkt.payload_offset()
        if po:
            pts, dts = parse_pts_dts(pkt.data[po:])
            if dts is not None:
                delay = pts - dts if pts is not None else 0
                if delay > self.cpb_delay and delay < 2 * PTS_TIMEBASE:
                    self.cpb_delay = delay
                removal_time = dts + self.cpb_delay

                if pkt.pid == self.v_pid:
                    self.v.push(TS_PACKET_SIZE)
                    self.v_queue.append(removal_time)
                    self.v_queue.sort()
                elif pkt.pid == self.a_pid:
                    self.a.push(TS_PACKET_SIZE)
                    self.a_queue.append(removal_time)
                    self.a_queue.sort()

        self.vlog.append(self.v.s); self.alog.append(self.a.s)

def run_single(f, target_muxrate=None):
    # Industrial Default PIDs
    v_pid, a_pid, pcr_pid = 0x100, 0x101, 0x100

    sim = IRD(pcr_pid, v_pid, a_pid)
    file_size = 0
    first_pcr, last_pcr = None, None

    with open(f, 'rb') as fin:
        while True:
            d = fin.read(188)
            if len(d) < 188: break
            file_size += 188
            if d[0] != 0x47: continue
            pkt = TSPacket(d)
            if pkt.pid == pcr_pid and pkt.has_pcr():
                p = pkt.read_pcr()
                if first_pcr is None: first_pcr = p
                last_pcr = p
            sim.feed(pkt)

    # 1. Calculate Overall Muxrate
    measured_muxrate = 0
    if first_pcr is not None and last_pcr is not None and last_pcr > first_pcr:
        duration_sec = (last_pcr - first_pcr) / PTS_TIMEBASE
        measured_muxrate = (file_size * 8) / duration_sec

    rep = {"v_uf": sim.v.uf, "a_uf": sim.a.uf, "v_of": sim.v.of, "a_of": sim.a.of}
    max_err = 0
    for p, s in zip(sim.pcr_log, sim.stc_log):
        if p: max_err = max(max_err, abs(p-s)/PTS_TIMEBASE)
    jitter = max([abs(sim.stc_log[i]-sim.stc_log[i-1]) for i in range(1, len(sim.stc_log))], default=0)/PTS_TIMEBASE

    score = 100
    if rep["v_uf"] or rep["a_uf"]: score -= 50
    if rep["v_of"] or rep["a_of"]: score -= 30
    if max_err > 0.1: score -= 10
    if jitter > 0.05: score -= 10

    # 2. CBR Enforcement Check
    cbr_pass = True
    if target_muxrate:
        deviation = abs(measured_muxrate - target_muxrate) / target_muxrate
        if deviation > 0.05: # 5% Tolerance
            cbr_pass = False

    return {
        "pass": score >= 80 and rep["v_uf"] == 0 and cbr_pass,
        "score": score if cbr_pass else (score - 40),
        "metrics": {
            **rep,
            "pcr_error": max_err,
            "jitter": jitter,
            "measured_muxrate": measured_muxrate,
            "cbr_integrity": "PASS" if cbr_pass else "FAIL"
        }
    }

if __name__ == '__main__':
    all_results = {}
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='+')
    parser.add_argument('--muxrate', type=int, help='Target muxrate in bps')
    args = parser.parse_args()

    for f in args.files:
        all_results[f] = run_single(f, args.muxrate)
    print(json.dumps(all_results, indent=2))
