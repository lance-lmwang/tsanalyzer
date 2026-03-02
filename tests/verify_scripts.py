import json
import os
import subprocess
import sys

def test_dashboard_integrity():
    print("--- [TEST] Dashboard Generator Integrity ---")
    
    # 1. Run the generator
    try:
        subprocess.run([sys.executable, "scripts/deploy_dashboard.py"], check=True, capture_output=True)
    except Exception as e:
        print(f"❌ FAIL: Script crashed: {e}")
        return False

    base_path = 'monitoring/grafana/provisioning/dashboards'
    files = ['tsa_global_wall.json', 'tsa_stream_focus.json', 'tsa_forensic_replay.json']
    
    for f in files:
        path = os.path.join(base_path, f)
        if not os.path.exists(path):
            print(f"❌ FAIL: Missing file {f}")
            return False
        
        with open(path, 'r') as jf:
            data = json.load(jf)
            
            # Assert Plane Existence
            if f == 'tsa_stream_focus.json':
                titles = [p.get('title', '') for p in data.get('panels', [])]
                
                # Critical Tier Check
                required_tiers = [
                    'FAILURE DOMAIN',      # Tier 1
                    'Link Capacity',       # Tier 2
                    'CC Error',            # Tier 2
                    'BITRATE ENVELOPE',    # Tier 3
                    'RST SURVIVAL',        # Tier 4
                    'OPERATIONAL AUDIT'    # Tier 5
                ]
                
                for tier in required_tiers:
                    found = any(tier in t for j in titles for t in [j] if tier in t)
                    if not found:
                        print(f"❌ FAIL: Tier '{tier}' was deleted in {f}!")
                        return False
    
    print("✅ PASS: All 3 Planes and 5 Tiers are structurally sound.")
    return True

if __name__ == "__main__":
    if not test_dashboard_integrity():
        sys.exit(1)
