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
                
                # Critical Tier Check (7-Tier Model)
                required_tiers = [
                    'SIGNAL STATUS',       # Tier 1
                    'SRT/MDI',             # Tier 2
                    'CRITICAL COMPLIANCE', # Tier 3
                    'CLOCK & TIMING',      # Tier 4
                    'SERVICE PAYLOAD',     # Tier 5
                    'ESSENCE QUALITY',     # Tier 6
                    'ALARM RECAP'          # Tier 7
                ]
                
                for tier in required_tiers:
                    found = any(tier in t for t in titles)
                    if not found:
                        print(f"❌ FAIL: Tier keyword '{tier}' not found in {f}!")
                        return False
    
    print("✅ PASS: All 3 Planes and 7 Tiers are structurally sound.")
    return True

if __name__ == "__main__":
    if not test_dashboard_integrity():
        sys.exit(1)
