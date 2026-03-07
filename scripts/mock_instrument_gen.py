import os
#!/usr/bin/env python3
import http.server
import random
import time

STREAMS = [f"stream_{i:03d}" for i in range(20)]

class MockMetricsHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/metrics":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            output = []

            now = time.time()
            for s in STREAMS:
                seed = (hash(s) + int(now/15)) % 100
                is_lost = 1 if seed > 98 else 0
                is_impaired = 1 if (seed > 90 and seed <= 98) else 0

                # TIER 1: EXECUTIVE (New Names)
                status = 0 if is_lost else 1
                output.append(f'tsa_executive_signal_lock{{stream_id="{s}"}} {status}')
                output.append(f'tsa_executive_link_saturation_pct{{stream_id="{s}"}} {85.5 if not is_lost else 0}')
                output.append(f'tsa_executive_srt_buffer_ms{{stream_id="{s}"}} {120 if not is_lost else 0}')
                output.append(f'tsa_stream_health_score{{stream_id="{s}"}} {100 if not (is_lost or is_impaired) else (60 if is_impaired else 0)}')

                # TIER 2: ETR 290 (New Names)
                cc_errs = 500 if is_impaired else (1200 if is_lost else 0)
                output.append(f'tsa_etr290_p1_sync_loss_total{{stream_id="{s}"}} {1 if is_lost else 0}')
                output.append(f'tsa_etr290_p1_pat_missing{{stream_id="{s}"}} {1 if is_lost else 0}')
                output.append(f'tsa_etr290_p1_pmt_missing{{stream_id="{s}"}} {1 if is_lost else 0}')
                output.append(f'tsa_etr290_p1_cc_errors_total{{stream_id="{s}"}} {cc_errs}')
                output.append(f'tsa_etr290_p2_pcr_repetition_errors{{stream_id="{s}"}} {10 if is_impaired else 0}')
                output.append(f'tsa_etr290_p2_pcr_accuracy_errors{{stream_id="{s}"}} {25 if is_impaired else 0}')
                output.append(f'tsa_etr290_p2_crc_errors{{stream_id="{s}"}} {2 if is_impaired else 0}')

                # TIER 3: MUX & SPECTRUM
                total_br = 10000 if not is_lost else 0
                payload_br = 8500 if not is_lost else 0
                stuffing_br = total_br - payload_br
                output.append(f'tsa_mux_physical_bitrate_kbps{{stream_id="{s}", type="physical_srt"}} {total_br + 200}')
                output.append(f'tsa_mux_logical_bitrate_kbps{{stream_id="{s}", type="logical_ts"}} {total_br}')
                output.append(f'tsa_mux_pid_bitrate_kbps{{stream_id="{s}", pid="0x0202", type="video"}} {payload_br * 0.9}')
                output.append(f'tsa_mux_pid_bitrate_kbps{{stream_id="{s}", pid="0x029e", type="audio"}} {payload_br * 0.1}')
                output.append(f'tsa_mux_pid_bitrate_kbps{{stream_id="{s}", pid="0x1FFF", type="stuffing"}} {stuffing_br}')

                # TIER 4: ES VITAL
                output.append(f'tsa_qoe_video_fps{{stream_id="{s}"}} {25.0 if not is_lost else 0}')
                output.append(f'tsa_qoe_av_sync_ms{{stream_id="{s}"}} {random.uniform(-15, 15) if not is_lost else 0}')
                output.append(f'tsa_qoe_freeze_frame_active{{stream_id="{s}"}} {1 if is_impaired and seed > 95 else 0}')
                output.append(f'tsa_qoe_black_screen_active{{stream_id="{s}"}} 0')

            self.wfile.write("\n".join(output).encode())
        else:
            self.send_response(404)
            self.end_headers()

if __name__ == "__main__":
    server_address = ('', 8000)
    print("TsAnalyzer PRO Mock Engine Active on Port 8000")
    http.server.HTTPServer(server_address, MockMetricsHandler).serve_forever()
