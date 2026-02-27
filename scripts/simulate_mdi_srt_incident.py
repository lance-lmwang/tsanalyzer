import time

def calculate_health(df, mlr, srt_retransmit, srt_unrecovered, latency=250):
    health = 100
    penalties = 0
    lid_active = False

    # 1. P1 / MDI-MLR Penalty
    if mlr > 0:
        penalties += 40
        lid_active = True
        print(" [!] MDI-MLR Active Penalty: -40")

    # 2. MDI Jitter / Buffer Context
    if df > (0.8 * latency):
        penalties += 15
        print(f" [!] MDI-DF ({df}ms) > 80% Buffer ({latency}ms): -15")

    # 3. SRT Retransmit
    if srt_retransmit > 15:
        penalties += 10
        print(f" [!] SRT Retransmit ({srt_retransmit}%): -10")

    # 4. SRT Unrecovered
    if srt_unrecovered > 0:
        penalties += 30
        lid_active = True
        print(" [!] SRT Unrecovered Loss: -30")

    score = health - penalties
    
    # 5. The Lid Rule
    if lid_active:
        score = min(score, 60)
        print(f" [LID] Score Capped at 60")

    return max(score, 0)

def simulate():
    print("=== TsAnalyzer Pro: MDI + SRT Causal Simulation ===")
    print("Config: SRT Latency = 250ms")
    
    # Timeline: Time (s), MDI-DF (ms), MDI-MLR (pkts/s), Retransmit (%), Unrecovered
    timeline = [
        (0,  30,  0, 2,  0),  # T=0: Healthy
        (5,  80,  0, 5,  0),  # T=5: Jitter starts
        (10, 210, 0, 12, 0),  # T=10: Jitter hits 84% (Pre-alert zone)
        (15, 260, 2, 25, 1),  # T=15: Buffer overflowed! Loss begins.
        (20, 150, 0, 5,  0),  # T=20: Recovery starts
    ]

    for t, df, mlr, ret, unr in timeline:
        print(f"\n--- T = {t}s ---")
        print(f"Metrics: DF={df}ms, MLR={mlr}, Retransmit={ret}%, Unrecovered={unr}")
        score = calculate_health(df, mlr, ret, unr)
        
        status = "OPTIMAL (GREEN)"
        if score < 70: status = "EMERGENCY (RED)"
        elif score < 90: status = "CRITICAL (ORANGE)"
        
        print(f"RESULT >> Health Score: {score} | NOC Status: {status}")
        if t == 10:
            print(" [INSIGHT] PREDICTIVE ALERT: MDI-DF is approaching buffer limit. 5s before CC errors!")
        if t == 15:
            print(" [INSIGHT] FATAL LOSS: ARQ failed. Stream impact detected.")

if __name__ == "__main__":
    simulate()
