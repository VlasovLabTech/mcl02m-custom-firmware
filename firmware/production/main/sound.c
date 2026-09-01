#include "sound.h"

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "melody_tables.h"
#include "ui_outputs.h"

#ifndef MCL02M_PRIVATE_SOUND_BUILD
#define MCL02M_PRIVATE_SOUND_BUILD 0
#endif

#if MCL02M_PRIVATE_SOUND_BUILD
#include "private_sound_tables.h"
#endif

#define CRITICAL_BURSTS 3U
#define CRITICAL_MOTIFS_PER_BURST 2U
#define CRITICAL_BURST_GAP_MS 4000U
#define IGBT_WARNING_FREQUENCY_HZ 4000U
#define IGBT_WARNING_ON_MS 300U
#define IGBT_WARNING_GAP_MS 100U

_Static_assert(COOKER_NO_PAN_TIMEOUT_MS == NUTCRACKER_PAS_DE_DEUX_DURATION_MS,
               "NoPan timeout must match the complete warning melody");

typedef struct {
    sound_pattern_t pattern;
    unsigned global_generation;
    unsigned pattern_generation;
    bool protected_request;
} sound_request_t;

static QueueHandle_t s_queue;
static volatile bool s_enabled = true;
static unsigned s_global_generation;
static unsigned s_pattern_generation[SOUND_PATTERN_COUNT];
static unsigned s_start_count[SOUND_PATTERN_COUNT];
static unsigned s_completion_count[SOUND_PATTERN_COUNT];
static unsigned s_protected_pending;
static int s_current_pattern = -1;

static bool mandatory_pattern(sound_pattern_t pattern)
{
    return pattern == SOUND_IGBT_WARNING || pattern == SOUND_NO_PAN ||
           pattern == SOUND_CRITICAL;
}

static bool protected_pattern(sound_pattern_t pattern)
{
    return pattern == SOUND_WAKE || pattern == SOUND_SLEEP ||
           pattern == SOUND_IGBT_WARNING || pattern == SOUND_NO_PAN ||
           pattern == SOUND_CRITICAL;
}

static bool request_valid(const sound_request_t *request)
{
    return __atomic_load_n(&s_global_generation, __ATOMIC_ACQUIRE) ==
               request->global_generation &&
           __atomic_load_n(&s_pattern_generation[request->pattern],
                           __ATOMIC_ACQUIRE) == request->pattern_generation;
}

static bool wait_interruptible(unsigned milliseconds, const sound_request_t *request)
{
    for (unsigned waited = 0; waited < milliseconds; waited += 20) {
        if (!request_valid(request)) return false;
        vTaskDelay(pdMS_TO_TICKS(milliseconds - waited < 20 ?
                                 milliseconds - waited : 20));
    }
    return request_valid(request);
}

static bool note_duty(unsigned hz, unsigned ms, unsigned pause_ms,
                      unsigned duty_permille, const sound_request_t *request)
{
    if (!request_valid(request)) return false;
    ui_buzzer_chirp_duty(hz, ms > 1000 ? 1000 : ms, duty_permille);
    if (!wait_interruptible(ms, request)) return false;
    ui_buzzer_stop();
    return wait_interruptible(pause_ms, request);
}

static bool note(unsigned hz, unsigned ms, unsigned pause_ms,
                 const sound_request_t *request)
{
    return note_duty(hz, ms, pause_ms, SELECTED_SOUND_NORMAL_DUTY_PERMILLE,
                     request);
}

static bool play_table(const buzzer_note_t *notes, unsigned duty_permille,
                       const sound_request_t *request)
{
    for (const buzzer_note_t *entry = notes;
         entry->frequency_hz != 0 || entry->on_ms != 0 || entry->gap_ms != 0;
         ++entry) {
        if (!note_duty(entry->frequency_hz, entry->on_ms, entry->gap_ms,
                       duty_permille, request)) return false;
    }
    return request_valid(request);
}

#if MCL02M_PRIVATE_SOUND_BUILD
static bool play_private_table(private_sound_table_t table,
                               const sound_request_t *request)
{
    for (size_t index = 0;; ++index) {
        private_sound_note_t entry;
        if (!private_sound_note(table, index, &entry)) break;
        if (!note_duty(entry.frequency_hz, entry.on_ms, entry.gap_ms,
                       private_sound_duty_permille(), request)) return false;
    }
    return request_valid(request);
}
#endif

static bool play(const sound_request_t *request)
{
    const sound_pattern_t pattern = request->pattern;
    if (!s_enabled && !mandatory_pattern(pattern)) return false;
    switch (pattern) {
    case SOUND_UI_CLICK:
        return note(3500, 35, 0, request);
    case SOUND_BOOT:
        return play_table(k_sound_boot, SELECTED_SOUND_NORMAL_DUTY_PERMILLE, request);
    case SOUND_WAKE:
#if MCL02M_PRIVATE_SOUND_BUILD
        return play_private_table(PRIVATE_SOUND_WAKE, request);
#else
        return play_table(k_sound_wake, SELECTED_SOUND_NORMAL_DUTY_PERMILLE, request);
#endif
    case SOUND_SLEEP:
#if MCL02M_PRIVATE_SOUND_BUILD
        return play_private_table(PRIVATE_SOUND_SLEEP, request);
#else
        return play_table(k_sound_sleep, SELECTED_SOUND_SLEEP_DUTY_PERMILLE, request);
#endif
    case SOUND_COMPLETE:
        return play_table(k_sound_complete, SELECTED_SOUND_NORMAL_DUTY_PERMILLE,
                          request);
    case SOUND_STAGE:
        return note(3500, 120, 90, request) && note(4200, 150, 0, request);
    case SOUND_WARNING:
        return note(3000, 180, 80, request) && note(3000, 180, 0, request);
    case SOUND_IGBT_WARNING:
        return note(IGBT_WARNING_FREQUENCY_HZ, IGBT_WARNING_ON_MS,
                    IGBT_WARNING_GAP_MS, request) &&
               note(IGBT_WARNING_FREQUENCY_HZ, IGBT_WARNING_ON_MS,
                    IGBT_WARNING_GAP_MS, request) &&
               note(IGBT_WARNING_FREQUENCY_HZ, IGBT_WARNING_ON_MS, 0, request);
    case SOUND_NO_PAN:
        return play_table(k_sound_midi_nutcracker_pas_de_deux,
                          SELECTED_SOUND_NORMAL_DUTY_PERMILLE, request);
    case SOUND_CRITICAL:
        /* Three 5 s bursts (two motifs each), separated by 4 s, then silence. */
        for (unsigned burst = 0; burst < CRITICAL_BURSTS; ++burst) {
            for (unsigned motif = 0; motif < CRITICAL_MOTIFS_PER_BURST; ++motif) {
                if (!play_table(k_sound_critical,
                                SELECTED_SOUND_NORMAL_DUTY_PERMILLE,
                                request)) return false;
            }
            if (burst + 1U < CRITICAL_BURSTS &&
                !wait_interruptible(CRITICAL_BURST_GAP_MS, request)) return false;
        }
        return request_valid(request);
    case SOUND_PATTERN_COUNT:
        return false;
    }
    return false;
}

static void sound_task(void *arg)
{
    (void)arg;
    sound_request_t request;
    while (xQueueReceive(s_queue, &request, portMAX_DELAY) == pdTRUE) {
        bool completed = false;
        if (request_valid(&request)) {
            __atomic_add_fetch(&s_start_count[request.pattern], 1U,
                               __ATOMIC_RELEASE);
            __atomic_store_n(&s_current_pattern, (int)request.pattern,
                             __ATOMIC_RELEASE);
            completed = play(&request);
            __atomic_store_n(&s_current_pattern, -1, __ATOMIC_RELEASE);
        }
        if (completed && request_valid(&request))
            __atomic_add_fetch(&s_completion_count[request.pattern], 1U,
                               __ATOMIC_RELEASE);
        const bool same_global = request.global_generation ==
            __atomic_load_n(&s_global_generation, __ATOMIC_ACQUIRE);
        if (request.protected_request && same_global)
            __atomic_sub_fetch(&s_protected_pending, 1U, __ATOMIC_RELEASE);
    }
}

esp_err_t sound_init(void)
{
    s_queue = xQueueCreate(8, sizeof(sound_request_t));
    if (s_queue == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreate(sound_task, "sound", 3072, NULL, 4, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

void sound_set_enabled(bool enabled) { s_enabled = enabled; }

void sound_play(sound_pattern_t pattern)
{
    if (s_queue == NULL || pattern >= SOUND_PATTERN_COUNT ||
        (!s_enabled && !mandatory_pattern(pattern))) return;
    const bool protect = protected_pattern(pattern);
    if (!protect &&
        __atomic_load_n(&s_protected_pending, __ATOMIC_ACQUIRE) != 0) return;
    sound_request_t request = {
        .pattern = pattern,
        .global_generation = __atomic_load_n(&s_global_generation, __ATOMIC_ACQUIRE),
        .pattern_generation = __atomic_load_n(&s_pattern_generation[pattern],
                                              __ATOMIC_ACQUIRE),
        .protected_request = protect,
    };
    if (protect)
        __atomic_add_fetch(&s_protected_pending, 1U, __ATOMIC_RELEASE);
    if (xQueueSend(s_queue, &request, 0) != pdTRUE && protect)
        __atomic_sub_fetch(&s_protected_pending, 1U, __ATOMIC_RELEASE);
}

void sound_cancel(sound_pattern_t pattern)
{
    if (pattern >= SOUND_PATTERN_COUNT) return;
    __atomic_add_fetch(&s_pattern_generation[pattern], 1U, __ATOMIC_RELEASE);
    if (__atomic_load_n(&s_current_pattern, __ATOMIC_ACQUIRE) == (int)pattern)
        ui_buzzer_stop();
}

unsigned sound_start_count(sound_pattern_t pattern)
{
    return pattern < SOUND_PATTERN_COUNT ?
           __atomic_load_n(&s_start_count[pattern], __ATOMIC_ACQUIRE) : 0;
}

unsigned sound_completion_count(sound_pattern_t pattern)
{
    return pattern < SOUND_PATTERN_COUNT ?
           __atomic_load_n(&s_completion_count[pattern], __ATOMIC_ACQUIRE) : 0;
}

void sound_stop(void)
{
    __atomic_add_fetch(&s_global_generation, 1U, __ATOMIC_RELEASE);
    if (s_queue != NULL) xQueueReset(s_queue);
    __atomic_store_n(&s_protected_pending, 0U, __ATOMIC_RELEASE);
    ui_buzzer_stop();
}
