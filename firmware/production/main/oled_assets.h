#pragma once

#include <stdint.h>

#define OLED_ASSET_WIDTH 64U
#define OLED_ASSET_HEIGHT 48U
#define OLED_ASSET_FRAME_BYTES 384U

extern const uint8_t oled_image_cancel[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_confirm[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_cooking[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_error[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_no_pan[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_ready[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_sleep_warning[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_sleep[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_turn_on[OLED_ASSET_FRAME_BYTES];
extern const uint8_t oled_image_wakeup[OLED_ASSET_FRAME_BYTES];
