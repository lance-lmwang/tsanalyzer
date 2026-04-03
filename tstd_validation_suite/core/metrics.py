import numpy as np
import pandas as pd

def pcr_accuracy_ns(df):
    """Calculate maximum PCR phase error in nanoseconds."""
    if "pcr_err" not in df or df["pcr_err"].empty:
        return 0
    # 1 tick = 1/27MHz ≈ 37ns
    return df["pcr_err"].abs().max() * (1000000000 / 27000000)

def bitrate_stability_score(df):
    """Calculate normalized standard deviation of instantaneous bitrate."""
    if "bps" not in df or len(df) < 100:
        return 0
    rolling_std = df["bps"].rolling(100).std()
    return rolling_std.mean() / df["bps"].mean()

def vbv_violation_count(df):
    """Count T-STD buffer overflows and underflows."""
    if "buffer" not in df:
        return 0
    violations = (df["buffer"] < 0) | (df["buffer"] > df["bucket_size"])
    return violations.sum()

def token_conservation_error(df):
    """Verify the conservation of global and PID tokens."""
    if "generated" not in df or "consumed" not in df:
        return 0
    # Formula: Generated - Consumed == Delta(Tokens)
    theoretical_delta = df["generated"] - df["consumed"]
    actual_residual = (df["global_tokens"] + df["sum_pid_tokens"]) - \
                      (df["global_tokens"].iloc[0] + df["sum_pid_tokens"].iloc[0])
    return (theoretical_delta - actual_residual).abs().max()

def oscillation_index(df):
    """Detect periodic switching patterns in program scheduling."""
    if "program_id" not in df:
        return 0
    switches = (df["program_id"].diff() != 0).astype(int).sum()
    return switches / len(df)

def pcr_jitter_spectrum(df):
    """Perform FFT on PCR error to analyze jitter frequency distribution."""
    if "pcr_err" not in df or len(df) < 2:
        return np.array([])
    err_signal = df["pcr_err"].fillna(0).values
    fft_vals = np.fft.rfft(err_signal)
    return np.abs(fft_vals)
