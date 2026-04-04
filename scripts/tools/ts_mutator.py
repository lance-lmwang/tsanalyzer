#!/usr/bin/env python3
import sys
import random

# ==============================================================
# MPEG-TS Engineering Constants
# ==============================================================
TS_PACKET_SIZE = 188
PTS_TIMEBASE = 90000 # 90kHz

# ==============================================================
# TS Packet Parser (Bit-accurate mutation)
# ==============================================================
class TSPacket:
    def __init__(self, data):
        self.data = bytearray(data)
        self.pid = ((data[1] & 0x1F) << 8) | data[2]
        self.adaptation_field_control = (data[3] >> 4) & 0x3

    def has_adaptation(self):
        return self.adaptation_field_control in (2, 3)

    def has_payload(self):
        return self.adaptation_field_control in (1, 3)

    def has_pcr(self):
        if not self.has_adaptation():
            return False
        af_len = self.data[4]
        if af_len < 7:
            return False
        flags = self.data[5]
        return (flags & 0x10) != 0

    def read_pcr(self):
        if not self.has_pcr():
            return None
        base = (
            (self.data[6] << 25) |
            (self.data[7] << 17) |
            (self.data[8] << 9) |
            (self.data[9] << 1) |
            (self.data[10] >> 7)
        )
        return base

    def write_pcr(self, pcr_base):
        if not self.has_pcr():
            return
        self.data[6]  = (pcr_base >> 25) & 0xFF
        self.data[7]  = (pcr_base >> 17) & 0xFF
        self.data[8]  = (pcr_base >> 9) & 0xFF
        self.data[9]  = (pcr_base >> 1) & 0xFF
        self.data[10] = ((pcr_base & 0x1) << 7) | (self.data[10] & 0x7F)

    def payload_offset(self):
        if not self.has_payload():
            return None
        if self.has_adaptation():
            return 4 + 1 + self.data[4]
        return 4

# ==============================================================
# Simple PES Parser (DTS focus)
# ==============================================================
def parse_pts_dts(payload):
    if len(payload) < 14:
        return None
    if payload[0:3] != b'\x00\x00\x01':
        return None
    flags = payload[7]
    if (flags & 0x40) == 0:
        return None

    dts = (
        ((payload[9] >> 1) & 0x07) << 30 |
        (payload[10] << 22) |
        ((payload[11] >> 1) << 15) |
        (payload[12] << 7) |
        (payload[13] >> 1)
    )
    return dts

def write_dts(payload, dts):
    payload[9]  = (payload[9] & 0xF0) | (((dts >> 30) & 0x07) << 1) | 1
    payload[10] = (dts >> 22) & 0xFF
    payload[11] = (((dts >> 15) & 0x7F) << 1) | 1
    payload[12] = (dts >> 7) & 0xFF
    payload[13] = ((dts & 0x7F) << 1) | 1

# ==============================================================
# TS Mutator (Disaster Generator)
# ==============================================================
class TSMutator:
    def __init__(self, mode):
        self.mode = mode
        self.packet_index = 0

    def mutate(self, pkt: TSPacket):
        self.packet_index += 1

        # Case 1 & 4: PCR Jumps and Jitter
        if pkt.has_pcr():
            pcr = pkt.read_pcr()
            if pcr is not None:
                if self.mode == "pcr_jump" and self.packet_index == 1000:
                    print(f"[MUTATOR] Injecting +8h jump on PCR at packet {self.packet_index}")
                    pcr += 8 * 3600 * PTS_TIMEBASE
                elif self.mode == "pcr_jitter":
                    pcr += random.randint(-20000, 20000)
                pkt.write_pcr(pcr)

        # Case 2 & 3: DTS Jumps and Lag (Video/Audio)
        poff = pkt.payload_offset()
        if poff is not None:
            payload = pkt.data[poff:]
            dts = parse_pts_dts(payload)
            if dts is not None:
                if self.mode == "video_jump" and pkt.pid == 0x100: # Assuming 0x100 is video
                    if self.packet_index > 500:
                        dts += 8 * 3600 * PTS_TIMEBASE
                elif self.mode == "audio_lag" and pkt.pid == 0x101: # Assuming 0x101 is audio
                    dts -= 300 * PTS_TIMEBASE
                write_dts(payload, dts)
                pkt.data[poff:] = payload

        return pkt

def run(input_file, output_file, mode):
    mutator = TSMutator(mode)
    print(f"[INFO] Mutating {input_file} -> {output_file} (Mode: {mode})")

    with open(input_file, 'rb') as fin, open(output_file, 'wb') as fout:
        while True:
            pkt = fin.read(TS_PACKET_SIZE)
            if len(pkt) < TS_PACKET_SIZE:
                break

            tsp = TSPacket(pkt)
            tsp = mutator.mutate(tsp)
            fout.write(tsp.data)

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: ts_mutator.py <input.ts> <output.ts> <mode>")
        print("Modes: pcr_jump, pcr_jitter, video_jump, audio_lag")
        sys.exit(1)

    run(sys.argv[1], sys.argv[2], sys.argv[3])
