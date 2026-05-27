#pragma once

/* ========== TFT ILI9341 (SPI) ========== */
#define TFT_SPI_HOST        SPI2_HOST
#define TFT_PIN_MOSI        11
#define TFT_PIN_SCK         12
#define TFT_PIN_CS          10
#define TFT_PIN_DC          9
#define TFT_PIN_RST         8
#define TFT_PIN_BL          7
#define TFT_WIDTH           320
#define TFT_HEIGHT          240
#define TFT_SPI_FREQ_HZ     (40 * 1000 * 1000)  /* 40MHz */

/* ========== Touch XPT2046 (SPI shared) ========== */
#define TOUCH_SPI_HOST      SPI2_HOST
#define TOUCH_PIN_CS        46
#define TOUCH_PIN_IRQ       3
#define TOUCH_SPI_FREQ_HZ   (2 * 1000 * 1000)

/* ========== RS-485 (UART) ========== */
#define RS485_UART_NUM      UART_NUM_1
#define RS485_PIN_TX        17
#define RS485_PIN_RX        18
#define RS485_PIN_DE        4       /* DE & RE tied together */
#define RS485_DEFAULT_BAUD  9600
#define RS485_BUF_SIZE      512

/* ========== Buttons ========== */
#define BTN_PIN_UP          1
#define BTN_PIN_DOWN        2
#define BTN_PIN_OK          42
#define BTN_PIN_BACK        41
#define BTN_DEBOUNCE_MS     30
#define BTN_LONG_PRESS_MS   800

/* ========== ADC (Waveform) ========== */
#define WAVE_ADC_CHANNEL    ADC_CHANNEL_3   /* GPIO4 */
#define WAVE_ADC_UNIT       ADC_UNIT_1
#define WAVE_SAMPLE_RATE    100000          /* 100KSPS */
#define WAVE_SAMPLE_DEPTH   1024

/* ========== Status LED ========== */
#define LED_PIN             48

/* ========== WiFi Defaults ========== */
#define WIFI_AP_SSID        "ModbusDiag"
#define WIFI_AP_PASS        "12345678"
#define WEB_SERVER_PORT     80

/* ========== Modbus Defaults ========== */
#define MODBUS_TIMEOUT_MS   100
#define MODBUS_RETRY_COUNT  2
#define MODBUS_MAX_DEVICES  32

/* ========== Task Priorities ========== */
#define TASK_PRIO_MODBUS    6
#define TASK_PRIO_UI        5
#define TASK_PRIO_CAPTURE   4
#define TASK_PRIO_WIFI      3

#define TASK_STACK_UI       4096
#define TASK_STACK_MODBUS   4096
#define TASK_STACK_CAPTURE  3072
#define TASK_STACK_WIFI     4096
