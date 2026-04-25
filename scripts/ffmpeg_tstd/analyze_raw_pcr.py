import sys

def parse_pcr(file_path):
    print(f"Parsing TS file for PCR jumps: {file_path}")
    last_pcr = -1
    packet_count = 0
    jumps = 0
    with open(file_path, "rb") as f:
        while True:
            pkt = f.read(188)
            if len(pkt) < 188:
                break
            packet_count += 1
            if pkt[0] != 0x47:
                continue

            afc = (pkt[3] >> 4) & 3
            if afc == 2 or afc == 3:
                if pkt[4] >= 7 and (pkt[5] & 0x10):
                    pcr_base = (pkt[6] << 25) | (pkt[7] << 17) | (pkt[8] << 9) | (pkt[9] << 1) | (pkt[10] >> 7)
                    pcr_ext = ((pkt[10] & 1) << 8) | pkt[11]
                    pcr = pcr_base * 300 + pcr_ext
                    if last_pcr != -1:
                        delta = pcr - last_pcr
                        if delta < -(1<<32)*300: delta += (1<<33)*300
                        elif delta > (1<<32)*300: delta -= (1<<33)*300

                        if delta < -27000000*0.5 or delta > 27000000*0.5:
                            print(f"PCR JUMP DETECTED at Pkt {packet_count}: {last_pcr/27000000.0:.3f}s -> {pcr/27000000.0:.3f}s (delta: {delta/27000000.0:.3f}s)")
                            jumps += 1
                    last_pcr = pcr
    print(f"Total PCR jumps found: {jumps}")

parse_pcr("/home/lmwang/dev/cae/sample/202508300200_Al-Taawoun_VS_Al-Nassr_2_cut_400M.ts")
