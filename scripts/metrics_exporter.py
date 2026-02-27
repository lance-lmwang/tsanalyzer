#!/usr/bin/env python3
import http.server
import glob
import os

class MetricsHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/metrics':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            
            # Aggregate all stat files
            output = []
            for path in glob.glob("/tmp/tsa_stream_*.stats"):
                try:
                    with open(path, "r") as f:
                        output.append(f.read())
                except: pass
            
            self.wfile.write("
".join(output).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        return # Silent mode for high perf

if __name__ == "__main__":
    print("[*] Python Metrics Exporter serving on port 8100")
    http.server.HTTPServer(('0.0.0.0', 8100), MetricsHandler).serve_forever()
