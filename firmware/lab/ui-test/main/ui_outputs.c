#include "ui_outputs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "pins.h"
#include "telemetry.h"

#define OLED_WIDTH 64
#define OLED_HEIGHT 48
#define OLED_PAGES 6
#define OLED_FRAME_BYTES (OLED_WIDTH * OLED_PAGES)

static SemaphoreHandle_t s_lock;
static uint8_t s_led_shadow[3];
static int64_t s_led_deadline_us;
static int64_t s_direct_deadline_us;
static int64_t s_buzzer_deadline_us;
static spi_device_handle_t s_oled;
static bool s_oled_initialized;
static uint8_t s_oled_frame[OLED_FRAME_BYTES];

static void led_send_byte(uint8_t value)
{
    for (unsigned bit = 0; bit < 8; ++bit) {
        gpio_set_level(PIN_LED_CLK, 0);
        esp_rom_delay_us(5);
        gpio_set_level(PIN_LED_DATA, value & 1U);
        gpio_set_level(PIN_LED_CLK, 1);
        esp_rom_delay_us(5);
        value >>= 1;
    }
}

static void led_begin_command(uint8_t command)
{
    gpio_set_level(PIN_LED_STB, 1);
    esp_rom_delay_us(5);
    gpio_set_level(PIN_LED_STB, 0);
    esp_rom_delay_us(5);
    led_send_byte(command);
}

static void led_end_command(void)
{
    esp_rom_delay_us(5);
    gpio_set_level(PIN_LED_STB, 1);
    esp_rom_delay_us(5);
}

static void led_write(unsigned logical_address, const uint8_t *data, size_t count)
{
    led_begin_command(0x00);
    led_begin_command(0x44);
    unsigned address = (logical_address & 0x7fU) << 1;
    for (size_t i = 0; i < count; ++i) {
        led_begin_command(0xc0U | (address & 0x0fU));
        led_send_byte(data[i]);
        address = (address + 2U) & 0xffU;
    }
    led_begin_command(0x8f);
    /* The final STB rising edge latches display-on and the RAM contents. */
    led_end_command();
}

static void led_apply_shadow(void)
{
    led_write(0, s_led_shadow, sizeof(s_led_shadow));
}

static void IRAM_ATTR oled_pre_transfer(spi_transaction_t *transaction)
{
    gpio_set_level(PIN_OLED_DC, (int)(uintptr_t)transaction->user);
}

static esp_err_t oled_send(const void *data, size_t bytes, bool is_data)
{
    spi_transaction_t transaction = {
        .length = bytes * 8,
        .tx_buffer = data,
        .user = (void *)(uintptr_t)(is_data ? 1 : 0),
    };
    return spi_device_transmit(s_oled, &transaction);
}

static esp_err_t oled_command(uint8_t command)
{
    return oled_send(&command, 1, false);
}

static esp_err_t oled_init_once(void)
{
    if (s_oled_initialized) {
        return ESP_OK;
    }

    gpio_config_t control = {
        .pin_bit_mask = (1ULL << PIN_OLED_RESET) | (1ULL << PIN_OLED_DC),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&control);
    if (err != ESP_OK) {
        return err;
    }

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_OLED_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_OLED_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = OLED_FRAME_BYTES,
    };
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_device_interface_config_t device = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = CONFIG_MCL02M_OLED_CS_GPIO,
        .queue_size = 1,
        .pre_cb = oled_pre_transfer,
    };
    err = spi_bus_add_device(SPI2_HOST, &device, &s_oled);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(PIN_OLED_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_OLED_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_OLED_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    static const uint8_t init_commands[] = {
        0xae, 0x00, 0x12, 0x40, 0x81, 0xff, 0xa0, 0xc0,
        0xa6, 0xa8, 0x2f, 0xd3, 0x00, 0xd5, 0x80, 0xd9,
        0x22, 0xda, 0x12, 0xdb, 0x00, 0x20, 0x02, 0x8d,
        0x14, 0xa4, 0xa6, 0xaf,
    };
    for (size_t i = 0; i < sizeof(init_commands); ++i) {
        err = oled_command(init_commands[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    s_oled_initialized = true;
    return ESP_OK;
}

static esp_err_t oled_flush(void)
{
    for (unsigned page = 0; page < OLED_PAGES; ++page) {
        esp_err_t err = oled_command(0xb0U + page);
        if (err != ESP_OK) return err;
        err = oled_command(0x00);
        if (err != ESP_OK) return err;
        err = oled_command(0x12);
        if (err != ESP_OK) return err;
        err = oled_send(&s_oled_frame[page * OLED_WIDTH], OLED_WIDTH, true);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static void oled_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    const size_t index = (size_t)(y / 8) * OLED_WIDTH + (size_t)x;
    const uint8_t mask = (uint8_t)(1U << (y & 7));
    if (on) {
        s_oled_frame[index] |= mask;
    } else {
        s_oled_frame[index] &= (uint8_t)~mask;
    }
}

static void oled_hline(int x0, int x1, int y)
{
    for (int x = x0; x <= x1; ++x) oled_set_pixel(x, y, true);
}

static void oled_vline(int x, int y0, int y1)
{
    for (int y = y0; y <= y1; ++y) oled_set_pixel(x, y, true);
}

static void oled_rect(int x, int y, int width, int height)
{
    oled_hline(x, x + width - 1, y);
    oled_hline(x, x + width - 1, y + height - 1);
    oled_vline(x, y, y + height - 1);
    oled_vline(x + width - 1, y, y + height - 1);
}

static void oled_fill_rect(int x, int y, int width, int height)
{
    for (int yy = y; yy < y + height; ++yy) {
        oled_hline(x, x + width - 1, yy);
    }
}

static void oled_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
    int dy = -dy_abs;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        oled_set_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        int twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void oled_circle(int center_x, int center_y, int radius)
{
    int x = radius;
    int y = 0;
    int error = 1 - radius;
    while (x >= y) {
        oled_set_pixel(center_x + x, center_y + y, true);
        oled_set_pixel(center_x + y, center_y + x, true);
        oled_set_pixel(center_x - y, center_y + x, true);
        oled_set_pixel(center_x - x, center_y + y, true);
        oled_set_pixel(center_x - x, center_y - y, true);
        oled_set_pixel(center_x - y, center_y - x, true);
        oled_set_pixel(center_x + y, center_y - x, true);
        oled_set_pixel(center_x + x, center_y - y, true);
        ++y;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            --x;
            error += 2 * (y - x) + 1;
        }
    }
}

static void oled_make_pattern(unsigned pattern)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    switch (pattern) {
    case 1: /* True 8x8 checkerboard. */
        for (int y = 0; y < OLED_HEIGHT; ++y) {
            for (int x = 0; x < OLED_WIDTH; ++x) {
                if (((x / 8) + (y / 8)) & 1) oled_set_pixel(x, y, true);
            }
        }
        break;
    case 2: /* Asymmetric orientation/geometry target. */
        oled_rect(0, 0, OLED_WIDTH, OLED_HEIGHT);
        oled_fill_rect(3, 3, 8, 8);          /* top-left: solid square */
        oled_rect(51, 3, 10, 10);            /* top-right: hollow square */
        oled_circle(8, 39, 5);                /* bottom-left: circle */
        oled_line(52, 33, 60, 45);            /* bottom-right: X */
        oled_line(60, 33, 52, 45);
        break;
    case 3: /* Exact 8-pixel grid. */
        for (int x = 0; x < OLED_WIDTH; x += 8) oled_vline(x, 0, OLED_HEIGHT - 1);
        oled_vline(OLED_WIDTH - 1, 0, OLED_HEIGHT - 1);
        for (int y = 0; y < OLED_HEIGHT; y += 8) oled_hline(0, OLED_WIDTH - 1, y);
        oled_hline(0, OLED_WIDTH - 1, OLED_HEIGHT - 1);
        break;
    case 4:
        memset(s_oled_frame, 0xff, sizeof(s_oled_frame));
        break;
    case 5: /* Smaller true 4x4 checkerboard. */
        for (int y = 0; y < OLED_HEIGHT; ++y) {
            for (int x = 0; x < OLED_WIDTH; ++x) {
                if (((x / 4) + (y / 4)) & 1) oled_set_pixel(x, y, true);
            }
        }
        break;
    case 6: /* Concentric circles reveal aspect ratio and clipping. */
        for (int radius = 4; radius <= 20; radius += 4) {
            oled_circle(31, 23, radius);
        }
        oled_set_pixel(31, 23, true);
        break;
    case 7: /* Diagonals and centre axes reveal orientation/order. */
        oled_rect(0, 0, OLED_WIDTH, OLED_HEIGHT);
        oled_line(0, 0, OLED_WIDTH - 1, OLED_HEIGHT - 1);
        oled_line(OLED_WIDTH - 1, 0, 0, OLED_HEIGHT - 1);
        oled_hline(0, OLED_WIDTH - 1, OLED_HEIGHT / 2);
        oled_vline(OLED_WIDTH / 2, 0, OLED_HEIGHT - 1);
        break;
    default:
        break;
    }
}

static const uint8_t s_font_digits[10][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
};

static const uint8_t s_font_upper[26][5] = {
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
};

/* Upper-case Russian 5x7 glyphs U+0410..U+042F; lower-case maps here too. */
static const uint8_t s_font_cyrillic[32][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, /* А */
    {0x7f,0x49,0x49,0x49,0x31}, /* Б */
    {0x7f,0x49,0x49,0x49,0x36}, /* В */
    {0x7f,0x01,0x01,0x01,0x01}, /* Г */
    {0x60,0x3e,0x21,0x3f,0x60}, /* Д */
    {0x7f,0x49,0x49,0x49,0x41}, /* Е */
    {0x77,0x08,0x7f,0x08,0x77}, /* Ж */
    {0x22,0x41,0x49,0x49,0x36}, /* З */
    {0x7f,0x20,0x10,0x08,0x7f}, /* И */
    {0x7e,0x20,0x11,0x08,0x7e}, /* Й */
    {0x7f,0x08,0x14,0x22,0x41}, /* К */
    {0x60,0x1e,0x01,0x01,0x7f}, /* Л */
    {0x7f,0x02,0x0c,0x02,0x7f}, /* М */
    {0x7f,0x08,0x08,0x08,0x7f}, /* Н */
    {0x3e,0x41,0x41,0x41,0x3e}, /* О */
    {0x7f,0x01,0x01,0x01,0x7f}, /* П */
    {0x7f,0x09,0x09,0x09,0x06}, /* Р */
    {0x3e,0x41,0x41,0x41,0x22}, /* С */
    {0x01,0x01,0x7f,0x01,0x01}, /* Т */
    {0x07,0x48,0x48,0x48,0x3f}, /* У */
    {0x1c,0x22,0x7f,0x22,0x1c}, /* Ф */
    {0x63,0x14,0x08,0x14,0x63}, /* Х */
    {0x3f,0x20,0x20,0x3f,0x60}, /* Ц */
    {0x0f,0x08,0x08,0x08,0x7f}, /* Ч */
    {0x7f,0x40,0x7f,0x40,0x7f}, /* Ш */
    {0x3f,0x20,0x3f,0x20,0x7f}, /* Щ */
    {0x01,0x7f,0x48,0x48,0x30}, /* Ъ */
    {0x7f,0x48,0x30,0x00,0x7f}, /* Ы */
    {0x7f,0x48,0x48,0x48,0x30}, /* Ь */
    {0x22,0x41,0x49,0x49,0x3e}, /* Э */
    {0x7f,0x08,0x3e,0x41,0x3e}, /* Ю */
    {0x46,0x29,0x19,0x09,0x7f}, /* Я */
};

/*
 * Compact Simplified Chinese subset used by the production UI.  Keeping the
 * table local avoids carrying a multi-megabyte CJK font for a 64x48 display.
 * Glyphs are 8x8, column-major, and intentionally limited to strings that can
 * actually appear on the cooker.
 */
typedef struct {
    uint32_t codepoint;
    uint8_t columns[8];
} oled_cjk_glyph_t;

static const oled_cjk_glyph_t s_font_cjk[] = {
    {0x4E2D, {0x3e,0x12,0x12,0xff,0x12,0x12,0x3e,0x00}}, /* 中 */
    {0x6587, {0x82,0x86,0x5a,0x23,0x23,0x5a,0x86,0x82}}, /* 文 */
    {0x529F, {0x42,0x3e,0xa2,0x62,0x3f,0x06,0x82,0xfe}}, /* 功 */
    {0x7387, {0x41,0x55,0x57,0xfd,0xfd,0x55,0x55,0x41}}, /* 率 */
    {0x6E29, {0x49,0x20,0xf0,0xdf,0xfe,0xff,0xff,0xa0}}, /* 温 */
    {0x5EA6, {0xa8,0x7f,0x93,0xbf,0xdf,0xdf,0xb7,0x83}}, /* 度 */
    {0x9884, {0x08,0xfd,0x0f,0x08,0xbd,0x77,0x75,0xbd}}, /* 预 */
    {0x8BBE, {0x04,0x7d,0x60,0x9f,0xbb,0x58,0x9b,0x84}}, /* 设 */
    {0x4FE1, {0x0e,0xff,0x01,0xfe,0x7f,0x7f,0xfe,0x02}}, /* 信 */
    {0x606F, {0x80,0x5f,0x5f,0xbf,0xbf,0x9f,0xdf,0xc0}}, /* 息 */
    {0x542F, {0x80,0x7e,0xfa,0xda,0x5b,0xdb,0x5a,0xfe}}, /* 启 */
    {0x52A8, {0x64,0x5d,0x65,0x24,0xc2,0x3f,0x82,0xfe}}, /* 动 */
    {0x65F6, {0x7f,0x49,0x7f,0x00,0x02,0xb2,0xff,0x56}}, /* 时 */
    {0x949F, {0x16,0xff,0xf6,0x00,0x3c,0x36,0xff,0x3e}}, /* 钟 */
    {0x5EF6, {0x81,0x6d,0x7b,0xbc,0xe9,0xe1,0xff,0xc8}}, /* 延 */
    {0x8BED, {0x0c,0x7d,0x08,0xfe,0x5f,0x5b,0xfe,0x2c}}, /* 语 */
    {0x8A00, {0x02,0xff,0x7e,0xff,0xff,0x7e,0xff,0x02}}, /* 言 */
    {0x58F0, {0x91,0x7f,0x2f,0x3b,0x3b,0x2f,0x3f,0x01}}, /* 声 */
    {0x97F3, {0x04,0xfd,0x7f,0xfd,0xfd,0x7f,0xfd,0x04}}, /* 音 */
    {0x663E, {0xb0,0xaf,0xff,0x8f,0x8f,0xff,0xaf,0xb0}}, /* 显 */
    {0x793A, {0x44,0x35,0x05,0xfd,0x7d,0x05,0x35,0x64}}, /* 示 */
    {0x4F11, {0x08,0xfe,0x41,0x22,0x3a,0xff,0x3a,0x62}}, /* 休 */
    {0x7720, {0x7f,0x5a,0x7f,0xff,0xde,0x1a,0x7e,0xdb}}, /* 眠 */
    {0x5206, {0x0c,0x86,0x69,0x18,0x88,0x89,0x7e,0x0c}}, /* 分 */
    {0x5C4F, {0x84,0x7f,0xaa,0x7e,0x2a,0x2b,0xfe,0x2b}}, /* 屏 */
    {0x5E55, {0x55,0xff,0x7b,0x7f,0x7f,0xff,0xff,0x55}}, /* 幕 */
    {0x533A, {0xff,0x81,0xa5,0x99,0x99,0xa5,0x81,0x00}}, /* 区 */
    {0x6062, {0x0c,0xff,0xd2,0x3b,0x9b,0x72,0x7b,0x8a}}, /* 恢 */
    {0x590D, {0x01,0xaf,0xbe,0x7f,0x7e,0xfe,0xbe,0x80}}, /* 复 */
    {0x51FA, {0x72,0x47,0x44,0x7f,0x7f,0xc4,0x47,0xf2}}, /* 出 */
    {0x5382, {0x82,0x7f,0x01,0x01,0x01,0x01,0x01,0x01}}, /* 厂 */
    {0x5B9E, {0x91,0x91,0x5b,0x35,0x3d,0x71,0x51,0x91}}, /* 实 */
    {0x6570, {0xaa,0xe6,0xff,0x2a,0x84,0x7b,0x7e,0x96}}, /* 数 */
    {0x636E, {0x12,0xff,0xda,0x40,0xff,0xdf,0x5f,0xff}}, /* 据 */
    {0x5B9A, {0x8a,0x6b,0x0a,0xcb,0xfb,0xaa,0xab,0x8a}}, /* 定 */
    {0x5F00, {0x18,0xd9,0x3f,0x19,0x19,0xff,0x19,0x18}}, /* 开 */
    {0x5173, {0x98,0x9a,0x5b,0x3e,0x3e,0x5b,0x9a,0x98}}, /* 关 */
    {0x6309, {0x12,0xff,0x1a,0x82,0xba,0x4f,0x7b,0x9a}}, /* 按 */
    {0x952E, {0x35,0xfd,0xdb,0xf6,0xf6,0xff,0xff,0xc6}}, /* 键 */
    {0x4FDD, {0x08,0xfe,0x01,0x5b,0x3f,0xfd,0x3f,0x5a}}, /* 保 */
    {0x5B58, {0x12,0xfa,0x03,0x13,0x96,0xff,0x17,0x12}}, /* 存 */
    {0x5668, {0x29,0xeb,0xfb,0xcf,0xcf,0xfb,0xef,0x2d}}, /* 器 */
    {0x79D2, {0x25,0xff,0x14,0x8c,0x84,0x5f,0x20,0x3e}}, /* 秒 */
    {0x5C0F, {0x30,0x1c,0x80,0xff,0x7f,0x00,0x0c,0x30}}, /* 小 */
    {0x95F4, {0xfc,0x3c,0x5a,0x42,0x5a,0xbc,0xff,0x00}}, /* 间 */
    {0x95ED, {0xfc,0x00,0x28,0x7d,0x0d,0x81,0xff,0x00}}, /* 闭 */
    {0x786E, {0x19,0x7f,0x79,0xf8,0x7d,0x7d,0x3f,0xfc}}, /* 确 */
    {0x8BA4, {0x04,0x7d,0x60,0xe0,0x70,0x3f,0x70,0x80}}, /* 认 */
    {0x540E, {0x80,0x7f,0x25,0xf5,0x55,0xd4,0x54,0xf4}}, /* 后 */
    {0x7A7A, {0x82,0x97,0x92,0xf3,0xf3,0x92,0x97,0x82}}, /* 空 */
    {0x9636, {0xff,0x45,0x3f,0xc4,0x7a,0x01,0xfa,0x0c}}, /* 阶 */
    {0x6BB5, {0x60,0xff,0x0a,0x80,0xbb,0x68,0xbb,0x92}}, /* 段 */
    {0x7535, {0x7e,0x2a,0x2b,0xff,0xaa,0xaa,0xbe,0xc0}}, /* 电 */
    {0x6E90, {0x44,0x80,0xff,0x5c,0x9e,0xff,0x1e,0x40}}, /* 源 */
    {0x72B6, {0x22,0xff,0x00,0xc4,0x74,0x1f,0x74,0x85}}, /* 状 */
    {0x6001, {0x5a,0x2b,0x66,0x83,0xbb,0x86,0x8b,0x5a}}, /* 态 */
    {0x5BC6, {0x49,0xc5,0x95,0xdb,0xdd,0x99,0xcd,0x49}}, /* 密 */
    {0x7801, {0x19,0x7f,0x79,0x26,0x2e,0x28,0xaf,0xfc}}, /* 码 */
    {0x672A, {0x48,0x2b,0x0a,0xdf,0xdf,0x0a,0x2b,0x48}}, /* 未 */
    {0x8FDE, {0xa8,0xf9,0xa2,0xae,0xab,0xff,0xaa,0xa2}}, /* 连 */
    {0x63A5, {0x12,0xff,0x1a,0x9a,0xfe,0xdb,0xfe,0xba}}, /* 接 */
    {0x5DF2, {0xfd,0x99,0x99,0x99,0x99,0x9f,0xce,0x00}}, /* 已 */
    {0x6B63, {0x80,0xf9,0x81,0xa7,0xff,0x99,0x99,0x80}}, /* 正 */
    {0x5E38, {0x42,0x6a,0x3f,0xff,0xff,0x3f,0x6a,0x02}}, /* 常 */
    {0x540C, {0xff,0x39,0x2f,0x2f,0x2f,0xb9,0xff,0x00}}, /* 同 */
    {0x6B65, {0x08,0xaf,0x88,0x6c,0x6f,0x4a,0x2b,0x08}}, /* 步 */
    {0x70ED, {0x8a,0x3f,0x87,0xb0,0x1f,0x8b,0x1e,0xb8}}, /* 热 */
    {0x70B9, {0x98,0x3c,0xa4,0xa7,0x27,0xa5,0x3d,0x99}}, /* 点 */
    {0x5C31, {0x5f,0xf7,0x77,0x9e,0xff,0x7e,0xff,0xc2}}, /* 就 */
    {0x7EEA, {0x56,0x5d,0x46,0xb4,0xff,0x7e,0xfe,0x05}}, /* 绪 */
    {0x6253, {0x12,0xff,0x1e,0x02,0x81,0x81,0xff,0x01}}, /* 打 */
    {0x7F51, {0xff,0x24,0x1c,0x24,0x24,0x1c,0xa4,0xff}}, /* 网 */
    {0x9875, {0x94,0xbe,0x42,0x33,0x32,0x42,0x5e,0x94}}, /* 页 */
    {0x91CD, {0x86,0xff,0xff,0xff,0xfe,0xfe,0xfe,0x86}}, /* 重 */
    {0x7F6E, {0x85,0xf7,0x9f,0xcf,0xcf,0x9f,0xf7,0x85}}, /* 置 */
    {0x5168, {0x84,0xa4,0xab,0xf9,0xf9,0xab,0xa4,0x84}}, /* 全 */
    {0x90E8, {0x28,0xfd,0x5d,0xfd,0xe8,0xff,0x45,0x3f}}, /* 部 */
    {0x4F4F, {0x08,0xfe,0x01,0x82,0x92,0xff,0x92,0x82}}, /* 住 */
    {0x53D6, {0x61,0x7f,0x5d,0xff,0x81,0x5e,0x7e,0x94}}, /* 取 */
    {0x6D88, {0x44,0x21,0x00,0xfd,0x3c,0x3f,0x3c,0xfd}}, /* 消 */
    {0x52A0, {0x82,0x7f,0x82,0xe6,0x7c,0xfe,0x42,0xfe}}, /* 加 */
    {0x6682, {0x09,0xef,0xff,0xf8,0xff,0xf3,0xfe,0x06}}, /* 暂 */
    {0x505C, {0x08,0xfe,0x21,0x35,0xff,0xff,0x7f,0x35}}, /* 停 */
    {0x65E0, {0x88,0x49,0x29,0x1f,0x7f,0x89,0x89,0xc8}}, /* 无 */
    {0x9505, {0x36,0xff,0x76,0xf9,0x2f,0x3e,0x2f,0xf9}}, /* 锅 */
    {0x5B8C, {0x91,0x95,0x75,0x15,0x15,0x75,0x95,0x91}}, /* 完 */
    {0x6210, {0xb0,0x7e,0x4b,0xfa,0x5f,0x23,0x5b,0xeb}}, /* 成 */
    {0x6545, {0xf2,0x5f,0xda,0xf4,0xbf,0x62,0xbe,0x96}}, /* 故 */
    {0x969C, {0xff,0x4d,0x33,0x56,0x7f,0xff,0x7f,0x46}}, /* 障 */
    {0x76EE, {0xff,0x55,0xd5,0x55,0xd5,0x55,0xff,0x00}}, /* 目 */
    {0x6807, {0x12,0xff,0x5e,0x24,0xa5,0xfd,0x05,0x64}}, /* 标 */
    {0x5F53, {0x45,0x54,0x54,0x57,0xd4,0x54,0xfd,0x00}}, /* 当 */
    {0x524D, {0x03,0xff,0x3f,0xff,0x7b,0x3b,0xfa,0x4b}}, /* 前 */
    {0x6863, {0x12,0xff,0x1e,0x45,0xd4,0xd7,0x54,0xfd}}, /* 档 */
    {0x4F4D, {0x0e,0xff,0x82,0xba,0xa2,0xc3,0xba,0x92}}, /* 位 */
    {0x8F93, {0x2e,0xff,0xfb,0xfa,0xfb,0x32,0x33,0xfa}}, /* 输 */
    {0x8FBE, {0x84,0x7d,0x40,0xa2,0xb2,0x8f,0x92,0xa2}}, /* 达 */
    {0x4E0A, {0x80,0x80,0x80,0xff,0x84,0x84,0x84,0x80}}, /* 上 */
    {0x9650, {0xff,0x44,0x3b,0xb9,0xdf,0x32,0x7f,0x91}}, /* 限 */
    {0x7B49, {0x3b,0x3d,0x7d,0x3f,0xbf,0xfd,0x3d,0x39}}, /* 等 */
    {0x5F85, {0x0a,0xfd,0x06,0x1c,0x9f,0x9f,0xff,0x1c}}, /* 待 */
    {0x7761, {0xff,0xff,0x18,0x7e,0xff,0xff,0xfe,0x7e}}, /* 睡 */
    {0x7B80, {0x13,0xf5,0x79,0x7f,0x7f,0xbb,0xff,0x45}}, /* 简 */
    {0x4F53, {0x0e,0xff,0x12,0x4a,0xff,0x4e,0x12,0x20}}, /* 体 */
};

typedef struct {
    const uint8_t *columns;
    uint8_t width;
    uint8_t height;
} oled_glyph_view_t;

static const uint8_t *oled_glyph(char c)
{
    static const uint8_t blank[5] = {0};
    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t equals[5] = {0x14, 0x14, 0x14, 0x14, 0x14};
    static const uint8_t less_than[5] = {0x08, 0x14, 0x22, 0x41, 0x00};
    static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
    static const uint8_t exclamation[5] = {0x00, 0x00, 0x5f, 0x00, 0x00};
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return s_font_digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return s_font_upper[c - 'A'];
    switch (c) {
    case ':': return colon;
    case '-': return dash;
    case '.': return dot;
    case '/': return slash;
    case '=': return equals;
    case '<': return less_than;
    case '?': return question;
    case '!': return exclamation;
    default: return blank;
    }
}

static oled_glyph_view_t oled_glyph_unicode(uint32_t codepoint)
{
    static const uint8_t yo[5] = {0x7d,0x54,0x54,0x54,0x45};
    static const uint8_t degree[5] = {0x06,0x09,0x09,0x06,0x00};
    if (codepoint == 0x00b0) return (oled_glyph_view_t){degree, 5, 7};
    if (codepoint >= 0x0430 && codepoint <= 0x044f) codepoint -= 0x20;
    if (codepoint == 0x0451) codepoint = 0x0401;
    if (codepoint == 0x0401) return (oled_glyph_view_t){yo, 5, 7};
    if (codepoint >= 0x0410 && codepoint <= 0x042f)
        return (oled_glyph_view_t){s_font_cyrillic[codepoint - 0x0410], 5, 7};
    for (size_t i = 0; i < sizeof(s_font_cjk) / sizeof(s_font_cjk[0]); ++i) {
        if (s_font_cjk[i].codepoint == codepoint)
            return (oled_glyph_view_t){s_font_cjk[i].columns, 8, 8};
    }
    return (oled_glyph_view_t){oled_glyph(codepoint <= 0x7f ? (char)codepoint : '?'),
                               5, 7};
}

static uint32_t oled_utf8_next(const char **text)
{
    const uint8_t *bytes = (const uint8_t *)*text;
    if (bytes[0] < 0x80) {
        *text += 1;
        return bytes[0];
    }
    if ((bytes[0] & 0xe0) == 0xc0 && (bytes[1] & 0xc0) == 0x80) {
        *text += 2;
        return ((uint32_t)(bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);
    }
    if ((bytes[0] & 0xf0) == 0xe0 && (bytes[1] & 0xc0) == 0x80 &&
        (bytes[2] & 0xc0) == 0x80) {
        *text += 3;
        return ((uint32_t)(bytes[0] & 0x0f) << 12) |
               ((uint32_t)(bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
    }
    *text += 1;
    return '?';
}

static unsigned oled_utf8_length(const char *text)
{
    unsigned length = 0;
    while (text != NULL && *text != '\0') {
        (void)oled_utf8_next(&text);
        ++length;
    }
    return length;
}

static void oled_draw_scaled_text(const char *text, int x, int y,
                                  unsigned scale_x, unsigned scale_y)
{
    while (text != NULL && *text != '\0') {
        const oled_glyph_view_t glyph = oled_glyph_unicode(oled_utf8_next(&text));
        for (unsigned glyph_x = 0; glyph_x < glyph.width; ++glyph_x) {
            for (unsigned glyph_y = 0; glyph_y < glyph.height; ++glyph_y) {
                if ((glyph.columns[glyph_x] & (1U << glyph_y)) == 0) continue;
                for (unsigned dx = 0; dx < scale_x; ++dx)
                    for (unsigned dy = 0; dy < scale_y; ++dy)
                        oled_set_pixel(x + (int)(glyph_x * scale_x + dx),
                                       y + (int)(glyph_y * scale_y + dy), true);
            }
        }
        x += (int)((glyph.width + 1U) * scale_x);
    }
}

static void oled_draw_compact_text(const char *text, int x, int y,
                                   unsigned scale_x, unsigned scale_y)
{
    while (text != NULL && *text != '\0') {
        const oled_glyph_view_t glyph = oled_glyph_unicode(oled_utf8_next(&text));
        for (unsigned glyph_x = 0; glyph_x < glyph.width; ++glyph_x) {
            for (unsigned glyph_y = 0; glyph_y < glyph.height; ++glyph_y) {
                if ((glyph.columns[glyph_x] & (1U << glyph_y)) == 0) continue;
                for (unsigned dx = 0; dx < scale_x; ++dx)
                    for (unsigned dy = 0; dy < scale_y; ++dy)
                        oled_set_pixel(x + (int)(glyph_x * scale_x + dx),
                                       y + (int)(glyph_y * scale_y + dy), true);
            }
        }
        x += (int)(glyph.width * scale_x);
    }
}

static int oled_text_width(const char *text, unsigned scale_x, bool compact)
{
    int width = 0;
    bool first = true;
    while (text != NULL && *text != '\0') {
        const oled_glyph_view_t glyph = oled_glyph_unicode(oled_utf8_next(&text));
        if (!compact && !first) width += (int)scale_x;
        width += (int)(glyph.width * scale_x);
        first = false;
    }
    return width;
}

static int oled_text_height(const char *text, unsigned scale_y)
{
    unsigned height = 0;
    while (text != NULL && *text != '\0') {
        const oled_glyph_view_t glyph = oled_glyph_unicode(oled_utf8_next(&text));
        if (glyph.height > height) height = glyph.height;
    }
    return (int)(height * scale_y);
}

static void oled_draw_centered(const char *text, int y, unsigned scale_x, unsigned scale_y)
{
    const int width = oled_text_width(text, scale_x, false);
    oled_draw_scaled_text(text, (OLED_WIDTH - width) / 2, y, scale_x, scale_y);
}

static void oled_draw_right(const char *text, int y, unsigned scale_x, unsigned scale_y)
{
    const int width = oled_text_width(text, scale_x, false);
    oled_draw_scaled_text(text, OLED_WIDTH - width, y, scale_x, scale_y);
}

static void oled_draw_centered_fit(const char *text, int y,
                                   unsigned scale_x, unsigned scale_y)
{
    const int regular_width = oled_text_width(text, scale_x, false);
    if (regular_width <= OLED_WIDTH) {
        oled_draw_scaled_text(text, (OLED_WIDTH - regular_width) / 2,
                              y, scale_x, scale_y);
        return;
    }
    const int compact_width = oled_text_width(text, scale_x, true);
    oled_draw_compact_text(text, (OLED_WIDTH - compact_width) / 2,
                           y, scale_x, scale_y);
}

static void oled_draw_edge_groups(const char *left, const char *right, int y)
{
    if (left != NULL && *left != '\0') oled_draw_scaled_text(left, 0, y, 1, 1);
    if (right != NULL && *right != '\0') oled_draw_right(right, y, 1, 1);
}

static void oled_draw_main_value(const char *value, bool degree)
{
    const unsigned glyphs = oled_utf8_length(value);
    const unsigned scale = glyphs <= 3 ? 3U : 2U;
    const int text_width = oled_text_width(value, scale, false);
    const int degree_gap = degree ? 1 : 0;
    const int degree_width = degree ? 5 : 0;
    const int total_width = text_width + degree_gap + degree_width;
    const int height = 7 * (int)scale;
    const int x = (OLED_WIDTH - total_width) / 2;
    const int y = 10 + (21 - height) / 2;
    oled_draw_scaled_text(value, x, y, scale, scale);
    if (degree) oled_draw_scaled_text("°", x + text_width + degree_gap, y, 1, 1);
}

static void oled_draw_prime(int x, int y)
{
    oled_set_pixel(x, y, true);
    oled_set_pixel(x, y + 1, true);
    oled_set_pixel(x, y + 2, true);
}

static void oled_draw_duration(const char *value, int y, bool seconds)
{
    const int text_width = oled_text_width(value, 2, false);
    const int marks_width = seconds ? 5 : 2;
    const int total_width = text_width + 1 + marks_width;
    const int x = (OLED_WIDTH - total_width) / 2;
    oled_draw_scaled_text(value, x, y, 2, 2);
    const int mark_x = x + text_width + 1;
    oled_draw_prime(mark_x, y + 1);
    if (seconds) oled_draw_prime(mark_x + 3, y + 1);
}

static void oled_make_menu_item(unsigned index, const char *label, const char *subtitle)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));

    char number[4];
    snprintf(number, sizeof(number), "%u", index);
    if (subtitle != NULL && *subtitle != '\0') {
        oled_draw_centered(number, 2, 1, 1);
        oled_draw_centered_fit(subtitle, 14, 1, 1);
    } else {
        oled_draw_centered(number, 2, 1, 1);
    }

    const unsigned glyphs = oled_utf8_length(label);
    const unsigned scale_x = glyphs <= 5 && oled_text_width(label, 2, false) <= OLED_WIDTH ?
                             2U : 1U;
    const unsigned scale_y = 2U;
    const int width = oled_text_width(label, scale_x, false);
    const int height = oled_text_height(label, scale_y);
    const int area_top = 21;
    const int area_height = 20;
    oled_draw_scaled_text(label, (OLED_WIDTH - width) / 2,
                          area_top + (area_height - height) / 2, scale_x, scale_y);
}

static void oled_make_large_number(unsigned value)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    char number[4];
    snprintf(number, sizeof(number), "%u", value > 99 ? 99 : value);
    const unsigned scale = 3;
    const int width = oled_text_width(number, scale, false);
    oled_draw_scaled_text(number, (OLED_WIDTH - width) / 2, 10, scale, scale);
}

static void oled_make_focus(const char *top_left, const char *top_right,
                            const char *value, bool degree, const char *bottom)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    oled_draw_edge_groups(top_left, top_right, 0);
    oled_draw_main_value(value, degree);
    if (bottom != NULL && *bottom != '\0') oled_draw_centered_fit(bottom, 41, 1, 1);
}

static void oled_make_temperature_editor(unsigned setpoint_c, unsigned current_c,
                                         bool current_valid)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    char selected[12];
    char current[12];
    snprintf(selected, sizeof(selected), "S%u°", setpoint_c);
    if (current_valid) snprintf(current, sizeof(current), "%u°", current_c);
    else strlcpy(current, "--°", sizeof(current));
    oled_draw_centered_fit(selected, 5, 2, 2);
    oled_draw_centered_fit(current, 29, 2, 2);
}

static void oled_make_cooking(const char *top_left, const char *top_right,
                              const char *value, bool degree, const char *timer,
                              bool timer_seconds, const char *bottom)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    oled_draw_edge_groups(top_left, top_right, 0);
    oled_draw_main_value(value, degree);
    if (timer != NULL && *timer != '\0') oled_draw_duration(timer, 34, timer_seconds);
    else if (bottom != NULL && *bottom != '\0') oled_draw_centered_fit(bottom, 41, 1, 1);
}

static void oled_make_timer(const char *value, bool timer_seconds, unsigned position)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    static const int y[3] = {0, 17, 34};
    oled_draw_duration(value, y[position % 3], timer_seconds);
}

static void oled_make_time_editor(const char *title, const char *value, const char *footer)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    if (title != NULL && *title != '\0') oled_draw_centered_fit(title, 0, 1, 1);
    if (value != NULL && *value != '\0') {
        const unsigned glyphs = oled_utf8_length(value);
        const unsigned scale = glyphs <= 5 ? 2U : 1U;
        oled_draw_centered_fit(value, scale == 2 ? 17 : 20, scale, scale);
    }
    if (footer != NULL && *footer != '\0') oled_draw_centered_fit(footer, 40, 1, 1);
}

static void oled_make_complete(const char *text)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    const int regular_width = oled_text_width(text, 2, false);
    const int compact_width = oled_text_width(text, 2, true);
    const char *space = strchr(text, ' ');
    if (regular_width <= OLED_WIDTH) {
        oled_draw_centered(text, 17, 2, 2);
    } else if (compact_width <= OLED_WIDTH) {
        oled_draw_compact_text(text, (OLED_WIDTH - compact_width) / 2, 17, 2, 2);
    } else if (space != NULL) {
        char first[32] = {0};
        const size_t first_bytes = (size_t)(space - text);
        if (first_bytes < sizeof(first)) memcpy(first, text, first_bytes);
        oled_draw_centered_fit(first, 8, 2, 2);
        oled_draw_centered_fit(space + 1, 27, 2, 2);
    } else {
        oled_draw_centered_fit(text, 20, 1, 1);
    }
}

static void oled_draw_info_line(int y, const char *label, unsigned value,
                                const char *unit, bool valid)
{
    char text[16];
    if (valid) snprintf(text, sizeof(text), "%u%s", value, unit);
    else strlcpy(text, "--", sizeof(text));
    oled_draw_scaled_text(label, 0, y, 1, 1);
    oled_draw_right(text, y, 1, 1);
}

static void oled_make_info(unsigned voltage_v, unsigned ntc_c, unsigned igbt_c, bool valid)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    oled_draw_info_line(0, "VOLT", voltage_v, "V", valid);
    oled_draw_info_line(16, "NTC", ntc_c, "°C", valid);
    oled_draw_info_line(32, "IGBT", igbt_c, "°C", valid);
}

static void oled_make_version(const char *firmware_title, const char *firmware_version,
                              const char *board_title, const char *board_revision)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    /* Four small rows, vertically centered and consistently left-aligned. */
    oled_draw_scaled_text(firmware_title, 0, 5, 1, 1);
    oled_draw_scaled_text(firmware_version, 0, 15, 1, 1);
    oled_draw_scaled_text(board_title, 0, 27, 1, 1);
    oled_draw_scaled_text(board_revision, 0, 37, 1, 1);
}

static void oled_make_sleep_clock(const char *text, unsigned y, unsigned alignment)
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    const int width = oled_text_width(text, 1, false);
    int x = (OLED_WIDTH - width) / 2;
    if (alignment == 1) x = 0;
    else if (alignment == 2) x = OLED_WIDTH - width;
    oled_draw_scaled_text(text, x, (int)y, 1, 1);
}

static void oled_make_text(const char *lines[UI_OLED_TEXT_LINES])
{
    memset(s_oled_frame, 0, sizeof(s_oled_frame));
    for (unsigned row = 0; row < UI_OLED_TEXT_LINES; ++row) {
        if (lines[row] == NULL || *lines[row] == '\0') continue;
        if (oled_text_width(lines[row], 1, false) <= OLED_WIDTH)
            oled_draw_scaled_text(lines[row], 0, (int)row * 10, 1, 1);
        else
            oled_draw_compact_text(lines[row], 0, (int)row * 10, 1, 1);
    }
}

static void outputs_watchdog_task(void *arg)
{
    (void)arg;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_led_deadline_us != 0 && now >= s_led_deadline_us) {
            memset(s_led_shadow, 0, sizeof(s_led_shadow));
            led_apply_shadow();
            s_led_deadline_us = 0;
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"output_timeout\",\"output\":\"serial_led\"}", now / 1000);
        }
        if (s_direct_deadline_us != 0 && now >= s_direct_deadline_us) {
            gpio_set_level(PIN_UI_DIRECT_0, 0);
            gpio_set_level(PIN_UI_DIRECT_1, 0);
            s_direct_deadline_us = 0;
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"output_timeout\",\"output\":\"direct_ui\"}", now / 1000);
        }
        if (s_buzzer_deadline_us != 0 && now >= s_buzzer_deadline_us) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            s_buzzer_deadline_us = 0;
            telemetry_emitf("{\"t_ms\":%lld,\"type\":\"output_timeout\",\"output\":\"buzzer\"}", now / 1000);
        }
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t ui_outputs_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_LED_STB) | (1ULL << PIN_LED_CLK) |
                        (1ULL << PIN_LED_DATA) | (1ULL << PIN_UI_DIRECT_0) |
                        (1ULL << PIN_UI_DIRECT_1),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&outputs);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(PIN_LED_STB, 1);
    gpio_set_level(PIN_LED_CLK, 1);
    gpio_set_level(PIN_LED_DATA, 0);
    gpio_set_level(PIN_UI_DIRECT_0, 0);
    gpio_set_level(PIN_UI_DIRECT_1, 0);
    memset(s_led_shadow, 0, sizeof(s_led_shadow));
    led_apply_shadow();

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }
    ledc_channel_config_t channel = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(outputs_watchdog_task, "output_guard", 3072, NULL, 7, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"outputs_safe\",\"state\":\"all_off\"}", esp_timer_get_time() / 1000);
    return ESP_OK;
}

void ui_outputs_all_off(const char *reason)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_led_shadow, 0, sizeof(s_led_shadow));
    led_apply_shadow();
    gpio_set_level(PIN_UI_DIRECT_0, 0);
    gpio_set_level(PIN_UI_DIRECT_1, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    s_led_deadline_us = 0;
    s_direct_deadline_us = 0;
    s_buzzer_deadline_us = 0;
    if (s_oled_initialized) {
        memset(s_oled_frame, 0, sizeof(s_oled_frame));
        oled_flush();
    }
    xSemaphoreGive(s_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"all_off\",\"reason\":\"%s\"}",
                    esp_timer_get_time() / 1000, reason == NULL ? "unspecified" : reason);
}

esp_err_t ui_led_raw_pulse(unsigned index, uint8_t value, unsigned duration_ms)
{
    if (index >= 3 || duration_ms < 50 || duration_ms > 3000) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_led_shadow, 0, sizeof(s_led_shadow));
    s_led_shadow[index] = value;
    led_apply_shadow();
    s_led_deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    xSemaphoreGive(s_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"ui_output\",\"output\":\"serial_led\","
                    "\"index\":%u,\"value\":%u,\"timeout_ms\":%u}",
                    esp_timer_get_time() / 1000, index, value, duration_ms);
    return ESP_OK;
}

esp_err_t ui_led_raw_set(unsigned index, uint8_t value)
{
    if (index >= 3) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_led_shadow, 0, sizeof(s_led_shadow));
    s_led_shadow[index] = value;
    led_apply_shadow();
    s_led_deadline_us = 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t ui_led_power_level(unsigned level, unsigned duration_ms)
{
    static const uint8_t levels[9][3] = {
        {0x00, 0x00, 0x01}, {0x00, 0x08, 0x01}, {0x00, 0x0c, 0x01},
        {0x00, 0x0e, 0x01}, {0x00, 0x0f, 0x01}, {0x08, 0x0f, 0x01},
        {0x0c, 0x0f, 0x01}, {0x0e, 0x0f, 0x01}, {0x0f, 0x0f, 0x01},
    };
    if (level > 9 || duration_ms < 50 || duration_ms > 5000) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (level == 0) {
        memset(s_led_shadow, 0, sizeof(s_led_shadow));
    } else {
        memcpy(s_led_shadow, levels[level - 1], sizeof(s_led_shadow));
    }
    led_apply_shadow();
    s_led_deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    xSemaphoreGive(s_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"ui_output\",\"output\":\"power_leds\","
                    "\"level\":%u,\"timeout_ms\":%u}",
                    esp_timer_get_time() / 1000, level, duration_ms);
    return ESP_OK;
}

esp_err_t ui_led_power_set(unsigned level)
{
    static const uint8_t levels[9][3] = {
        {0x00, 0x00, 0x01}, {0x00, 0x08, 0x01}, {0x00, 0x0c, 0x01},
        {0x00, 0x0e, 0x01}, {0x00, 0x0f, 0x01}, {0x08, 0x0f, 0x01},
        {0x0c, 0x0f, 0x01}, {0x0e, 0x0f, 0x01}, {0x0f, 0x0f, 0x01},
    };
    if (level > 9) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (level == 0) memset(s_led_shadow, 0, sizeof(s_led_shadow));
    else memcpy(s_led_shadow, levels[level - 1], sizeof(s_led_shadow));
    led_apply_shadow();
    s_led_deadline_us = 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t ui_led_panel_set(unsigned power_level, bool orange, bool blue)
{
    static const uint8_t levels[9][3] = {
        {0x00, 0x00, 0x01}, {0x00, 0x08, 0x01}, {0x00, 0x0c, 0x01},
        {0x00, 0x0e, 0x01}, {0x00, 0x0f, 0x01}, {0x08, 0x0f, 0x01},
        {0x0c, 0x0f, 0x01}, {0x0e, 0x0f, 0x01}, {0x0f, 0x0f, 0x01},
    };
    if (power_level > 9) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (power_level == 0) memset(s_led_shadow, 0, sizeof(s_led_shadow));
    else memcpy(s_led_shadow, levels[power_level - 1], sizeof(s_led_shadow));
    /* Hardware test: byte 2 bit 1 is orange, bit 2 is blue. */
    if (orange) s_led_shadow[2] |= 0x02;
    if (blue) s_led_shadow[2] |= 0x04;
    led_apply_shadow();
    s_led_deadline_us = 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t ui_direct_output_pulse(unsigned gpio, unsigned level, unsigned duration_ms)
{
    if ((gpio != PIN_UI_DIRECT_0 && gpio != PIN_UI_DIRECT_1) || level > 1 ||
        duration_ms < 50 || duration_ms > 2000) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    gpio_set_level(PIN_UI_DIRECT_0, 0);
    gpio_set_level(PIN_UI_DIRECT_1, 0);
    gpio_set_level((gpio_num_t)gpio, level);
    s_direct_deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    xSemaphoreGive(s_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"ui_output\",\"output\":\"direct_ui\","
                    "\"gpio\":%u,\"level\":%u,\"timeout_ms\":%u}",
                    esp_timer_get_time() / 1000, gpio, level, duration_ms);
    return ESP_OK;
}

esp_err_t ui_direct_output_set(unsigned gpio, unsigned level)
{
    if ((gpio != PIN_UI_DIRECT_0 && gpio != PIN_UI_DIRECT_1) || level > 1)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    gpio_set_level((gpio_num_t)gpio, level);
    s_direct_deadline_us = 0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t ui_oled_show_pattern(unsigned pattern)
{
    if (pattern > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_pattern(pattern);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"ui_output\",\"output\":\"oled\","
                    "\"pattern\":%u,\"esp_err\":%d,\"cs_gpio\":%d}",
                    esp_timer_get_time() / 1000, pattern, err, CONFIG_MCL02M_OLED_CS_GPIO);
    return err;
}

esp_err_t ui_oled_show_bitmap(const uint8_t bitmap[UI_OLED_BITMAP_BYTES])
{
    if (bitmap == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        memcpy(s_oled_frame, bitmap, OLED_FRAME_BYTES);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_bitmap_text(const uint8_t bitmap[UI_OLED_BITMAP_BYTES],
                                   const char *text, int x, int y, unsigned scale)
{
    if (bitmap == NULL || text == NULL || *text == '\0' || scale == 0 || scale > 2 ||
        x < 0 || x + oled_text_width(text, scale, false) > OLED_WIDTH ||
        y < 0 || y + (int)(7U * scale) > OLED_HEIGHT)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        memcpy(s_oled_frame, bitmap, OLED_FRAME_BYTES);
        oled_draw_scaled_text(text, x, y, scale, scale);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_bitmap_right_text(const uint8_t bitmap[UI_OLED_BITMAP_BYTES],
                                         const char *top, int top_y,
                                         const char *bottom, int bottom_y)
{
    if (bitmap == NULL || top == NULL || bottom == NULL ||
        (*top == '\0' && *bottom == '\0') ||
        top_y < 0 || bottom_y < 0 ||
        top_y + oled_text_height(top, 1) > OLED_HEIGHT ||
        bottom_y + oled_text_height(bottom, 1) > OLED_HEIGHT ||
        oled_text_width(top, 1, false) > OLED_WIDTH ||
        oled_text_width(bottom, 1, false) > OLED_WIDTH)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        memcpy(s_oled_frame, bitmap, OLED_FRAME_BYTES);
        if (*top != '\0') oled_draw_right(top, top_y, 1, 1);
        if (*bottom != '\0') oled_draw_right(bottom, bottom_y, 1, 1);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_overlay_debug_counter(unsigned value)
{
    char text[2];
    snprintf(text, sizeof(text), "%u", value > 6U ? 6U : value);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        /* Debug row 2: small text at the absolute left edge, overlap allowed. */
        oled_draw_scaled_text(text, 0, 10, 1, 1);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_text(const char *lines[UI_OLED_TEXT_LINES])
{
    if (lines == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_text(lines);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_menu_item(unsigned index, const char *label, const char *subtitle)
{
    if (index == 0 || index > 99 || label == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_menu_item(index, label, subtitle);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_large_number(unsigned value)
{
    if (value > 99) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_large_number(value);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_focus(const char *top_left, const char *top_right,
                             const char *value, bool degree, const char *bottom)
{
    if (value == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_focus(top_left, top_right, value, degree, bottom);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_temperature_editor(unsigned setpoint_c, unsigned current_c,
                                          bool current_valid)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_temperature_editor(setpoint_c, current_c, current_valid);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_cooking(const char *top_left, const char *top_right,
                               const char *value, bool degree, const char *timer,
                               bool timer_seconds, const char *bottom)
{
    if (value == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_cooking(top_left, top_right, value, degree, timer,
                          timer_seconds, bottom);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_timer(const char *value, bool timer_seconds, unsigned position)
{
    if (value == NULL || position > 2) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_timer(value, timer_seconds, position);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_time_editor(const char *title, const char *value,
                                   const char *footer)
{
    if (value == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_time_editor(title, value, footer);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_complete(const char *text)
{
    if (text == NULL || *text == '\0') return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_complete(text);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_info(unsigned voltage_v, unsigned ntc_c, unsigned igbt_c, bool valid)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_info(voltage_v, ntc_c, igbt_c, valid);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_version(const char *firmware_title, const char *firmware_version,
                               const char *board_title, const char *board_revision)
{
    if (firmware_title == NULL || *firmware_title == '\0' ||
        firmware_version == NULL || *firmware_version == '\0' ||
        board_title == NULL || *board_title == '\0' ||
        board_revision == NULL || *board_revision == '\0')
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_version(firmware_title, firmware_version, board_title, board_revision);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_show_sleep_clock(const char *text, unsigned y, unsigned alignment)
{
    if (text == NULL || *text == '\0' || y > OLED_HEIGHT - 7 || alignment > 2)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = oled_init_once();
    if (err == ESP_OK) {
        oled_make_sleep_clock(text, y, alignment);
        err = oled_flush();
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_oled_power(bool enabled)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = enabled ? oled_init_once() : ESP_OK;
    if (err == ESP_OK && s_oled_initialized) err = oled_command(enabled ? 0xaf : 0xae);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ui_buzzer_chirp_duty(unsigned frequency_hz, unsigned duration_ms,
                               unsigned duty_permille)
{
    if (frequency_hz < 200 || frequency_hz > 10000 || duration_ms < 20 ||
        duration_ms > 1000 || duty_permille == 0 || duty_permille > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t duty = (duty_permille * 1024U + 500U) / 1000U;
    if (duty > 1023U) duty = 1023U;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* ESP-IDF 6 returns esp_err_t here (older releases returned the actual frequency). */
    esp_err_t err = ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequency_hz);
    if (err == ESP_OK) {
        err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    }
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    if (err == ESP_OK) {
        s_buzzer_deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    }
    xSemaphoreGive(s_lock);
    telemetry_emitf("{\"t_ms\":%lld,\"type\":\"ui_output\",\"output\":\"buzzer\","
                    "\"frequency_hz\":%u,\"timeout_ms\":%u,\"duty_permille\":%u,"
                    "\"esp_err\":%d}", esp_timer_get_time() / 1000, frequency_hz,
                    duration_ms, duty_permille, err);
    return err;
}

esp_err_t ui_buzzer_chirp(unsigned frequency_hz, unsigned duration_ms)
{
    return ui_buzzer_chirp_duty(frequency_hz, duration_ms, 500U);
}

void ui_buzzer_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    s_buzzer_deadline_us = 0;
    xSemaphoreGive(s_lock);
}
