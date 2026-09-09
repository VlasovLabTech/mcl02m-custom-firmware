#include "fixture.h"

static powerboard_status_t pb(void) {
    powerboard_status_t s; powerboard_control_get_status(&s); return s;
}
static cooker_snapshot_t cook(void) {
    cooker_snapshot_t s; cooking_engine_get_snapshot(&s); return s;
}
static void feedback(uint8_t r20, uint8_t r26) {
    host_now_us += 500000;
    test_pb_feedback(r20, r26, 0xffff);
    test_engine_tick();
    test_pb_safety();
}
static void start_zero(void) {
    test_engine_reset(58, 83);
    assert(test_engine_start() == ESP_OK);
    test_pb_transmit();
    feedback(0, 0); feedback(0, 0);
    assert(cook().state == COOK_STATE_COOKING && pb().state == PB_STATE_ACTIVE_ZERO);
}
static void start_heat(void) {
    test_engine_reset(125, 83);
    assert(test_engine_start() == ESP_OK);
    test_pb_transmit(); feedback(0, 2);
    assert(cook().state == COOK_STATE_COOKING);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
    if (!strcmp(scenario, "cool_pause_resume_zero")) {
        start_zero();
        for (unsigned i = 0; i < 180; ++i) feedback(0, 0);
        assert(cook().state == COOK_STATE_COOKING && cook().fault == FAULT_NONE);
        test_engine_pause(); test_pb_transmit(); feedback(0, 0); feedback(0, 0);
        assert(cook().state == COOK_STATE_PAUSED);
        test_engine_pause(); test_pb_transmit(); feedback(0, 0); feedback(0, 0);
        assert(cook().state == COOK_STATE_COOKING && pb().state == PB_STATE_ACTIVE_ZERO);
        test_pb_sample(57); feedback(0, 0);
        assert(pb().transition_pending && pb().transition_requested_gear > 0);
        test_pb_transmit(); feedback(0, 2);
        assert(pb().state == PB_STATE_HEATING && cook().fault == FAULT_NONE);
    } else if (!strcmp(scenario, "cool_pause_resume_heat")) {
        start_zero(); test_engine_pause(); test_pb_transmit(); feedback(0, 0); feedback(0, 0);
        test_pb_sample(40); feedback(0, 0);
        test_engine_pause();
        assert(pb().transition_pending && pb().transition_requested_gear > 0);
        test_pb_transmit(); feedback(0, 1);
        assert(cook().state == COOK_STATE_COOKING && pb().state == PB_STATE_HEATING);
    } else if (!strcmp(scenario, "lower_target_during_start")) {
        test_engine_reset(125, 83); assert(test_engine_start() == ESP_OK);
        test_pb_transmit();
        assert(cooking_set_temperature(58) == ESP_OK);
        test_pb_transmit(); feedback(0, 2);
        assert(cook().state == COOK_STATE_COOKING && pb().state == PB_STATE_ACTIVE_ZERO);
        for (unsigned i = 0; i < 20; ++i) feedback(0, 2);
        assert(cook().fault == FAULT_NONE);
    } else if (!strcmp(scenario, "first_heat_after_zero_ramp")) {
        start_zero(); assert(cooking_set_temperature(190) == ESP_OK);
        assert(pb().transition_requested_gear == 56);
        test_pb_transmit(); feedback(0, 2);
        assert(pb().state == PB_STATE_HEATING);
    } else if (!strcmp(scenario, "edited_start_topology")) {
        test_engine_reset(58, 40); assert(test_engine_start() == ESP_OK);
        assert(cooking_set_temperature(190) == ESP_OK);
        assert(pb().transition_requested_gear == 56);
    } else if (!strcmp(scenario, "pan_return_off_hold")) {
        start_heat(); feedback(2, 0); feedback(2, 0); feedback(2, 0);
        assert(cook().state == COOK_STATE_NO_PAN);
        test_pb_sample(130); feedback(0, 2); test_pb_transmit();
        feedback(0, 0); feedback(0, 0);
        assert(pb().transition_kind == PB_TRANSITION_PAN_RETURN_RESUME && pb().transition_pending);
        assert(pb().transition_requested_gear == 0);
        test_pb_transmit(); feedback(0, 0); feedback(0, 0);
        assert(cook().state == COOK_STATE_COOKING && pb().state == PB_STATE_ACTIVE_ZERO);
    } else if (!strcmp(scenario, "pan_lost_after_hold")) {
        start_heat(); test_pb_begin_hold(); test_pb_transmit();
        test_pb_feedback(0, 2, 0xffff); /* Hold completes before cooking consumes it. */
        test_pb_feedback(2, 0, 0xffff); test_pb_feedback(2, 0, 0xffff); test_pb_feedback(2, 0, 0xffff);
        assert(pb().state == PB_STATE_NO_PAN);
        test_engine_tick(); assert(cook().state == COOK_STATE_NO_PAN);
    } else if (!strcmp(scenario, "setpoint_pan_return_race")) {
        start_heat(); test_pb_begin_hold();
        assert(cooking_set_temperature(58) == ESP_OK);
        assert(cook().fault == FAULT_NONE && cook().target_temperature_c == 58);
        test_pb_transmit(); feedback(0, 2);
        assert(pb().transition_kind == PB_TRANSITION_PAN_RETURN_RESUME && pb().transition_requested_gear == 0);
    } else if (!strcmp(scenario, "profile_pan_return_race")) {
        start_heat(); test_pb_begin_hold(); test_engine_profile();
        assert(cook().fault == FAULT_NONE && cook().profile_stage_index == 1);
    } else if (!strcmp(scenario, "delayed_retry_after_timeout")) {
        test_engine_reset(125, 83); test_engine_schedule(); test_pb_transmit();
        assert(cook().delayed_start_attempts == 1);
        for (unsigned i = 0; i < 16; ++i) feedback(0, 0);
        assert(pb().state == PB_STATE_FAULT);
        test_engine_tick();
        assert(cook().fault == FAULT_NONE); /* Wait for Stop, not a false ECL. */
        feedback(0, 0); feedback(0, 0); feedback(0, 0); feedback(0, 0);
        assert(cook().state == COOK_STATE_STARTING && cook().delayed_start_attempts == 2);
        test_pb_transmit(); feedback(0, 2);
        assert(cook().state == COOK_STATE_COOKING && cook().fault == FAULT_NONE);
    } else if (!strcmp(scenario, "stale_confirmation_after_stop")) {
        test_engine_reset(125, 83); assert(test_engine_start() == ESP_OK);
        test_pb_transmit(); test_pb_feedback(0, 2, 0xffff);
        test_engine_stop(); feedback(0, 0); feedback(0, 0); feedback(0, 0);
        assert(cook().state == COOK_STATE_IDLE && cook().fault == FAULT_NONE);
    } else if (!strcmp(scenario, "stale_confirmation_after_fault")) {
        start_heat(); test_engine_stale_completion(); test_engine_tick();
        assert(cook().state == COOK_STATE_FAULT && cook().fault == FAULT_E02_NO_PAN_TIMEOUT);
    } else if (!strcmp(scenario, "first_fault_preserved")) {
        start_heat(); test_engine_fault(); test_pb_force_fault("I2C LOST"); test_engine_tick();
        assert(cook().fault == FAULT_E02_NO_PAN_TIMEOUT);
    } else if (!strcmp(scenario, "stop_needs_consecutive_samples")) {
        start_zero(); test_engine_stop();
        test_pb_feedback(0, 0, 0xffff); test_pb_feedback(0, 0, 0xffbf);
        test_pb_feedback(0, 0, 0xffff);
        assert(pb().state == PB_STATE_STOPPING);
        test_pb_feedback(0, 0, 0xffff); test_engine_tick();
        assert(cook().state == COOK_STATE_IDLE);
    } else if (!strcmp(scenario, "no_pan_needs_consecutive_samples")) {
        start_heat(); feedback(2, 0); feedback(2, 0);
        test_pb_feedback(2, 0, 0xfffe); feedback(2, 0);
        assert(pb().state == PB_STATE_HEATING);
        feedback(2, 0); feedback(2, 0); assert(pb().state == PB_STATE_NO_PAN);
    } else if (!strcmp(scenario, "heating_still_needs_ack")) {
        test_engine_reset(125, 83); assert(test_engine_start() == ESP_OK);
        test_pb_transmit();
        for (unsigned i = 0; i < 16; ++i) feedback(0, 0);
        test_engine_tick(); assert(cook().fault == FAULT_START_TIMEOUT);
    } else if (!strcmp(scenario, "zero_invalid_feedback")) {
        test_engine_reset(58, 83); assert(test_engine_start() == ESP_OK);
        test_pb_transmit(); test_pb_feedback(0, 0, 0xffff);
        test_pb_feedback(0, 0, 0xffbf); test_pb_feedback(0, 0, 0xffff);
        assert(pb().transition_pending);
        test_pb_feedback(0, 0, 0xffff); assert(pb().state == PB_STATE_ACTIVE_ZERO);
    } else if (!strcmp(scenario, "zero_timer_complete")) {
        start_zero(); assert(cooking_timer_set(1) == ESP_OK);
        for (unsigned i = 0; i < 10; ++i) { host_now_us += 100000; test_engine_tick(); }
        assert(cook().state == COOK_STATE_STOPPING);
        feedback(0, 0); feedback(0, 0);
        assert(cook().state == COOK_STATE_COMPLETE);
    } else if (!strcmp(scenario, "pause_two_hour_limit")) {
        start_zero(); test_engine_pause(); test_pb_transmit(); feedback(0, 0); feedback(0, 0);
        host_now_us += 7200000000LL; test_engine_tick();
        assert(cook().state == COOK_STATE_STOPPING && cook().fault == FAULT_NONE);
        feedback(0, 0); feedback(0, 0); assert(cook().state == COOK_STATE_IDLE);
    } else if (!strcmp(scenario, "igbt_requires_fresh_samples")) {
        start_zero(); test_pb_igbt(93, true);
        for (unsigned i = 0; i < 5; ++i) { host_now_us += 100000; test_engine_tick(); }
        assert(!cook().igbt_warning_active);
        test_pb_igbt(93, true); test_engine_tick(); assert(cook().igbt_warning_active);
        test_pb_igbt(91, true); test_engine_tick(); assert(!cook().igbt_warning_active);
    } else if (!strcmp(scenario, "native_fault_still_stops")) {
        start_zero(); feedback(0x17, 0); assert(cook().fault == FAULT_NONE);
        feedback(0x17, 0); assert(cook().fault == FAULT_E07_IGBT_OVERHEAT);
    } else if (!strcmp(scenario, "i2c_loss_still_stops")) {
        start_zero(); test_pb_bus_failure(15000); test_engine_tick();
        assert(cook().fault == FAULT_E09_COMMUNICATION);
        assert(!strcmp(pb().i2c_incident.reason, "CRITICAL LOSS"));
    } else if (!strcmp(scenario, "lease_loss_still_stops")) {
        start_zero(); host_now_us += 3000000; test_pb_safety(); test_engine_tick();
        assert(cook().fault == FAULT_COOKING_LEASE);
    } else if (!strcmp(scenario, "second_delayed_timeout_is_est")) {
        test_engine_reset(125, 83); test_engine_schedule(); test_pb_transmit();
        for (unsigned i = 0; i < 16; ++i) feedback(0, 0);
        test_engine_tick(); feedback(0, 0); feedback(0, 0); feedback(0, 0); feedback(0, 0);
        assert(cook().delayed_start_attempts == 2);
        test_pb_transmit(); for (unsigned i = 0; i < 16; ++i) feedback(0, 0);
        test_engine_tick(); assert(cook().fault == FAULT_START_TIMEOUT);
    } else if (!strcmp(scenario, "first_heat_after_zero_waits_eight_seconds")) {
        start_zero(); assert(cooking_set_temperature(190) == ESP_OK); test_pb_transmit();
        assert(pb().transition_remaining_ms == 8000);
        for (unsigned i = 0; i < 8; ++i) feedback(0, 0);
        feedback(0, 2); assert(pb().state == PB_STATE_HEATING && cook().fault == FAULT_NONE);
    } else {
        fprintf(stderr, "Unknown scenario: %s\n", scenario); return 2;
    }
    printf("PASS: %s\n", scenario);
    return 0;
}
