#pragma once

#include <stddef.h>
#include <stdint.h>

#define OLED_ASSET_V2_WIDTH 64U
#define OLED_ASSET_V2_HEIGHT 48U
#define OLED_ASSET_V2_FRAME_BYTES 384U
#define OLED_ASSET_V2_COUNT 36U

typedef struct {
    const char *filename;
    const uint8_t *frame;
} oled_asset_v2_entry_t;

extern const uint8_t oled_image_v2_cancel[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_confirm[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_coocking3[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_error[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_frustrated[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_happy[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_hi[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_hot[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_intro1[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_intro2[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_love[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_noopls[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_nopan[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_nowifi[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready2[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready3[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready4[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready5[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready6[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready7[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_ready8[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_sad[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_scary[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_silent[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_sleep1[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_sleep2[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_sleep3[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_startcooking[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_time[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_toohot[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_wait[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_wakeup[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_whatisgoingon[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_wifipresent[OLED_ASSET_V2_FRAME_BYTES];
extern const uint8_t oled_image_v2_wifisearch[OLED_ASSET_V2_FRAME_BYTES];

extern const oled_asset_v2_entry_t
    oled_assets_v2[OLED_ASSET_V2_COUNT];
