#include "tft_driver.h"
#include "app_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "tft";

static spi_device_handle_t s_spi = NULL;

/* ========== ILI9341 Commands ========== */
#define ILI9341_NOP         0x00
#define ILI9341_SWRESET     0x01
#define ILI9341_SLPOUT      0x11
#define ILI9341_DISPON      0x29
#define ILI9341_CASET       0x2A
#define ILI9341_PASET       0x2B
#define ILI9341_RAMWR       0x2C
#define ILI9341_MADCTL      0x36
#define ILI9341_PIXFMT      0x3A
#define ILI9341_FRMCTR1     0xB1
#define ILI9341_DFUNCTR     0xB6
#define ILI9341_PWCTR1      0xC0
#define ILI9341_PWCTR2      0xC1
#define ILI9341_VMCTR1      0xC5
#define ILI9341_VMCTR2      0xC7
#define ILI9341_GMCTRP1     0xE0
#define ILI9341_GMCTRN1     0xE1

/* MADCTL bits */
#define MADCTL_MY   0x80
#define MADCTL_MX   0x40
#define MADCTL_MV   0x20
#define MADCTL_ML   0x10
#define MADCTL_RGB  0x00
#define MADCTL_BGR  0x08

/* 5x7 font (ASCII 32~127) */
static const uint8_t font5x7[] = {
    0x00,0x00,0x00,0x00,0x00, /* space */  0x00,0x00,0x5F,0x00,0x00, /* ! */
    0x00,0x07,0x00,0x07,0x00, /* " */      0x14,0x7F,0x14,0x7F,0x14, /* # */
    0x24,0x2A,0x7F,0x2A,0x12, /* $ */      0x23,0x13,0x08,0x64,0x62, /* % */
    0x36,0x49,0x55,0x22,0x50, /* & */      0x00,0x05,0x03,0x00,0x00, /* ' */
    0x00,0x1C,0x22,0x41,0x00, /* ( */      0x00,0x41,0x22,0x1C,0x00, /* ) */
    0x08,0x2A,0x1C,0x2A,0x08, /* * */      0x08,0x08,0x3E,0x08,0x08, /* + */
    0x00,0x50,0x30,0x00,0x00, /* , */      0x08,0x08,0x08,0x08,0x08, /* - */
    0x00,0x60,0x60,0x00,0x00, /* . */      0x20,0x10,0x08,0x04,0x02, /* / */
    0x3E,0x51,0x49,0x45,0x3E, /* 0 */      0x00,0x42,0x7F,0x40,0x00, /* 1 */
    0x42,0x61,0x51,0x49,0x46, /* 2 */      0x21,0x41,0x45,0x4B,0x31, /* 3 */
    0x18,0x14,0x12,0x7F,0x10, /* 4 */      0x27,0x45,0x45,0x45,0x39, /* 5 */
    0x3C,0x4A,0x49,0x49,0x30, /* 6 */      0x01,0x71,0x09,0x05,0x03, /* 7 */
    0x36,0x49,0x49,0x49,0x36, /* 8 */      0x06,0x49,0x49,0x29,0x1E, /* 9 */
    0x00,0x36,0x36,0x00,0x00, /* : */      0x00,0x56,0x36,0x00,0x00, /* ; */
    0x00,0x08,0x14,0x22,0x41, /* < */      0x14,0x14,0x14,0x14,0x14, /* = */
    0x41,0x22,0x14,0x08,0x00, /* > */      0x02,0x01,0x51,0x09,0x06, /* ? */
    0x32,0x49,0x79,0x41,0x3E, /* @ */      0x7E,0x11,0x11,0x11,0x7E, /* A */
    0x7F,0x49,0x49,0x49,0x36, /* B */      0x3E,0x41,0x41,0x41,0x22, /* C */
    0x7F,0x41,0x41,0x22,0x1C, /* D */      0x7F,0x49,0x49,0x49,0x41, /* E */
    0x7F,0x09,0x09,0x01,0x01, /* F */      0x3E,0x41,0x41,0x51,0x32, /* G */
    0x7F,0x08,0x08,0x08,0x7F, /* H */      0x00,0x41,0x7F,0x41,0x00, /* I */
    0x20,0x40,0x41,0x3F,0x01, /* J */      0x7F,0x08,0x14,0x22,0x41, /* K */
    0x7F,0x40,0x40,0x40,0x40, /* L */      0x7F,0x02,0x04,0x02,0x7F, /* M */
    0x7F,0x04,0x08,0x10,0x7F, /* N */      0x3E,0x41,0x41,0x41,0x3E, /* O */
    0x7F,0x09,0x09,0x09,0x06, /* P */      0x3E,0x41,0x51,0x21,0x5E, /* Q */
    0x7F,0x09,0x19,0x29,0x46, /* R */      0x46,0x49,0x49,0x49,0x31, /* S */
    0x01,0x01,0x7F,0x01,0x01, /* T */      0x3F,0x40,0x40,0x40,0x3F, /* U */
    0x1F,0x20,0x40,0x20,0x1F, /* V */      0x7F,0x20,0x18,0x20,0x7F, /* W */
    0x63,0x14,0x08,0x14,0x63, /* X */      0x03,0x04,0x78,0x04,0x03, /* Y */
    0x61,0x51,0x49,0x45,0x43, /* Z */      0x00,0x00,0x7F,0x41,0x41, /* [ */
    0x02,0x04,0x08,0x10,0x20, /* \ */      0x41,0x41,0x7F,0x00,0x00, /* ] */
    0x04,0x02,0x01,0x02,0x04, /* ^ */      0x40,0x40,0x40,0x40,0x40, /* _ */
    0x00,0x01,0x02,0x04,0x00, /* ` */      0x20,0x54,0x54,0x54,0x78, /* a */
    0x7F,0x48,0x44,0x44,0x38, /* b */      0x38,0x44,0x44,0x44,0x20, /* c */
    0x38,0x44,0x44,0x48,0x7F, /* d */      0x38,0x54,0x54,0x54,0x18, /* e */
    0x08,0x7E,0x09,0x01,0x02, /* f */      0x08,0x14,0x54,0x54,0x3C, /* g */
    0x7F,0x08,0x04,0x04,0x78, /* h */      0x00,0x44,0x7D,0x40,0x00, /* i */
    0x20,0x40,0x44,0x3D,0x00, /* j */      0x00,0x7F,0x10,0x28,0x44, /* k */
    0x00,0x41,0x7F,0x40,0x00, /* l */      0x7C,0x04,0x18,0x04,0x78, /* m */
    0x7C,0x08,0x04,0x04,0x78, /* n */      0x38,0x44,0x44,0x44,0x38, /* o */
    0x7C,0x14,0x14,0x14,0x08, /* p */      0x08,0x14,0x14,0x18,0x7C, /* q */
    0x7C,0x08,0x04,0x04,0x08, /* r */      0x48,0x54,0x54,0x54,0x20, /* s */
    0x04,0x3F,0x44,0x40,0x20, /* t */      0x3C,0x40,0x40,0x20,0x7C, /* u */
    0x1C,0x20,0x40,0x20,0x1C, /* v */      0x3C,0x40,0x30,0x40,0x3C, /* w */
    0x44,0x28,0x10,0x28,0x44, /* x */      0x0C,0x50,0x50,0x50,0x3C, /* y */
    0x44,0x64,0x54,0x4C,0x44, /* z */      0x00,0x08,0x36,0x41,0x00, /* { */
    0x00,0x00,0x7F,0x00,0x00, /* | */      0x00,0x41,0x36,0x08,0x00, /* } */
    0x08,0x08,0x2A,0x1C,0x08, /* ~ */
};

static void dc_cmd(void) { gpio_set_level(TFT_PIN_DC, 0); }
static void dc_data(void) { gpio_set_level(TFT_PIN_DC, 1); }

static void spi_write(const uint8_t *data, int len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void write_cmd(uint8_t cmd)
{
    dc_cmd();
    spi_write(&cmd, 1);
}

static void write_data(const uint8_t *data, int len)
{
    dc_data();
    spi_write(data, len);
}

static void write_data8(uint8_t val)
{
    dc_data();
    spi_write(&val, 1);
}

static void write_data16(uint16_t val)
{
    uint8_t buf[2] = { val >> 8, val & 0xFF };
    dc_data();
    spi_write(buf, 2);
}

static void set_window(int x, int y, int w, int h)
{
    write_cmd(ILI9341_CASET);
    write_data16(x);
    write_data16(x + w - 1);
    write_cmd(ILI9341_PASET);
    write_data16(y);
    write_data16(y + h - 1);
    write_cmd(ILI9341_RAMWR);
}

esp_err_t tft_driver_init(void)
{
    /* Configure GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TFT_PIN_DC) | (1ULL << TFT_PIN_RST) | (1ULL << TFT_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(TFT_PIN_BL, 1);  /* Backlight on */

    /* Reset */
    gpio_set_level(TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Init SPI bus */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = TFT_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = TFT_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2,
    };
    esp_err_t ret = spi_bus_initialize(TFT_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = TFT_SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = TFT_PIN_CS,
        .queue_size = 7,
    };
    ret = spi_bus_add_device(TFT_SPI_HOST, &dev_cfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ILI9341 init sequence */
    write_cmd(ILI9341_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    write_cmd(ILI9341_PWCTR1);
    uint8_t pwr1[] = {0x23};
    write_data(pwr1, 1);

    write_cmd(ILI9341_PWCTR2);
    uint8_t pwr2[] = {0x10};
    write_data(pwr2, 1);

    write_cmd(ILI9341_VMCTR1);
    uint8_t vm1[] = {0x3E, 0x28};
    write_data(vm1, 2);

    write_cmd(ILI9341_VMCTR2);
    uint8_t vm2[] = {0x86};
    write_data(vm2, 1);

    write_cmd(ILI9341_MADCTL);
    uint8_t madctl[] = {MADCTL_MX | MADCTL_BGR};
    write_data(madctl, 1);

    write_cmd(ILI9341_PIXFMT);
    uint8_t pixfmt[] = {0x55};  /* 16-bit */
    write_data(pixfmt, 1);

    write_cmd(ILI9341_FRMCTR1);
    uint8_t frm[] = {0x00, 0x18};
    write_data(frm, 2);

    write_cmd(ILI9341_DFUNCTR);
    uint8_t df[] = {0x08, 0x82, 0x27};
    write_data(df, 3);

    /* Gamma correction */
    write_cmd(ILI9341_GMCTRP1);
    uint8_t gm_p[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                      0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
    write_data(gm_p, 15);

    write_cmd(ILI9341_GMCTRN1);
    uint8_t gm_n[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                      0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
    write_data(gm_n, 15);

    write_cmd(ILI9341_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    write_cmd(ILI9341_DISPON);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "ILI9341 initialized");
    return ESP_OK;
}

void tft_fill_screen(uint16_t color)
{
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, color);
}

void tft_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return;
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;

    set_window(x, y, w, h);
    dc_data();

    /* Fill line buffer and send row by row */
    uint16_t line_buf[320];
    uint16_t c = (color >> 8) | ((color & 0xFF) << 8); /* swap bytes */
    for (int i = 0; i < w && i < 320; i++) line_buf[i] = c;

    for (int row = 0; row < h; row++) {
        spi_transaction_t t = {
            .length = w * 16,
            .tx_buffer = line_buf,
        };
        spi_device_polling_transmit(s_spi, &t);
    }
}

void tft_draw_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    set_window(x, y, 1, 1);
    write_data16(color);
}

void tft_draw_hline(int x, int y, int w, uint16_t color)
{
    tft_fill_rect(x, y, w, 1, color);
}

void tft_draw_vline(int x, int y, int h, uint16_t color)
{
    tft_fill_rect(x, y, 1, h, color);
}

void tft_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    tft_draw_hline(x, y, w, color);
    tft_draw_hline(x, y + h - 1, w, color);
    tft_draw_vline(x, y, h, color);
    tft_draw_vline(x + w - 1, y, h, color);
}

void tft_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, uint8_t size)
{
    if (c < 32 || c > 127) c = '?';
    const uint8_t *glyph = &font5x7[(c - 32) * 5];

    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            uint16_t color = (line & (1 << row)) ? fg : bg;
            tft_fill_rect(x + col * size, y + row * size, size, size, color);
        }
    }
}

void tft_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg, uint8_t size)
{
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += 8 * size;
        } else {
            tft_draw_char(cx, y, *str, fg, bg, size);
            cx += 6 * size;
        }
        str++;
    }
}

void tft_blit(int x, int y, int w, int h, const uint16_t *data)
{
    set_window(x, y, w, h);
    dc_data();
    spi_transaction_t t = {
        .length = w * h * 16,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}
