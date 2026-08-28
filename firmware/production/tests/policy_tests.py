#!/usr/bin/env python3
"""Executable policy models for boundary and time-state invariants."""

from dataclasses import dataclass


@dataclass
class Timer:
    remaining: int

    def tick(self, state: str, seconds: int = 1) -> None:
        if state == "COOKING":
            self.remaining = max(0, self.remaining - seconds)


def braking_margin(target: int, rise_4s: int) -> int:
    margin = min(20, 10 + max(0, rise_4s))
    return max(margin, 15) if target >= 170 else margin


def temperature_step(
    phase: str, target: int, measured: int, rise_4s: int = 0
) -> tuple[str, int]:
    error = target - measured
    margin = braking_margin(target, rise_4s)
    if error <= 0:
        return "HOLD", 0
    if phase == "PREHEAT" and error > margin:
        return phase, 99 if error >= 30 else 77 if error >= 18 else 56
    if phase == "PREHEAT":
        phase = "APPROACH"
    if phase == "APPROACH" and error > 2:
        return phase, min(35, 8 + error * 2)
    return "HOLD", max(0, min(35, round(4 + 2 * error)))


def ramp_step(current: int, target: int) -> int:
    if target > current:
        candidate = min(target, current + 10)
        if target >= 56 and current <= 35 and 35 < candidate < 56:
            return 56
        return candidate
    candidate = max(target, current - 10)
    if target <= 35 and current >= 56 and 35 < candidate < 56:
        return 35
    return candidate


def profile_sequence(durations: list[int]) -> list[int]:
    """Return the physical 1-based cells that actually execute."""
    assert len(durations) == 5
    assert sum(durations) <= 5 * 60 * 60
    return [index + 1 for index, duration in enumerate(durations) if duration > 0]


def active_zero_command(state: str) -> tuple[int, int, int]:
    """Model the retained-session command used by zero output and manual Pause."""
    if state in {"ACTIVE_ZERO", "PAUSED"}:
        return 0x81, 0, 0
    if state == "STOPPED":
        return 0, 0, 0
    raise ValueError(state)


def manual_pause_state(elapsed_s: int) -> str:
    return "STOPPED" if elapsed_s >= 2 * 60 * 60 else "PAUSED"


def i2c_display_step(peak: int, hold_until_ms: int, current: int,
                     now_ms: int) -> tuple[int, int, int]:
    """Mirror the display-only peak hold; the engine's current count is untouched."""
    current = min(current, 6)
    if current > 0:
        peak = max(peak, current)
        hold_until_ms = now_ms + 2_000
    elif hold_until_ms and now_ms >= hold_until_ms:
        peak = 0
        hold_until_ms = 0
    return peak, hold_until_ms, max(current, peak)


def picture_policy(
    state: str,
    *,
    transient: str | None = None,
    idle_ms: int = 0,
    sleep_ms: int = 0,
) -> str | None:
    """Model the production display priority and 60-second default Sleep."""
    urgent = {"FAULT": "error", "NO_PAN": "nopan", "COMPLETE": "ready"}
    if state in urgent:
        return urgent[state]
    if transient:
        return transient
    if state == "SLEEP" and sleep_ms < 10_000:
        return "sleep2"
    if state in {"IDLE", "READY"} and idle_ms >= 50_000:
        return "sleep1"
    return None


def run() -> None:
    timer = Timer(270)
    timer.tick("COOKING", 10)
    assert timer.remaining == 260
    timer.tick("PAUSED", 60)
    timer.tick("NO_PAN", 120)
    assert timer.remaining == 260
    timer.tick("COOKING", 260)
    assert timer.remaining == 0

    assert temperature_step("PREHEAT", 100, 20) == ("PREHEAT", 99)
    assert temperature_step("PREHEAT", 100, 75) == ("PREHEAT", 77)
    assert temperature_step("PREHEAT", 100, 89) == ("PREHEAT", 56)
    assert temperature_step("PREHEAT", 100, 91)[1] <= 35
    assert braking_margin(125, 5) == 15
    assert temperature_step("PREHEAT", 125, 110, rise_4s=5) == ("APPROACH", 35)
    assert braking_margin(125, 2) == 12
    assert temperature_step("PREHEAT", 125, 110, rise_4s=2) == ("PREHEAT", 56)
    assert braking_margin(190, 0) == 15
    assert temperature_step("PREHEAT", 190, 175) == ("APPROACH", 35)
    assert temperature_step("HOLD", 58, 59) == ("HOLD", 0)
    assert temperature_step("HOLD", 58, 58) == ("HOLD", 0)
    assert temperature_step("HOLD", 58, 57) == ("HOLD", 6)
    for measured in range(40, 191):
        assert temperature_step("HOLD", 190, measured)[1] <= 35

    at_limit_ms = 0
    saturated = False
    for sample in range(180):
        gear = 35
        at_limit_ms = at_limit_ms + 500 if gear == 35 and 100 - 90 >= 3 else 0
        saturated = at_limit_ms >= 90_000
        if sample < 179:
            assert not saturated
    assert saturated

    assert (35 + 10) // 11 == 4
    assert (36 + 10) // 11 == 4
    assert (55 + 10) // 11 == 5
    assert (56 + 10) // 11 == 6
    assert ramp_step(30, 99) == 56
    assert ramp_step(35, 56) == 56
    assert ramp_step(56, 35) == 35
    assert ramp_step(57, 28) == 35
    assert ramp_step(20, 45) == 30
    assert ramp_step(60, 45) == 50
    assert profile_sequence([2400, 1500, 0, 300, 0]) == [1, 2, 4]
    assert profile_sequence([0, 0, 0, 0, 0]) == []
    assert active_zero_command("ACTIVE_ZERO") == (0x81, 0, 0)
    assert active_zero_command("PAUSED") == (0x81, 0, 0)
    assert active_zero_command("STOPPED") == (0, 0, 0)
    assert manual_pause_state(2 * 60 * 60 - 1) == "PAUSED"
    assert manual_pause_state(2 * 60 * 60) == "STOPPED"
    peak, deadline, shown = i2c_display_step(0, 0, 3, 100)
    assert (peak, deadline, shown) == (3, 2_100, 3)
    peak, deadline, shown = i2c_display_step(peak, deadline, 0, 600)
    assert (peak, deadline, shown) == (3, 2_100, 3)
    peak, deadline, shown = i2c_display_step(peak, deadline, 0, 2_099)
    assert shown == 3
    peak, deadline, shown = i2c_display_step(peak, deadline, 0, 2_100)
    assert (peak, deadline, shown) == (0, 0, 0)
    peak, deadline, shown = i2c_display_step(0, 0, 9, 3_000)
    assert (peak, deadline, shown) == (6, 5_000, 6)
    # A zero-power profile cell is still an ordinary timed cell, not manual Pause.
    assert profile_sequence([3 * 60 * 60, 60, 0, 0, 0]) == [1, 2]
    assert picture_policy("IDLE", idle_ms=49_999) is None
    assert picture_policy("IDLE", idle_ms=50_000) == "sleep1"
    assert picture_policy("SLEEP", sleep_ms=0) == "sleep2"
    assert picture_policy("SLEEP", sleep_ms=9_999) == "sleep2"
    assert picture_policy("SLEEP", sleep_ms=10_000) is None
    assert picture_policy("FAULT", transient="cancel") == "error"
    assert picture_policy("NO_PAN", transient="cooking") == "nopan"
    assert picture_policy("COMPLETE", idle_ms=999_999) == "ready"
    print("POLICY TESTS: PASS")


if __name__ == "__main__":
    run()
