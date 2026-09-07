/* Compile the real driver; only hardware, RTOS scheduling and time are mocked. */
#include "fixture.h"
#include "../../../lab/power-test/main/powerboard_control.c"

void test_pb_reset(void) {
    powerboard_control_init();
    s_status.state = PB_STATE_STOPPED;
    s_status.confirmed_state = PB_STATE_STOPPED;
    s_status.stop_verified = true;
    s_status.valid_mask = 0xffff;
    s_status.registers[3] = 0xc0;
    s_status.registers[4] = 0x80;
    s_status.igbt_c = 40;
    s_status.bottom_c = 83;
    strlcpy(s_status.fault, "NONE", sizeof(s_status.fault));
}
void test_pb_feedback(uint8_t r20, uint8_t r26, uint16_t valid) {
    s_status.registers[0] = r20;
    s_status.registers[6] = r26;
    s_status.valid_mask = valid;
    ++s_status.completed_cycles;
    update_status_feedback();
}
void test_pb_sample(uint8_t bottom) { s_status.bottom_c = bottom; }
void test_pb_igbt(uint8_t temperature, bool valid) {
    ++s_status.igbt_sample_sequence;
    s_status.igbt_c = temperature;
    if (valid) s_status.valid_mask |= 1U << 3;
    else s_status.valid_mask &= ~(1U << 3);
}
void test_pb_bus_failure(unsigned duration_ms) {
    update_i2c_health_locked(host_now_us, PB_CRITICAL_READ_MASK, PB_CRITICAL_READ_MASK,
                             true, PB_ALL_WRITE_MASK, 0);
    host_now_us += (int64_t)duration_ms * 1000;
    update_i2c_health_locked(host_now_us, PB_CRITICAL_READ_MASK, PB_CRITICAL_READ_MASK,
                             true, PB_ALL_WRITE_MASK, 0);
}
void test_pb_safety(void) { update_time_and_safety(host_now_us); }
void test_pb_begin_hold(void) {
    s_status.state = PB_STATE_NO_PAN;
    begin_transition_locked(PB_TRANSITION_PAN_RETURN_HOLD, PB_STATE_ACTIVE_ZERO, 0);
}
void test_pb_force_fault(const char *reason) { fault_locked(reason); }
void test_pb_transmit(void) {
    /* Stand in for a successful complete command heartbeat; no real I2C.
     * Feedback must still be processed by the driver's actual C implementation. */
    if (!s_status.transition_pending) return;
    const uint8_t gear = s_status.transition_requested_gear;
    const bool zero = gear == 0;
    s_status.transmitted_gear = gear;
    s_status.transmitted_topology = zero ? 0x81 : topology_for_gear(gear);
    s_status.last_command_0d = s_status.transmitted_topology;
    s_status.last_command_00 = zero ? 0 : 1;
    s_status.last_command_0c = gear;
    s_status.transmitted_state = s_status.transition_requested_state;
    s_status.transition_command_transmitted = true;
    s_transition_feedback_baseline = s_status.feedback_sequence;
    if (!s_transition_deadline_us) {
        s_transition_deadline_us = host_now_us +
            (int64_t)transition_timeout_ms_locked() * 1000;
    }
}
