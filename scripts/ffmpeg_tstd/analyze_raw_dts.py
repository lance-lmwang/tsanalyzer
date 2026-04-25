import sys

def parse_ts(file_path):
    print(f"Parsing TS file: {file_path}")
    last_dts = {}
    packet_count = 0
    with open(file_path, "rb") as f:
        while True:
            pkt = f.read(188)
            if len(pkt) < 188:
                break
            packet_count += 1
            if pkt[0] != 0x47:
                continue

            pid = ((pkt[1] & 0x1f) << 8) | pkt[2]
            pusi = (pkt[1] & 0x40) != 0

            if pusi:
                # check adaptation field
                afc = (pkt[3] >> 4) & 3
                payload_offset = 4
                if afc == 2 or afc == 3:
                    payload_offset += 1 + pkt[4]

                if payload_offset + 9 <= 188 and pkt[payload_offset] == 0 and pkt[payload_offset+1] == 0 and pkt[payload_offset+2] == 1:
                    stream_id = pkt[payload_offset+3]
                    if stream_id >= 0xC0 and stream_id <= 0xEF: # Audio or Video
                        pts_dts_flags = (pkt[payload_offset+7] >> 6) & 3
                        if pts_dts_flags == 2 or pts_dts_flags == 3: # PTS or PTS+DTS
                            p = payload_offset + 9
                            pts = ((pkt[p] & 0x0E) << 29) | (pkt[p+1] << 22) | ((pkt[p+2] & 0xFE) << 14) | (pkt[p+3] << 7) | ((pkt[p+4] & 0xFE) >> 1)
                            dts = pts
                            if pts_dts_flags == 3:
                                p += 5
                                dts = ((pkt[p] & 0x0E) << 29) | (pkt[p+1] << 22) | ((pkt[p+2] & 0xFE) << 14) | (pkt[p+3] << 7) | ((pkt[p+4] & 0xFE) >> 1)

                            if pid in last_dts:
                                delta = dts - last_dts[pid]
                                # handle 33-bit wrap
                                if delta < -(1<<32): delta += (1<<33)
                                elif delta > (1<<32): delta -= (1<<33)

                                if delta < -90000 or delta > 90000*0.5:
                                    print(f"JUMP DETECTED at TS Pkt {packet_count}, PID 0x{pid:04x}: {last_dts[pid]/90000.0:.3f}s -> {dts/90000.0:.3f}s (delta: {delta/90000.0:.3f}s)")
                            last_dts[pid] = dts

parse_ts("/home/lmwang/dev/cae/sample/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_400M.ts")
