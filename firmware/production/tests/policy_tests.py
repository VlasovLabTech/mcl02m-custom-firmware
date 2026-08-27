#!/usr/bin/env python3
"""Executable policy models for boundary and time-state invariants."""

from dataclasses import dataclass


@dataclass
class Timer:
    remaining: int

    def tick(self, state: str, seconds: int = 1) -> None:
        if state == "COOKING":
            self.remaining = max(0, self.remaining - seconds)


def temperature_step(phase: str, target: int, measured: int) -> tuple[str, int]:
    error = target - measured
    if phase == "PREHEAT" and error > 10:
        return phase, 99 if error >= 30 else 77 if error >= 18 else 56
    if phase == "PREHEAT":
        phase = "APPROACH"
    if phase == "APPROACH" and error > 2:
        return phase, min(35, 8 + error * 2)
    return "HOLD", max(0, min(35, round(4 + 2 * error)))


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
    assert profile_sequence([2400, 1500, 0, 300, 0]) == [1, 2, 4]
    assert profile_sequence([0, 0, 0, 0, 0]) == []
    assert active_zero_command("ACTIVE_ZERO") == (0x81, 0, 0)
    assert active_zero_command("PAUSED") == (0x81, 0, 0)
    assert active_zero_command("STOPPED") == (0, 0, 0)
    assert manual_pause_state(2 * 60 * 60 - 1) == "PAUSED"
    assert manual_pause_state(2 * 60 * 60) == "STOPPED"
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
