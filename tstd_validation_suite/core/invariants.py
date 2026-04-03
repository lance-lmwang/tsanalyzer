def check_tstd_invariants(ctx):
    """
    Perform per-step structural invariant checks.
    Raises AssertionError if any physical constraint is violated.
    """
    # 1. Global Token Floor (Strict rate limiting)
    # We allow a tiny epsilon for floating point precision if necessary,
    # but in fixed-point it should be strict.
    assert ctx.global_tokens >= -1e-6, f"Global token inflation/underflow: {ctx.global_tokens}"

    # 2. TBn Buffer Integrity (Standard conformance)
    for pid in ctx.pids:
        assert pid.buffer_level >= 0, f"PID {pid.id} buffer underflow: {pid.buffer_level}"
        assert pid.buffer_level <= pid.bucket_size, f"PID {pid.id} buffer overflow: {pid.buffer_level}"

    # 3. Clock Monotonicity
    assert ctx.v_stc >= ctx.last_stc, f"STC domain regressed: {ctx.v_stc} < {ctx.last_stc}"

    # 4. Bitrate PLL Error Bounding
    # The secondary control loop should never let cumulative error explode
    assert abs(ctx.bitrate_error_integral) < ctx.max_integral_limit, "Bitrate PLL Integral Windup detected"

def validate_step_consistency(prev_ctx, curr_ctx):
    """Verify continuity between discrete steps."""
    delta_bytes = curr_ctx.total_bytes_written - prev_ctx.total_bytes_written
    assert delta_bytes == 188, "Physical emission size mismatch (must be 188 bytes)"
