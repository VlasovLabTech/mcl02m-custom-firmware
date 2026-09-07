#include "fixture.h"
#include "../../main/cooking_engine.c"

void test_engine_reset(unsigned target, unsigned bottom) {
    cooking_engine_init();
    test_pb_reset();
    s_status.state = COOK_STATE_READY;
    s_status.mode = COOK_MODE_TEMPERATURE;
    s_status.target_temperature_c = target;
    s_status.bottom_c = bottom;
    s_status.readings_valid = true;
    test_pb_sample((uint8_t)bottom);
}
int test_engine_start(void) { return begin_run_locked(); }
void test_engine_tick(void) {
    powerboard_status_t pb;
    powerboard_control_get_status(&pb);
    if (update_schedule_locked(host_now_us)) powerboard_control_get_status(&pb);
    apply_power_status_locked(&pb, host_now_us);
    update_manual_pause_timeout_locked(host_now_us);
    update_timer_locked(100000);
    update_temperature_locked(host_now_us);
    renew_cooking_lease_locked();
}
void test_engine_pause(void) { intent_t intent = {.type = INTENT_PAUSE_RESUME}; handle_intent_locked(&intent); }
void test_engine_stop(void) { intent_t intent = {.type = INTENT_STOP}; handle_intent_locked(&intent); }
void test_engine_ack(void) { intent_t intent = {.type = INTENT_ACK}; handle_intent_locked(&intent); }
void test_engine_schedule(void) {
    intent_t intent = {.type = INTENT_SCHEDULE_REL, .value = 1};
    handle_intent_locked(&intent);
    host_now_us += 1000000;
    test_engine_tick();
}
void test_engine_fault(void) { set_fault_locked(FAULT_E02_NO_PAN_TIMEOUT, "E02 NO PAN"); }
void test_engine_force_sample(powerboard_status_t *pb) { apply_power_status_locked(pb, host_now_us); }
void test_engine_stale_completion(void) {
    s_status.state = COOK_STATE_FAULT;
    s_status.fault = FAULT_E02_NO_PAN_TIMEOUT;
    s_applied_transition_generation = 0;
}
void test_engine_profile(void) {
    s_status.mode = COOK_MODE_PROFILE;
    s_status.state = COOK_STATE_COOKING;
    s_status.profile_stage_mode = COOK_MODE_POWER;
    s_status.profile_stage_index = 1;
    s_status.timer_remaining_s = 0;
    s_status.timer_enabled = true;
    s_profile_selected = true;
    s_active_profile.stages[1] = (cooker_profile_stage_t){
        .mode = COOK_MODE_POWER, .gear = 20, .timer_s = 60
    };
    update_timer_locked(100000);
}
