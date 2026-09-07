#include "host_runtime.h"
#include "settings.h"
#include "sound.h"
int64_t host_now_us = 1000000;
void telemetry_emit(const char *s) { (void)s; }
void telemetry_emitf(const char *s, ...) { (void)s; }
void sound_play(sound_pattern_t p) { (void)p; }
void sound_cancel(sound_pattern_t p) { (void)p; }
void sound_stop(void) {}
unsigned sound_start_count(sound_pattern_t p) { (void)p; return 0; }
unsigned sound_completion_count(sound_pattern_t p) { (void)p; return 0; }
void settings_profiles_get(cooker_profile_t p[COOKER_PROFILE_COUNT]) { memset(p, 0, sizeof(*p) * COOKER_PROFILE_COUNT); }
unsigned settings_profile_stage_count(const cooker_profile_t *p) {
    unsigned n = 0; for (unsigned i = 0; i < COOKER_PROFILE_STAGE_COUNT; ++i) if (p->stages[i].timer_s) ++n; return n;
}
