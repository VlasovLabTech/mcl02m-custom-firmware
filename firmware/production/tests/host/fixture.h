#pragma once
#include "host_runtime.h"
#include "cooking_engine.h"
#include "powerboard_control.h"
void test_pb_reset(void);
void test_pb_feedback(uint8_t r20, uint8_t r26, uint16_t valid);
void test_pb_transmit(void);
void test_pb_safety(void);
void test_pb_sample(uint8_t bottom);
void test_pb_igbt(uint8_t temperature, bool valid);
void test_pb_bus_failure(unsigned duration_ms);
void test_pb_begin_hold(void);
void test_pb_force_fault(const char *reason);
void test_engine_reset(unsigned target, unsigned bottom);
int test_engine_start(void);
void test_engine_tick(void);
void test_engine_pause(void);
void test_engine_stop(void);
void test_engine_ack(void);
void test_engine_schedule(void);
void test_engine_profile(void);
void test_engine_fault(void);
void test_engine_force_sample(powerboard_status_t *pb);
void test_engine_stale_completion(void);
