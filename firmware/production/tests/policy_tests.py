#!/usr/bin/env python3
"""Executable policy models for boundary and time-state invariants."""

from dataclasses import dataclass


KNOWN_R20_FAULTS = {
    0x01, 0x0B, 0x0C, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
}


@dataclass(frozen=True)
class StartIncident:
    state: str
    r20: int
    r26: int
    requested_gear: int
    transmitted_gear: int
    transmitted_topology: int
    completed_cycles: int
    bad_cycles: int
    consecutive_bad_cycles: int
    reason: str


@dataclass
class StartProtocol:
    """Deterministic model of the lower Start-confirmation boundary contract."""

    state: str = "ARMED"
    requested_gear: int = 0
    transmitted_gear: int = 0
    transmitted_topology: int = 0
    deadline_ms: int | None = None
    r20: int = 0
    r26: int = 0
    completed_cycles: int = 0
    bad_cycles: int = 0
    consecutive_bad_cycles: int = 0
    no_pan_samples: int = 0
    known_fault_samples: int = 0
    known_fault_value: int = 0
    cookware_limited: bool = False
    warning: int | None = None
    incident: StartIncident | None = None

    def start(self, gear: int = 99) -> None:
        assert self.state == "ARMED" and 0 < gear <= 99
        self.state = "STARTING"
        self.requested_gear = gear
        self.transmitted_gear = min(gear, 10)
        self.transmitted_topology = 0xA1
        self.deadline_ms = None
        self.incident = None

    def heartbeat(self, now_ms: int) -> None:
        if self.state == "STARTING" and self.deadline_ms is None:
            self.deadline_ms = now_ms + 8_000

    def _confirmation_open(self, now_ms: int) -> bool:
        return (
            self.state == "STARTING"
            and self.deadline_ms is not None
            and now_ms < self.deadline_ms
        )

    def _fault(self, reason: str) -> None:
        if self.state == "FAULT":
            return
        if reason == "START TIMEOUT" and self.incident is None:
            self.incident = StartIncident(
                state=self.state,
                r20=self.r20,
                r26=self.r26,
                requested_gear=self.requested_gear,
                transmitted_gear=self.transmitted_gear,
                transmitted_topology=self.transmitted_topology,
                completed_cycles=self.completed_cycles,
                bad_cycles=self.bad_cycles,
                consecutive_bad_cycles=self.consecutive_bad_cycles,
                reason=reason,
            )
        self.state = "FAULT"

    def sample(self, now_ms: int, r20: int = 0, r26: int = 0,
               *, i2c_ok: bool = True) -> None:
        self.completed_cycles += 1
        if i2c_ok:
            self.r20 = r20
            self.r26 = r26
            self.consecutive_bad_cycles = 0
            feedback_valid = True
        else:
            self.bad_cycles += 1
            self.consecutive_bad_cycles += 1
            feedback_valid = False

        confirmation_open = self._confirmation_open(now_ms)
        heating_feedback_open = self.state == "HEATING" or confirmation_open

        if feedback_valid and r20 == 0x02 and heating_feedback_open:
            self.no_pan_samples += 1
            if self.no_pan_samples >= 3:
                self.state = "NO_PAN"
                self.deadline_ms = None
        elif feedback_valid and r20 != 0x02:
            self.no_pan_samples = 0

        if feedback_valid and heating_feedback_open and r20_policy(r20) not in {
            "known_fault", "no_pan"
        } and r26 in {1, 2}:
            self.cookware_limited = r26 == 1
            if self.cookware_limited:
                self.transmitted_gear = min(self.transmitted_gear, 35)
                self.transmitted_topology = 0xA1
            if confirmation_open:
                self.state = "HEATING"
                self.deadline_ms = None

        if feedback_valid and r20 in KNOWN_R20_FAULTS:
            if r20 == self.known_fault_value:
                self.known_fault_samples += 1
            else:
                self.known_fault_value = r20
                self.known_fault_samples = 1
            if self.known_fault_samples >= 2 and self.state in {
                "STARTING", "HEATING", "ACTIVE_ZERO", "PAUSED", "NO_PAN"
            }:
                self._fault("KNOWN R20")
        elif feedback_valid:
            self.known_fault_samples = 0
            if r20_policy(r20) == "dismissible_warning":
                self.warning = r20

        if (
            self.state == "STARTING"
            and self.deadline_ms is not None
            and now_ms >= self.deadline_ms
        ):
            self._fault("START TIMEOUT")

        if (
            self.consecutive_bad_cycles >= 6
            and self.state not in {"STOPPED", "FAULT"}
        ):
            self._fault("I2C LOST")

    def observe_stop_feedback(self, r20: int, r26: int) -> None:
        """Later Stop feedback may change live registers, never the EST incident."""
        self.r20 = r20
        self.r26 = r26


@dataclass
class StopTransaction:
    """Model the idempotent Stop transaction and its first-cause evidence."""

    state: str = "HEATING"
    generation: int = 0
    reason: str = "NONE"
    issue: str = "NONE"
    terminal: str = "IDLE"
    deadline_ms: int | None = None
    zero_samples: int = 0
    verified: bool = False
    timed_out: bool = False
    write_attempts: int = 0
    consecutive_i2c_bad: int = 0

    def begin(self, reason: str, terminal: str, now_ms: int = 0) -> None:
        if self.state in {"STOPPING", "FAULT"}:
            return
        self.state = "STOPPING"
        self.generation += 1
        self.reason = reason
        self.issue = "NONE"
        self.terminal = terminal
        self.deadline_ms = now_ms + 8_000
        self.zero_samples = 0
        self.verified = False
        self.timed_out = False

    def write_heartbeat(self, results: tuple[bool, bool, bool]) -> None:
        assert len(results) == 3
        self.write_attempts += 1

    def feedback(self, now_ms: int, *, r26: int = 1, valid: bool = True,
                 i2c_bad: bool = False, r20: int = 0) -> None:
        if self.state not in {"STOPPING", "FAULT"}:
            return
        if i2c_bad:
            self.consecutive_i2c_bad += 1
            if self.consecutive_i2c_bad >= 6 and self.issue == "NONE":
                self.issue = "I2C LOST"
        else:
            self.consecutive_i2c_bad = 0

        if r20 in KNOWN_R20_FAULTS and self.issue == "NONE":
            self.issue = "KNOWN R20"

        if valid:
            if r26 == 0:
                self.zero_samples = min(2, self.zero_samples + 1)
                if self.zero_samples == 2:
                    self.verified = True
                    self.deadline_ms = None
                    if self.state == "STOPPING":
                        self.state = self.terminal
            else:
                self.zero_samples = 0
                self.verified = False

        if (
            self.state in {"STOPPING", "FAULT"}
            and self.deadline_ms is not None
            and now_ms >= self.deadline_ms
        ):
            self.timed_out = True
            self.deadline_ms = None
            if self.issue == "NONE":
                self.issue = "STOP TIMEOUT"


LEASE_LIVE_STATES = {
    "STARTING", "HEATING", "ACTIVE_ZERO", "PAUSED", "NO_PAN", "PROFILE_ZERO"
}


@dataclass
class CookingLease:
    """Model the cooking-task lease enforced by the independent power task."""

    generation: int = 0
    active: bool = False
    expired: bool = False
    deadline_ms: int | None = None
    renewals: int = 0
    expirations: int = 0
    stop_reason: str = "NONE"

    def begin(self, now_ms: int, state: str) -> int:
        assert state == "ARMED"
        assert not self.active
        self.generation += 1
        self.active = True
        self.expired = False
        self.deadline_ms = now_ms + 3_000
        self.renewals = 0
        return self.generation

    def renew(self, generation: int, now_ms: int, state: str) -> bool:
        if (
            not self.active
            or generation == 0
            or generation != self.generation
            or state not in LEASE_LIVE_STATES
        ):
            return False
        if self.deadline_ms is None or now_ms >= self.deadline_ms:
            self._expire()
            return False
        self.deadline_ms = now_ms + 3_000
        self.renewals += 1
        return True

    def power_tick(self, now_ms: int) -> None:
        if self.active and self.deadline_ms is not None and now_ms >= self.deadline_ms:
            self._expire()

    def normal_stop(self) -> None:
        self.active = False
        self.deadline_ms = None

    def _expire(self) -> None:
        self.active = False
        self.expired = True
        self.deadline_ms = None
        self.expirations += 1
        self.stop_reason = "COOK LEASE"


@dataclass
class ConfirmedTransition:
    """Model generation-tagged command/feedback confirmation without invented gear data."""

    generation: int = 0
    confirmed_generation: int = 0
    feedback_sequence: int = 0
    feedback_baseline: int = 0
    kind: str = "NONE"
    requested_state: str = "STOPPED"
    requested_gear: int = 0
    transmitted_gear: int = 0
    transmitted_topology: int = 0
    confirmed_state: str = "STOPPED"
    confirmed_gear: int = 0
    pending: bool = False
    command_transmitted: bool = False
    inferred: bool = False
    feedback_gear_known: bool = False
    deadline_ms: int | None = None
    result: str = "NONE"
    rejection_sequence: int = 0
    rejection: str = "NONE"

    def request(self, kind: str, requested_state: str, gear: int) -> int | None:
        if self.pending:
            if (
                self.kind == kind
                and self.requested_state == requested_state
                and self.requested_gear == gear
            ):
                return self.generation
            self.rejection_sequence += 1
            self.rejection = f"{kind} TRANSITION BUSY"
            return None
        self.generation += 1
        self.kind = kind
        self.requested_state = requested_state
        self.requested_gear = gear
        self.pending = True
        self.command_transmitted = False
        self.inferred = False
        self.deadline_ms = None
        self.feedback_baseline = self.feedback_sequence
        self.result = "PENDING"
        return self.generation

    def transmit(self, generation: int, now_ms: int, command: tuple[int, int, int]) -> bool:
        if not self.pending or generation != self.generation:
            return False
        if self.kind in {"ACTIVE_ZERO", "PAUSE", "PAN_RETURN_HOLD"} or self.requested_gear == 0:
            expected = (0x81, 0, 0)
        elif self.requested_gear <= 35:
            expected = (0xA1, 1, self.requested_gear)
        elif self.requested_gear < 56:
            expected = (0xC1, 1, self.requested_gear)
        else:
            expected = (0xE1, 1, self.requested_gear)
        if command != expected:
            return False
        self.transmitted_topology, _, self.transmitted_gear = command
        self.command_transmitted = True
        self.feedback_baseline = self.feedback_sequence
        if self.deadline_ms is None:
            self.deadline_ms = now_ms + (8_000 if self.kind == "START" else 3_000)
        return True

    def replace_start_gear(self, gear: int) -> int | None:
        if not self.pending or self.kind != "START":
            self.rejection_sequence += 1
            self.rejection = "GEAR TRANSITION BUSY"
            return None
        if gear == self.requested_gear:
            return self.generation
        self.generation += 1
        self.requested_gear = min(gear, 10)
        self.requested_state = "ACTIVE_ZERO" if gear == 0 else "HEATING"
        self.command_transmitted = False
        self.feedback_baseline = self.feedback_sequence
        self.deadline_ms = None
        self.result = "PENDING"
        return self.generation

    def feedback(self, now_ms: int, *, r20: int, r26: int, valid: bool = True) -> bool:
        if valid:
            self.feedback_sequence += 1
        if not self.pending or not self.command_transmitted or not valid:
            return False
        if self.deadline_ms is None or now_ms >= self.deadline_ms:
            self.timeout(now_ms)
            return False
        if self.feedback_sequence <= self.feedback_baseline:
            return False
        if self.kind in {"PAN_RETURN_HOLD", "PAN_RETURN_RESUME"}:
            r20_ok = r20 in {0, 0x2B, 0x29, 0x2A}
        else:
            r20_ok = r20 not in KNOWN_R20_FAULTS and r20 != 0x02
        if (
            self.kind not in {"PAN_RETURN_HOLD", "PAN_RETURN_RESUME"}
            and (self.kind in {"ACTIVE_ZERO", "PAUSE"} or self.requested_gear == 0)
            and r20 == 0x02
        ):
            r20_ok = True
        if not r20_ok or r26 not in {1, 2}:
            return False
        self.pending = False
        self.command_transmitted = False
        self.confirmed_generation = self.generation
        self.confirmed_state = self.requested_state
        self.confirmed_gear = min(self.requested_gear, 35) if r26 == 1 else self.requested_gear
        self.feedback_gear_known = False
        self.inferred = True
        self.deadline_ms = None
        self.result = "CONFIRMED"
        return True

    def timeout(self, now_ms: int) -> None:
        if not self.pending or self.deadline_ms is None or now_ms < self.deadline_ms:
            return
        reasons = {
            "START": "START TIMEOUT",
            "ACTIVE_ZERO": "ZERO ACK TIMEOUT",
            "PAUSE": "PAUSE ACK TIMEOUT",
            "RESUME": "RESUME TIMEOUT",
            "PAN_RETURN_HOLD": "PAN HOLD TIMEOUT",
            "PAN_RETURN_RESUME": "PAN RESUME TIMEOUT",
        }
        self.pending = False
        self.command_transmitted = False
        self.deadline_ms = None
        self.result = reasons[self.kind]

    def stop(self, reason: str = "USER STOP") -> None:
        if self.pending:
            self.pending = False
            self.command_transmitted = False
            self.deadline_ms = None
            self.result = reason

    def replace_pan_return_with_pause(self) -> int | None:
        if not self.pending or self.kind not in {"PAN_RETURN_HOLD", "PAN_RETURN_RESUME"}:
            return None
        self.generation += 1
        self.kind = "PAUSE"
        self.requested_state = "PAUSED"
        self.requested_gear = 0
        self.command_transmitted = False
        self.feedback_baseline = self.feedback_sequence
        self.deadline_ms = None
        self.result = "PENDING"
        return self.generation


@dataclass
class Timer:
    remaining: int

    def tick(self, state: str, seconds: int = 1) -> None:
        if state == "COOKING":
            self.remaining = max(0, self.remaining - seconds)


@dataclass
class TimerControl:
    last: int = 270
    remaining: int = 270
    enabled: bool = False

    def set(self, seconds: int) -> None:
        assert 0 < seconds <= 5 * 60 * 60
        self.last = seconds
        self.remaining = seconds
        self.enabled = True

    def disable(self) -> None:
        self.enabled = False


@dataclass
class SessionTimeBuckets:
    """Independent wall, output, Pause and NoPan accounting for one retained session."""

    wall_s: int = 0
    heating_s: int = 0
    active_zero_s: int = 0
    profile_zero_s: int = 0
    manual_pause_s: int = 0
    no_pan_s: int = 0

    def tick(self, state: str, seconds: int, *, output: str = "OFF",
             profile_zero: bool = False) -> None:
        if state in {"STARTING", "COOKING", "PAUSED", "NO_PAN"}:
            self.wall_s += seconds
        if state == "PAUSED":
            self.manual_pause_s += seconds
        elif state == "NO_PAN":
            self.no_pan_s += seconds
        elif state == "COOKING" and output == "HEATING":
            self.heating_s += seconds
        elif state == "COOKING" and output == "ACTIVE_ZERO":
            if profile_zero:
                self.profile_zero_s += seconds
            else:
                self.active_zero_s += seconds


def delayed_start_view(mode: str, current_view: str) -> str:
    del current_view  # Expiry owns the view regardless of the menu being displayed.
    return {
        "POWER": "POWER",
        "TEMPERATURE": "TEMPERATURE",
        "PROFILE": "PROFILE_READY",
    }[mode]


def long_center_action(state: str, timer_editing: bool) -> str:
    if state == "DELAYED":
        return "CANCEL_DELAY"
    if state in {"STARTING", "COOKING", "PAUSED", "NO_PAN", "STOPPING"}:
        return "STOP"
    if timer_editing:
        return "CLOSE_TIMER"
    return "NAVIGATE"


def delayed_deadline(cancel_queued: bool, due: bool) -> str:
    """Queued physical intents are consumed before the deadline update."""
    if cancel_queued:
        return "IDLE"
    return "STARTING" if due else "DELAYED"


def delayed_start_tick(due: bool, stale_power: str, fresh_power: str,
                       start_pending: bool) -> tuple[str, str]:
    """A due schedule must classify a post-Start snapshot, never the stale one."""
    state = "DELAYED"
    power = stale_power
    if due:
        state = "STARTING"
        power = fresh_power
    if state == "STARTING" and power == "STOPPED" and not start_pending:
        state = "FAULT"
    return state, power


def delayed_picture_labels(mode: str, value: int, remaining: str) -> tuple[str, ...]:
    return "time", remaining, delayed_mode(mode, value)


def delayed_after_restart(was_scheduled: bool) -> bool:
    del was_scheduled
    return False  # Schedule state is deliberately RAM-only.


def timer_fields(total_s: int) -> tuple[int, int, int]:
    assert 0 <= total_s <= 5 * 60 * 60
    return total_s % 60, (total_s // 60) % 60, total_s // 3600


def timer_total(seconds: int, minutes: int, hours: int) -> int:
    assert 0 <= seconds <= 59 and 0 <= minutes <= 59 and 0 <= hours <= 5
    total = hours * 3600 + minutes * 60 + seconds
    assert 0 < total <= 5 * 60 * 60
    return total


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


def cookware_feedback(r26: int, command_active: bool, requested: int,
                      applied: int) -> tuple[bool, bool, int, str]:
    """Model stock R26 cookware capability feedback and the effective command."""
    if not command_active or r26 not in {1, 2}:
        return False, False, applied, "unchanged"
    if r26 == 1:
        return True, True, min(applied, 35), "A1"
    return True, False, applied, "normal"


def user_power_step(selected: int, delta: int, cookware_limited: bool) -> tuple[int, bool]:
    """Return the honest displayed setting and whether the limit notice repeats."""
    requested = max(0, min(99, selected + delta))
    if cookware_limited and requested > 35:
        return selected, True
    return requested, False


def r20_policy(value: int) -> str:
    """Classify stock-known statuses without inventing faults for unknown values."""
    if value == 0:
        return "normal"
    if value == 0x02:
        return "no_pan"
    if value in {0x2B, 0x29, 0x2A}:
        return "silent_nonfault"
    if value in KNOWN_R20_FAULTS:
        return "known_fault"
    return "dismissible_warning"


def r20_warning_events(values: list[int]) -> list[int]:
    """Emit once per continuous unknown value, rearming after a known status."""
    present: int | None = None
    events: list[int] = []
    for value in values:
        if r20_policy(value) == "dismissible_warning":
            if present != value:
                present = value
                events.append(value)
        else:
            present = None
    return events


def profile_sequence(durations: list[int]) -> list[int]:
    """Return the physical 1-based cells that actually execute."""
    assert len(durations) == 5
    assert sum(durations) <= 5 * 60 * 60
    return [index + 1 for index, duration in enumerate(durations) if duration > 0]


def startup_probe_outcome(
    required_reads: tuple[bool, ...], service_reads: tuple[bool, ...]
) -> tuple[bool, int]:
    """Only capability/safety reads gate boot; R2C-R2F are diagnostics."""
    return all(required_reads), sum(not result for result in service_reads)


def profile_timer_action(*, remaining_s: int, transition_pending: bool) -> str:
    """A completed cell waits until the preceding output transaction settles."""
    if transition_pending:
        return "WAIT"
    return "ADVANCE" if remaining_s == 0 else "COUNT"


def active_zero_command(state: str) -> tuple[int, int, int]:
    """Model the retained-session command used by zero output and manual Pause."""
    if state in {"ACTIVE_ZERO", "PAUSED"}:
        return 0x81, 0, 0
    if state == "STOPPED":
        return 0, 0, 0
    raise ValueError(state)


def retained_resume_first_gear(target: int) -> int:
    return target


def manual_pause_state(elapsed_s: int) -> str:
    return "STOPPED" if elapsed_s >= 2 * 60 * 60 else "PAUSED"


def configure_mode(state: str) -> tuple[str, bool]:
    configurable = {"SLEEP", "IDLE", "READY", "COMPLETE"}
    return ("READY", True) if state in configurable else (state, False)


def start_transaction(arm_ok: bool, start_ok: bool) -> tuple[bool, bool]:
    """Return run_started and whether an armed-session rollback Stop is required."""
    if not arm_ok:
        return False, False
    if not start_ok:
        return False, True
    return True, False


def pause_no_pan_context(no_pan_elapsed_s: int) -> tuple[str, int]:
    """Manual Pause starts a fresh NoPan window after a later Resume."""
    assert no_pan_elapsed_s >= 0
    return "PAUSED", 0


def changed_temperature_output(
    state: str, target: int, measured: int, readings_valid: bool
) -> int | None:
    if state not in {"STARTING", "COOKING"}:
        return None
    if not readings_valid:
        return 0
    return temperature_step("PREHEAT", target, measured)[1]


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
    readings_valid: bool = False,
    bottom_c: int = 0,
) -> str | None:
    """Model the production display priority and 60-second default Sleep."""
    urgent = {"FAULT": "error", "NO_PAN": "nopan", "COMPLETE": "ready"}
    if state in urgent:
        return urgent[state]
    if state == "DELAYED":
        return "time"
    if transient:
        return transient
    surface_hot = readings_valid and bottom_c > 60
    if state in {"IDLE", "READY"} and surface_hot and idle_ms >= 5_000:
        return "hot" if (idle_ms - 5_000) % 3_000 < 2_000 else "blank"
    if state == "SLEEP" and sleep_ms < 10_000:
        return "sleep2"
    if state in {"IDLE", "READY"} and not surface_hot and idle_ms >= 50_000:
        return "sleep1"
    return None


def ready_picture(completion_number: int) -> str:
    return f"ready{completion_number % 3 + 1}"


def delayed_mode(mode: str, value: int) -> str:
    return {"POWER": "P", "TEMPERATURE": "t", "PROFILE": "pr"}[mode] + str(value)


def sleep_allowed(*, readings_valid: bool, bottom_c: int) -> bool:
    return not (readings_valid and bottom_c > 60)


def no_pan_fault(*, melody_completed: bool, queue_elapsed_ms: int,
                 playback_elapsed_ms: int | None, pan_returned: bool) -> bool:
    """Completion is authoritative; queue and playback watchdogs stay separate."""
    if pan_returned:
        return False
    if melody_completed:
        return True
    if playback_elapsed_ms is None:
        return queue_elapsed_ms >= 30_000
    return playback_elapsed_ms >= 132_000


def transient_after_physical_input(
    transient: str | None, *, physical_input: bool, action_picture: str | None = None
) -> str | None:
    """Input dismisses the old timed picture; its own action may create a new one."""
    if physical_input:
        transient = None
    return action_picture if action_picture is not None else transient


def start_at_view(clock_valid: bool) -> str:
    return "START_AT_HOURS" if clock_valid else "START_AT_NO_CLOCK"


def live_screen_kind(state: str) -> str:
    return "FOCUS" if state in {"STARTING", "COOKING", "PAUSED", "STOPPING"} else "TEXT"


def run() -> None:
    timer = Timer(270)
    timer.tick("COOKING", 10)
    assert timer.remaining == 260
    timer.tick("PAUSED", 60)
    timer.tick("NO_PAN", 120)
    assert timer.remaining == 260
    timer.tick("COOKING", 260)
    assert timer.remaining == 0
    assert not no_pan_fault(melody_completed=False, queue_elapsed_ms=20_000,
                            playback_elapsed_ms=None, pan_returned=False)
    assert no_pan_fault(melody_completed=False, queue_elapsed_ms=30_000,
                        playback_elapsed_ms=None, pan_returned=False)
    assert not no_pan_fault(melody_completed=False, queue_elapsed_ms=140_000,
                            playback_elapsed_ms=127_999, pan_returned=False)
    assert no_pan_fault(melody_completed=True, queue_elapsed_ms=128_000,
                        playback_elapsed_ms=128_000, pan_returned=False)
    assert no_pan_fault(melody_completed=False, queue_elapsed_ms=150_000,
                        playback_elapsed_ms=132_000, pan_returned=False)
    assert not no_pan_fault(melody_completed=True, queue_elapsed_ms=128_000,
                            playback_elapsed_ms=128_000, pan_returned=True)

    control = TimerControl()
    control.set(5 * 60)
    control.disable()
    control.set(40 * 60)
    assert control.enabled and control.last == 2400 and control.remaining == 2400
    control.disable()
    control.set(45)
    assert control.enabled and control.remaining == 45
    assert timer_fields(4 * 3600 + 40 * 60 + 25) == (25, 40, 4)
    assert timer_total(25, 40, 4) == 4 * 3600 + 40 * 60 + 25
    assert timer_total(0, 0, 5) == 5 * 60 * 60

    clocks = SessionTimeBuckets()
    clocks.tick("STARTING", 2)
    clocks.tick("COOKING", 600, output="HEATING")
    clocks.tick("COOKING", 300, output="ACTIVE_ZERO")
    clocks.tick("COOKING", 3 * 60 * 60, output="ACTIVE_ZERO", profile_zero=True)
    clocks.tick("PAUSED", 120)
    clocks.tick("NO_PAN", 30)
    assert clocks.wall_s == 2 + 600 + 300 + 3 * 60 * 60 + 120 + 30
    assert clocks.heating_s == 600
    assert clocks.active_zero_s == 300
    assert clocks.profile_zero_s == 3 * 60 * 60
    assert clocks.manual_pause_s == 120 and clocks.no_pan_s == 30
    assert 5 * 60 * 60 + 2 * 60 * 60 + 60 < 8 * 60 * 60
    menu_views = (
        "HOME", "POWER", "TEMPERATURE", "READINGS", "SETTINGS", "TIMER",
        "START_IN", "START_AT", "PROFILES", "WIFI", "CLOCK",
    )
    for current_view in menu_views:
        assert delayed_start_view("POWER", current_view) == "POWER"
        assert delayed_start_view("TEMPERATURE", current_view) == "TEMPERATURE"
        assert delayed_start_view("PROFILE", current_view) == "PROFILE_READY"
    assert long_center_action("COOKING", True) == "STOP"
    assert long_center_action("NO_PAN", True) == "STOP"
    assert long_center_action("DELAYED", True) == "CANCEL_DELAY"
    assert long_center_action("IDLE", True) == "CLOSE_TIMER"
    assert delayed_deadline(cancel_queued=True, due=True) == "IDLE"
    assert delayed_deadline(cancel_queued=False, due=True) == "STARTING"
    assert delayed_start_tick(False, "STOPPED", "STOPPED", False) == (
        "DELAYED", "STOPPED")
    assert delayed_start_tick(True, "STOPPED", "STARTING", True) == (
        "STARTING", "STARTING")
    assert delayed_start_tick(True, "STOPPED", "STOPPED", True) == (
        "STARTING", "STOPPED")
    assert delayed_start_tick(True, "STOPPED", "STOPPED", False) == (
        "FAULT", "STOPPED")
    assert delayed_picture_labels("POWER", 10, "00:42") == (
        "time", "00:42", "P10")
    assert not delayed_after_restart(was_scheduled=True)

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
    assert cookware_feedback(1, True, 99, 66) == (True, True, 35, "A1")
    assert cookware_feedback(1, True, 20, 20) == (True, True, 20, "A1")
    assert cookware_feedback(2, True, 99, 66) == (True, False, 66, "normal")
    assert cookware_feedback(1, False, 99, 0) == (False, False, 0, "unchanged")
    assert user_power_step(35, 5, True) == (35, True)
    assert user_power_step(35, -5, True) == (30, False)
    assert user_power_step(35, 5, False) == (40, False)
    assert r20_policy(0x00) == "normal"
    assert r20_policy(0x02) == "no_pan"
    assert r20_policy(0x2B) == "silent_nonfault"
    assert r20_policy(0x29) == "silent_nonfault"
    assert r20_policy(0x2A) == "silent_nonfault"
    assert r20_policy(0x17) == "known_fault"
    assert r20_policy(0x7F) == "dismissible_warning"
    assert r20_warning_events([0x7F, 0x7F, 0x00, 0x7F, 0x29, 0x7F]) == [0x7F] * 3

    # Start acknowledgement is accepted only after the first transmitted nonzero
    # heartbeat and strictly before its sole eight-second deadline.
    for r26, limited in ((0x01, True), (0x02, False)):
        immediate = StartProtocol()
        immediate.start()
        immediate.heartbeat(100)
        immediate.sample(101, r20=0, r26=r26)
        assert immediate.state == "HEATING"
        assert immediate.cookware_limited is limited

        delayed = StartProtocol()
        delayed.start()
        delayed.heartbeat(100)
        delayed.sample(8_099, r20=0, r26=r26)
        assert delayed.state == "HEATING"

    stale = StartProtocol()
    stale.start()
    stale.sample(50, r20=0x2B, r26=0x02)
    assert stale.state == "STARTING" and stale.deadline_ms is None
    stale.heartbeat(100)
    stale.sample(101, r20=0x2B, r26=0x02)
    assert stale.state == "HEATING"

    for transition_r20 in (0x2B, 0x29, 0x2A):
        before = StartProtocol()
        before.start()
        before.sample(0, r20=transition_r20, r26=0)
        before.heartbeat(100)
        before.sample(101, r20=transition_r20, r26=2)
        before.sample(500, r20=transition_r20, r26=2)
        assert before.state == "HEATING" and before.warning is None

    warning = StartProtocol()
    warning.start()
    warning.heartbeat(0)
    warning.sample(500, r20=0x7F, r26=2)
    assert warning.state == "HEATING" and warning.warning == 0x7F

    no_pan = StartProtocol()
    no_pan.start()
    no_pan.heartbeat(0)
    for timestamp in (500, 1_000, 1_500):
        no_pan.sample(timestamp, r20=0x02, r26=0)
    assert no_pan.state == "NO_PAN" and no_pan.incident is None

    for known_fault in sorted(KNOWN_R20_FAULTS):
        faulted = StartProtocol()
        faulted.start()
        faulted.heartbeat(0)
        faulted.sample(500, r20=known_fault, r26=0)
        faulted.sample(1_000, r20=known_fault, r26=0)
        assert faulted.state == "FAULT" and faulted.incident is None

    recovered_i2c = StartProtocol()
    recovered_i2c.start()
    recovered_i2c.heartbeat(0)
    for timestamp in (500, 1_000, 1_500, 2_000, 2_500):
        recovered_i2c.sample(timestamp, i2c_ok=False)
    recovered_i2c.sample(3_000, r20=0, r26=2)
    assert recovered_i2c.state == "HEATING"

    lost_i2c = StartProtocol()
    lost_i2c.start()
    lost_i2c.heartbeat(0)
    for timestamp in (500, 1_000, 1_500, 2_000, 2_500, 3_000):
        lost_i2c.sample(timestamp, i2c_ok=False)
    assert lost_i2c.state == "FAULT" and lost_i2c.incident is None

    before_boundary = StartProtocol()
    before_boundary.start()
    before_boundary.heartbeat(0)
    before_boundary.sample(7_999, r20=0x2B, r26=2)
    assert before_boundary.state == "HEATING"

    at_boundary = StartProtocol()
    at_boundary.start()
    at_boundary.heartbeat(0)
    at_boundary.sample(8_000, r20=0x2B, r26=2)
    assert at_boundary.state == "FAULT"
    assert at_boundary.incident is not None
    assert at_boundary.incident.r20 == 0x2B
    assert at_boundary.incident.r26 == 0x02
    frozen_incident = at_boundary.incident
    at_boundary.observe_stop_feedback(0, 0)
    at_boundary.sample(8_500, r20=0, r26=2)
    assert at_boundary.state == "FAULT" and at_boundary.incident == frozen_incident

    no_ack = StartProtocol()
    no_ack.start()
    no_ack.heartbeat(0)
    no_ack.sample(8_000, r20=0, r26=0)
    assert no_ack.state == "FAULT"
    assert no_ack.incident is not None and no_ack.incident.reason == "START TIMEOUT"
    no_ack.sample(8_001, r20=0, r26=2)
    assert no_ack.state == "FAULT" and no_ack.incident.reason == "START TIMEOUT"

    stop_origins = {
        "USER STOP": "IDLE",
        "CANCEL": "IDLE",
        "TIMER COMPLETE": "COMPLETE",
        "PROFILE COMPLETE": "COMPLETE",
        "PAUSE TIMEOUT": "IDLE",
        "RUN LIMIT": "FAULT",
        "E05 BOTTOM": "FAULT",
    }
    for origin, terminal in stop_origins.items():
        stop = StopTransaction()
        stop.begin(origin, terminal)
        stop.write_heartbeat((False, True, True))
        stop.feedback(500, r26=1)
        stop.write_heartbeat((True, False, True))
        stop.feedback(1_000, r26=0)
        assert stop.state == "STOPPING" and not stop.verified
        stop.write_heartbeat((True, True, False))
        stop.feedback(1_500, r26=1)
        stop.write_heartbeat((True, True, True))
        stop.feedback(2_000, r26=0)
        stop.feedback(2_500, r26=0)
        assert stop.state == terminal and stop.verified
        assert stop.reason == origin and stop.write_attempts == 4

    repeated_stop = StopTransaction()
    repeated_stop.begin("USER STOP", "IDLE")
    generation = repeated_stop.generation
    repeated_stop.begin("CANCEL", "IDLE", 2_000)
    assert repeated_stop.generation == generation
    assert repeated_stop.reason == "USER STOP"

    stuck_stop = StopTransaction()
    stuck_stop.begin("USER STOP", "IDLE")
    for timestamp in range(500, 8_001, 500):
        stuck_stop.write_heartbeat((True, True, True))
        stuck_stop.feedback(timestamp, r26=1)
    assert stuck_stop.state == "STOPPING" and stuck_stop.timed_out
    assert stuck_stop.reason == "USER STOP" and stuck_stop.issue == "STOP TIMEOUT"
    stuck_stop.feedback(8_500, r26=0)
    stuck_stop.feedback(9_000, r26=0)
    assert stuck_stop.state == "IDLE" and stuck_stop.verified

    lost_stop = StopTransaction()
    lost_stop.begin("TIMER COMPLETE", "COMPLETE")
    for timestamp in (500, 1_000, 1_500, 2_000, 2_500, 3_000):
        lost_stop.write_heartbeat((False, False, False))
        lost_stop.feedback(timestamp, valid=False, i2c_bad=True)
    assert lost_stop.state == "STOPPING"
    assert lost_stop.reason == "TIMER COMPLETE" and lost_stop.issue == "I2C LOST"
    lost_stop.feedback(3_500, r26=0)
    lost_stop.feedback(4_000, r26=0)
    assert lost_stop.state == "COMPLETE"

    orthogonal_stop = StopTransaction()
    orthogonal_stop.begin("CANCEL", "IDLE")
    orthogonal_stop.feedback(500, r26=1, r20=0x2B)
    orthogonal_stop.feedback(1_000, r26=1, r20=0x7F)
    assert orthogonal_stop.state == "STOPPING" and orthogonal_stop.issue == "NONE"
    orthogonal_stop.feedback(1_500, r26=1, r20=0x17)
    assert orthogonal_stop.issue == "KNOWN R20" and orthogonal_stop.reason == "CANCEL"

    for live_state in LEASE_LIVE_STATES:
        lease = CookingLease()
        generation = lease.begin(0, "ARMED")
        assert lease.renew(generation, 100, live_state)
        lease.power_tick(2_999)
        assert lease.active and not lease.expired

    for nonlive_state in {"IDLE", "READY", "DELAYED", "STOPPING", "COMPLETE", "FAULT"}:
        lease = CookingLease()
        assert not lease.renew(1, 100, nonlive_state)
        assert not lease.active and not lease.expired

    suspended_cooking = CookingLease()
    suspended_cooking.begin(0, "ARMED")
    suspended_cooking.power_tick(2_999)
    assert suspended_cooking.active
    suspended_cooking.power_tick(3_000)
    assert suspended_cooking.expired
    assert suspended_cooking.stop_reason == "COOK LEASE"
    assert suspended_cooking.expirations == 1

    stalled_ui = CookingLease()
    ui_generation = stalled_ui.begin(0, "ARMED")
    for timestamp in range(100, 10_001, 100):
        assert stalled_ui.renew(ui_generation, timestamp, "ACTIVE_ZERO")
        if timestamp % 500 == 0:
            stalled_ui.power_tick(timestamp)
    assert stalled_ui.active and not stalled_ui.expired

    stalled_power = CookingLease()
    power_generation = stalled_power.begin(0, "ARMED")
    for timestamp in (1_000, 2_000, 3_000):
        assert stalled_power.renew(power_generation, timestamp, "PAUSED")
    assert stalled_power.deadline_ms == 6_000
    assert not stalled_power.expired  # Power-task watchdog remains a separate guard.

    stale_generation = CookingLease()
    old_generation = stale_generation.begin(0, "ARMED")
    stale_generation.normal_stop()
    new_generation = stale_generation.begin(1_000, "ARMED")
    deadline = stale_generation.deadline_ms
    assert new_generation != old_generation
    assert not stale_generation.renew(old_generation, 1_100, "HEATING")
    assert stale_generation.deadline_ms == deadline
    assert stale_generation.renew(new_generation, 1_100, "HEATING")

    stopped_lease = CookingLease()
    stopped_generation = stopped_lease.begin(0, "ARMED")
    stopped_lease.normal_stop()
    stopped_lease.power_tick(10_000)
    assert not stopped_lease.active and not stopped_lease.expired
    assert not stopped_lease.renew(stopped_generation, 10_000, "PROFILE_ZERO")

    start_transition = ConfirmedTransition()
    start_generation = start_transition.request("START", "HEATING", 10)
    assert not start_transition.feedback(100, r20=0, r26=2)
    assert start_transition.transmit(start_generation, 200, (0xA1, 1, 10))
    assert start_transition.feedback(500, r20=0x2B, r26=2)
    assert start_transition.confirmed_state == "HEATING"
    assert start_transition.confirmed_gear == 10 and start_transition.inferred
    assert not start_transition.feedback_gear_known

    edited_start = ConfirmedTransition()
    old_start_generation = edited_start.request("START", "HEATING", 10)
    assert edited_start.transmit(old_start_generation, 0, (0xA1, 1, 10))
    edited_start.feedback_sequence += 1  # unread old-generation feedback
    zero_start_generation = edited_start.replace_start_gear(0)
    assert zero_start_generation != old_start_generation
    assert not edited_start.transmit(old_start_generation, 100, (0xA1, 1, 10))
    assert edited_start.transmit(zero_start_generation, 200, (0x81, 0, 0))
    assert edited_start.feedback(500, r20=0, r26=2)
    assert edited_start.confirmed_state == "ACTIVE_ZERO"

    for kind, final_state, gear, command in (
        ("ACTIVE_ZERO", "ACTIVE_ZERO", 0, (0x81, 0, 0)),
        ("PAUSE", "PAUSED", 0, (0x81, 0, 0)),
        ("RESUME", "HEATING", 99, (0xE1, 1, 99)),
        ("RESUME", "ACTIVE_ZERO", 0, (0x81, 0, 0)),
    ):
        transition = ConfirmedTransition()
        generation = transition.request(kind, final_state, gear)
        assert transition.request(kind, final_state, gear) == generation
        assert transition.transmit(generation, 100, command)
        assert transition.feedback(500, r20=0, r26=2)
        assert transition.confirmed_generation == generation
        assert transition.confirmed_state == final_state
        assert transition.confirmed_gear == gear

    no_pan_pause = ConfirmedTransition()
    no_pan_pause_generation = no_pan_pause.request("PAUSE", "PAUSED", 0)
    assert no_pan_pause.transmit(no_pan_pause_generation, 0, (0x81, 0, 0))
    assert no_pan_pause.feedback(500, r20=0x02, r26=2)

    zero_profile_start = ConfirmedTransition()
    zero_start_generation = zero_profile_start.request("START", "ACTIVE_ZERO", 0)
    assert zero_profile_start.transmit(zero_start_generation, 0, (0x81, 0, 0))
    assert zero_profile_start.feedback(500, r20=0x02, r26=2)

    stale_transition = ConfirmedTransition()
    stale_generation = stale_transition.request("ACTIVE_ZERO", "ACTIVE_ZERO", 0)
    assert stale_transition.request("RESUME", "HEATING", 35) is None
    assert stale_transition.generation == stale_generation
    assert stale_transition.rejection == "RESUME TRANSITION BUSY"
    stale_transition.stop()
    new_generation = stale_transition.request("START", "HEATING", 35)
    assert new_generation != stale_generation
    assert not stale_transition.transmit(stale_generation, 100, (0x81, 0, 0))
    assert stale_transition.transmit(new_generation, 200, (0xA1, 1, 35))
    assert stale_transition.feedback(500, r20=0, r26=1)
    assert stale_transition.transmitted_gear == 35
    assert stale_transition.confirmed_gear == 35

    restricted_resume = ConfirmedTransition()
    restricted_generation = restricted_resume.request("RESUME", "HEATING", 99)
    assert restricted_resume.transmit(restricted_generation, 0, (0xE1, 1, 99))
    assert restricted_resume.feedback(500, r20=0, r26=1)
    assert restricted_resume.transmitted_gear == 99
    assert restricted_resume.confirmed_gear == 35

    exact_deadline = ConfirmedTransition()
    deadline_generation = exact_deadline.request("RESUME", "HEATING", 35)
    assert exact_deadline.transmit(deadline_generation, 0, (0xA1, 1, 35))
    assert not exact_deadline.feedback(3_000, r20=0, r26=2)
    assert exact_deadline.result == "RESUME TIMEOUT"

    rejected_feedback = ConfirmedTransition()
    rejected_generation = rejected_feedback.request("START", "HEATING", 10)
    assert rejected_feedback.transmit(rejected_generation, 0, (0xA1, 1, 10))
    assert not rejected_feedback.feedback(500, r20=0x17, r26=2)
    assert not rejected_feedback.feedback(1_000, r20=0x02, r26=2)
    assert rejected_feedback.pending
    rejected_feedback.stop()
    assert not rejected_feedback.pending and rejected_feedback.result == "USER STOP"

    # Pan return is deliberately two-phase. Only recognized pan-present R20
    # values can confirm the safe active-zero hold; an unknown warning cannot.
    pan_return = ConfirmedTransition()
    hold_generation = pan_return.request("PAN_RETURN_HOLD", "ACTIVE_ZERO", 0)
    assert pan_return.transmit(hold_generation, 0, (0x81, 0, 0))
    assert not pan_return.feedback(250, r20=0x33, r26=2)
    assert pan_return.pending
    assert pan_return.feedback(500, r20=0x2B, r26=1)
    assert pan_return.confirmed_state == "ACTIVE_ZERO"
    resume_generation = pan_return.request("PAN_RETURN_RESUME", "HEATING", 35)
    assert resume_generation != hold_generation
    assert pan_return.transmit(resume_generation, 750, (0xA1, 1, 35))
    assert not pan_return.feedback(1_000, r20=0x02, r26=2)
    assert pan_return.pending
    assert pan_return.feedback(1_250, r20=0, r26=1)
    assert pan_return.confirmed_state == "HEATING"
    assert pan_return.confirmed_gear == 35

    # Stop and Pause replace/cancel a recovery generation, so late feedback
    # from the old generation can never restore output.
    cancelled_return = ConfirmedTransition()
    cancelled_generation = cancelled_return.request(
        "PAN_RETURN_RESUME", "HEATING", 35
    )
    assert cancelled_return.transmit(cancelled_generation, 0, (0xA1, 1, 35))
    cancelled_return.stop()
    assert not cancelled_return.feedback(500, r20=0, r26=2)
    paused_return = ConfirmedTransition()
    old_generation = paused_return.request("PAN_RETURN_HOLD", "ACTIVE_ZERO", 0)
    assert paused_return.transmit(old_generation, 0, (0x81, 0, 0))
    pause_generation = paused_return.replace_pan_return_with_pause()
    assert pause_generation != old_generation
    assert not paused_return.transmit(old_generation, 100, (0x81, 0, 0))
    assert paused_return.transmit(pause_generation, 200, (0x81, 0, 0))
    assert paused_return.feedback(500, r20=0x02, r26=2)
    assert paused_return.confirmed_state == "PAUSED"

    # A retained session deliberately resumes at the freshly recomputed target;
    # it does not inherit the cold-start gear-10 ramp as an accidental side effect.
    assert retained_resume_first_gear(99) == 99
    assert retained_resume_first_gear(35) == 35
    assert profile_sequence([2400, 1500, 0, 300, 0]) == [1, 2, 4]
    assert profile_sequence([0, 0, 0, 0, 0]) == []
    assert startup_probe_outcome((True,) * 6, (True, False, True, False)) == (True, 2)
    assert startup_probe_outcome((True, True, False, True, True, True), (True,) * 4) == (False, 0)
    assert profile_timer_action(remaining_s=0, transition_pending=True) == "WAIT"
    assert profile_timer_action(remaining_s=0, transition_pending=False) == "ADVANCE"
    assert profile_timer_action(remaining_s=1, transition_pending=False) == "COUNT"
    assert active_zero_command("ACTIVE_ZERO") == (0x81, 0, 0)
    assert active_zero_command("PAUSED") == (0x81, 0, 0)
    assert active_zero_command("STOPPED") == (0, 0, 0)
    assert manual_pause_state(2 * 60 * 60 - 1) == "PAUSED"
    assert manual_pause_state(2 * 60 * 60) == "STOPPED"
    assert configure_mode("IDLE") == ("READY", True)
    assert configure_mode("READY") == ("READY", True)
    assert configure_mode("DELAYED") == ("DELAYED", False)
    assert configure_mode("STARTING") == ("STARTING", False)
    assert configure_mode("FAULT") == ("FAULT", False)
    assert start_transaction(False, False) == (False, False)
    assert start_transaction(True, False) == (False, True)
    assert start_transaction(True, True) == (True, False)
    assert pause_no_pan_context(59) == ("PAUSED", 0)
    assert changed_temperature_output("STARTING", 125, 130, True) == 0
    assert changed_temperature_output("STARTING", 125, 100, True) == 77
    assert changed_temperature_output("STARTING", 125, 100, False) == 0
    assert changed_temperature_output("PAUSED", 125, 100, True) is None
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
    assert picture_policy(
        "COMPLETE", idle_ms=999_999, readings_valid=True, bottom_c=99
    ) == "ready"
    assert [ready_picture(i) for i in range(6)] == [
        "ready1", "ready2", "ready3", "ready1", "ready2", "ready3"
    ]
    assert picture_policy("DELAYED", transient="confirm") == "time"
    assert delayed_mode("POWER", 85) == "P85"
    assert delayed_mode("TEMPERATURE", 107) == "t107"
    assert delayed_mode("PROFILE", 2) == "pr2"
    assert picture_policy("IDLE", idle_ms=4_999, readings_valid=True, bottom_c=61) is None
    assert picture_policy("IDLE", idle_ms=5_000, readings_valid=True, bottom_c=61) == "hot"
    assert picture_policy("IDLE", idle_ms=6_999, readings_valid=True, bottom_c=61) == "hot"
    assert picture_policy("IDLE", idle_ms=7_000, readings_valid=True, bottom_c=61) == "blank"
    assert picture_policy("IDLE", idle_ms=7_999, readings_valid=True, bottom_c=61) == "blank"
    assert picture_policy("IDLE", idle_ms=8_000, readings_valid=True, bottom_c=61) == "hot"
    assert picture_policy("IDLE", idle_ms=50_000, readings_valid=True, bottom_c=60) == "sleep1"
    assert not sleep_allowed(readings_valid=True, bottom_c=61)
    assert sleep_allowed(readings_valid=True, bottom_c=60)
    assert sleep_allowed(readings_valid=False, bottom_c=99)
    assert transient_after_physical_input("cooking", physical_input=True) is None
    assert transient_after_physical_input("confirm", physical_input=True) is None
    assert transient_after_physical_input(
        "cooking", physical_input=True, action_picture="cancel") == "cancel"
    assert transient_after_physical_input("cooking", physical_input=False) == "cooking"
    assert live_screen_kind("STOPPING") == "FOCUS"
    assert start_at_view(False) == "START_AT_NO_CLOCK"
    assert start_at_view(True) == "START_AT_HOURS"
    print("POLICY TESTS: PASS")


if __name__ == "__main__":
    run()
