#include "sound.h"

#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "melody_tables.h"
#include "ui_outputs.h"

#define CRITICAL_BURSTS 3U
#define CRITICAL_MOTIFS_PER_BURST 2U
#define CRITICAL_BURST_GAP_MS 4000U

static QueueHandle_t s_queue;
static volatile bool s_enabled = true;
static volatile unsigned s_generation;

static bool mandatory_pattern(sound_pattern_t pattern)
{
    return pattern == SOUND_NO_PAN || pattern == SOUND_CRITICAL;
}

static bool wait_interruptible(unsigned milliseconds, unsigned generation)
{
    for (unsigned waited = 0; waited < milliseconds; waited += 20) {
        if (s_generation != generation) return false;
        vTaskDelay(pdMS_TO_TICKS(milliseconds - waited < 20 ? milliseconds - waited : 20));
    }
    return true;
}

static bool note_duty(unsigned hz, unsigned ms, unsigned pause_ms, unsigned duty_permille,
                      unsigned generation)
{
    if (s_generation != generation) return false;
    ui_buzzer_chirp_duty(hz, ms > 1000 ? 1000 : ms, duty_permille);
    if (!wait_interruptible(ms, generation)) return false;
    ui_buzzer_stop();
    return wait_interruptible(pause_ms, generation);
}

static bool note(unsigned hz, unsigned ms, unsigned pause_ms, unsigned generation)
{
    return note_duty(hz, ms, pause_ms, SELECTED_SOUND_NORMAL_DUTY_PERMILLE,
                     generation);
}

static bool play_table(const buzzer_note_t *notes, unsigned duty_permille,
                       unsigned generation)
{
    for (const buzzer_note_t *entry = notes;
         entry->frequency_hz != 0 || entry->on_ms != 0 || entry->gap_ms != 0;
         ++entry) {
        if (!note_duty(entry->frequency_hz, entry->on_ms, entry->gap_ms,
                       duty_permille, generation)) return false;
    }
    return s_generation == generation;
}

static void play(sound_pattern_t pattern)
{
    const unsigned generation = s_generation;
    if (!s_enabled && !mandatory_pattern(pattern)) return;
    switch (pattern) {
    case SOUND_UI_CLICK:
        note(3500, 35, 0, generation);
        break;
    case SOUND_BOOT:
        play_table(k_sound_boot, SELECTED_SOUND_NORMAL_DUTY_PERMILLE, generation);
        break;
    case SOUND_WAKE:
        play_table(k_sound_wake, SELECTED_SOUND_NORMAL_DUTY_PERMILLE, generation);
        break;
    case SOUND_SLEEP:
        play_table(k_sound_sleep, SELECTED_SOUND_SLEEP_DUTY_PERMILLE, generation);
        break;
    case SOUND_COMPLETE:
        play_table(k_sound_complete, SELECTED_SOUND_NORMAL_DUTY_PERMILLE, generation);
        break;
    case SOUND_STAGE:
        note(3500, 120, 90, generation);
        note(4200, 150, 0, generation);
        break;
    case SOUND_WARNING:
        note(3000, 180, 80, generation);
        note(3000, 180, 0, generation);
        break;
    case SOUND_NO_PAN:
        for (;;) {
            if (!play_table(k_sound_no_pan, SELECTED_SOUND_NORMAL_DUTY_PERMILLE,
                            generation)) return;
            if (!wait_interruptible(COOKER_NO_PAN_SOUND_PAUSE_MS, generation)) return;
        }
        break;
    case SOUND_CRITICAL:
        /* Three 5 s bursts (two motifs each), separated by 4 s, then silence. */
        for (unsigned burst = 0; burst < CRITICAL_BURSTS; ++burst) {
            for (unsigned motif = 0; motif < CRITICAL_MOTIFS_PER_BURST; ++motif) {
                if (!play_table(k_sound_critical, SELECTED_SOUND_NORMAL_DUTY_PERMILLE,
                                generation)) return;
            }
            if (burst + 1U < CRITICAL_BURSTS &&
                !wait_interruptible(CRITICAL_BURST_GAP_MS, generation)) return;
        }
        break;
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    sound_pattern_t pattern;
    while (xQueueReceive(s_queue, &pattern, portMAX_DELAY) == pdTRUE) play(pattern);
}

esp_err_t sound_init(void)
{
    s_queue = xQueueCreate(8, sizeof(sound_pattern_t));
    if (s_queue == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreate(sound_task, "sound", 3072, NULL, 4, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

void sound_set_enabled(bool enabled) { s_enabled = enabled; }

void sound_play(sound_pattern_t pattern)
{
    if (s_queue == NULL || (!s_enabled && !mandatory_pattern(pattern))) return;
    xQueueSend(s_queue, &pattern, 0);
}

void sound_stop(void)
{
    ++s_generation;
    if (s_queue != NULL) xQueueReset(s_queue);
    ui_buzzer_stop();
}
