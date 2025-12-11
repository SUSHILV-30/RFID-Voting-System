/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : MFRC522 (HAL, SPI) + SSD1306 (register-level I2C) +
  *                   Register-level ADC (PA1) for potentiometer voting UI
  *                   + register-level GPIO init
  *
  * This file is ready-to-build (assumes rc522.c/.h and CubeMX HAL files exist
  * in the project). It uses register-level code for I2C (SSD1306), ADC (PA1),
  * and GPIO initialization while keeping SPI & MFRC522 HAL-based.
  *
  * Important: this file DOES NOT define SysTick_Handler; it uses HAL's tick
  * (HAL_GetTick / HAL_Delay) to avoid duplicate interrupt definitions with
  * CubeMX-generated stm32f4xx_it.c.
  ****************************************************************************
  */
/* USER CODE END Header */

/* standard C headers */
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* project headers */
#include "main.h"     /* CubeMX-generated project header (pins, prototypes) */
#include "rc522.h"    /* MFRC522 driver (uses HAL SPI in your project) */

/* CMSIS / device / HAL headers */
#include "stm32f4xx.h"    /* CMSIS device registers (GPIOA, ADC1, I2C1, etc.) */
#include "stm32f4xx_hal.h"/* HAL core (RCC_OscInitTypeDef, HAL_Init, HAL_SPI_Init, etc.) */

/* -------------------------------------------------------------------------- */
#ifndef MIN_LED_ON_MS
#define MIN_LED_ON_MS 200U
#endif

#define SSD1306_ADDR_7BIT  0x3CU
#define SSD1306_WRITE_ADDR (SSD1306_ADDR_7BIT << 1)
#define I2C_TIMEOUT  100000U

/* -------------------------------------------------------------------------- */
SPI_HandleTypeDef hspi1; /* used by rc522 HAL driver */

/* Globals */
uint8_t status;
uint8_t str[MAX_LEN];
uint8_t sNum[5];
static uint32_t led_on_until = 0U;

/* Display states */
enum { DS_WELCOME = 0, DS_CASTE_VOTE = 2, DS_VOTE_CASTED = 3, DS_VERIFIED = 4, DS_INVALID = 5 };
static uint8_t display_state = DS_WELCOME;
static uint32_t display_until = 0U;

/* Authorized UIDs */
static const uint8_t auth_uids[][5] = {
    { 0x73, 0x91, 0xB1, 0x28, 0x7B },
    { 0x96, 0x7C, 0x41, 0x1E, 0xB5 }
};
static const size_t auth_count = sizeof(auth_uids) / sizeof(auth_uids[0]);

/* Vote counters */
static uint32_t votesA = 0, votesB = 0, votesC = 0;

/* Selection & arrow animation */
static uint8_t sel_idx = 0; /* 0=A,1=B,2=C */
static uint32_t anim_toggle_until = 0U;
static uint8_t anim_state = 1; /* 1 = arrow visible, 0 = arrow hidden */

/* Button hold detection */
#define LONG_PRESS_MS 1000U

/* ADC thresholds (12-bit ADC 0..4095) */
static uint16_t adc_thresh_low = 1365;
static uint16_t adc_thresh_high = 2730;

/* Track where a button press originated (screen at the moment of press) */
static uint8_t btn_press_origin = DS_WELCOME;

/* Prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init_register(void);
static void MX_SPI1_Init(void);
void Error_Handler(void);

/* SSD1306 / I2C (register-level) */
static void delay_cpu(volatile uint32_t d);
static void i2c1_init(void);
static int i2c1_start_write(uint8_t addr);
static int i2c1_write_bytes(uint8_t *buf, uint32_t len);
static void i2c1_stop(void);
static int i2c1_write_transaction(uint8_t addr8, uint8_t *data, uint32_t len);
static int ssd1306_command(uint8_t cmd);
static int ssd1306_data(uint8_t *data, uint32_t len);
static void ssd1306_init(void);
static void ssd1306_clear(void);
static void ssd1306_set_pos(uint8_t page, uint8_t col);
static void ssd1306_draw_char(uint8_t page, uint8_t col, char ch);
static void ssd1306_print(uint8_t page, uint8_t col, const char *s);

/* ADC register-level prototypes */
static void MX_ADC1_Init_register(void);
static uint32_t read_pot_adc_register(void);

/* UI helpers */
static void show_welcome(void);
static void show_caste_vote_screen(uint8_t sel, uint8_t anim_visible);
static void show_vote_casted(uint8_t sel);
static void show_verified_with_uid(const uint8_t uid[5]);
static void show_invalid_with_uid(const uint8_t uid[5]);
static void show_vote_counts(uint32_t a, uint32_t b, uint32_t c);

/* busy-wait */
static void delay_cpu(volatile uint32_t d) { while (d--) { __NOP(); } }

/* ----------------- I2C1 register-level routines (PB6=SCL PB7=SDA) ----------------- */
static void i2c1_init(void)
{
    RCC->AHB1ENR |= (1U << 1);   /* GPIOB */
    RCC->APB1ENR |= (1U << 21);  /* I2C1 */

    /* Configure PB6/PB7 -> AF4 Open-Drain Pull-up */
    GPIOB->MODER &= ~((3U << (6*2)) | (3U << (7*2)));
    GPIOB->MODER |=  ((2U << (6*2)) | (2U << (7*2))); /* AF */
    GPIOB->OTYPER |= (1U << 6) | (1U << 7); /* open-drain */
    GPIOB->OSPEEDR &= ~((3U << (6*2)) | (3U << (7*2)));
    GPIOB->OSPEEDR |=  ((2U << (6*2)) | (2U << (7*2))); /* med speed */
    GPIOB->PUPDR &= ~((3U << (6*2)) | (3U << (7*2)));
    GPIOB->PUPDR |=  ((1U << (6*2)) | (1U << (7*2))); /* pull-up */
    GPIOB->AFR[0] &= ~((0xFU << (6*4)) | (0xFU << (7*4)));
    GPIOB->AFR[0] |=  ((4U << (6*4)) | (4U << (7*4))); /* AF4 */

    /* Reset & configure I2C1 for ~100kHz (assumes PCLK1 ~42 MHz) */
    I2C1->CR1 = (1U << 15); /* SWRST */
    delay_cpu(1000);
    I2C1->CR1 = 0;
    I2C1->CR2 = 42U & 0x3F; /* FREQ */
    I2C1->CCR = 210U & 0x0FFF; /* CCR */
    I2C1->TRISE = 43U & 0x3F;
    I2C1->CR1 |= (1U << 10); /* ACK */
    I2C1->CR1 |= (1U << 0);  /* PE */
    delay_cpu(1000);
}

static int i2c1_start_write(uint8_t addr)
{
    uint32_t to = I2C_TIMEOUT;
    I2C1->CR1 |= (1U << 8); /* START */
    while (!(I2C1->SR1 & (1U << 0))) { if (!--to) return -1; } /* SB */
    I2C1->DR = addr;
    to = I2C_TIMEOUT;
    while (!(I2C1->SR1 & (1U << 1))) { if (!--to) return -2; } /* ADDR */
    (void)I2C1->SR1; (void)I2C1->SR2;
    return 0;
}

static int i2c1_write_bytes(uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        uint32_t to = I2C_TIMEOUT;
        while (!(I2C1->SR1 & (1U << 7))) { if (!--to) return -3; } /* TXE */
        I2C1->DR = buf[i];
    }
    uint32_t to = I2C_TIMEOUT;
    while (!(I2C1->SR1 & (1U << 2))) { if (!--to) break; }
    return 0;
}

static void i2c1_stop(void)
{
    I2C1->CR1 |= (1U << 9); /* STOP */
    delay_cpu(1000);
}

static int i2c1_write_transaction(uint8_t addr8, uint8_t *data, uint32_t len)
{
    int r = i2c1_start_write(addr8);
    if (r) { i2c1_stop(); return r; }
    r = i2c1_write_bytes(data, len);
    i2c1_stop();
    return r;
}

/* SSD1306 helpers */
static int ssd1306_command(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return i2c1_write_transaction(SSD1306_WRITE_ADDR, buf, 2);
}

static int ssd1306_data(uint8_t *data, uint32_t len)
{
    const uint32_t chunk = 32;
    uint8_t tmp[33];
    uint32_t sent = 0;
    while (sent < len)
    {
        uint32_t c = (len - sent > chunk) ? chunk : (len - sent);
        tmp[0] = 0x40;
        memcpy(&tmp[1], &data[sent], c);
        int r = i2c1_write_transaction(SSD1306_WRITE_ADDR, tmp, c + 1);
        if (r) return r;
        sent += c;
    }
    return 0;
}

static void ssd1306_init(void)
{
    delay_cpu(10000);
    ssd1306_command(0xAE);
    ssd1306_command(0xD5); ssd1306_command(0x80);
    ssd1306_command(0xA8); ssd1306_command(0x3F);
    ssd1306_command(0xD3); ssd1306_command(0x00);
    ssd1306_command(0x40);
    ssd1306_command(0x8D); ssd1306_command(0x14);
    ssd1306_command(0x20); ssd1306_command(0x00);
    ssd1306_command(0xA1);
    ssd1306_command(0xC8);
    ssd1306_command(0xDA); ssd1306_command(0x12);
    ssd1306_command(0x81); ssd1306_command(0xCF);
    ssd1306_command(0xD9); ssd1306_command(0xF1);
    ssd1306_command(0xDB); ssd1306_command(0x40);
    ssd1306_command(0xA4);
    ssd1306_command(0xA6);
    ssd1306_command(0xAF);
}

static void ssd1306_clear(void)
{
    uint8_t zeros[128];
    memset(zeros, 0x00, sizeof(zeros));
    for (uint8_t page = 0; page < 8; ++page)
    {
        ssd1306_command(0xB0 + page);
        ssd1306_command(0x00);
        ssd1306_command(0x10);
        ssd1306_data(zeros, 128);
    }
}

static void ssd1306_set_pos(uint8_t page, uint8_t col)
{
    ssd1306_command(0xB0 | (page & 0x07));
    ssd1306_command(0x00 | (col & 0x0F));
    ssd1306_command(0x10 | ((col >> 4) & 0x0F));
}

/* 5x7 font */
/* 5x7 ASCII font (96 chars, ASCII 32..127) */
static const uint8_t font5x7[96][5] = {
{0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},{0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
{0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44}
};

/* draw/print */
static void ssd1306_draw_char(uint8_t page, uint8_t col, char ch)
{
    if (ch < 32 || ch > 127) ch = '?';
    const uint8_t *glyph = font5x7[ch - 32];
    ssd1306_set_pos(page, col);
    uint8_t buf[6];
    for (int i = 0; i < 5; ++i) buf[i] = glyph[i];
    buf[5] = 0x00;
    ssd1306_data(buf, 6);
}

static void ssd1306_print(uint8_t page, uint8_t col, const char *s)
{
    uint8_t c = col; uint8_t p = page;
    while (*s) {
        if (c > 122) { c = 0; if (++p > 7) p = 0; }
        ssd1306_draw_char(p, c, *s++);
        c += 6;
    }
}

/* ADC register-level init & read for PA1 (ADC1_IN1) */
static void MX_ADC1_Init_register(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC1->CR2 &= ~ADC_CR2_ADON;
    ADC1->SMPR2 &= ~(0x7U << (3 * 1));
    ADC1->SMPR2 |=  (0x4U << (3 * 1));
    ADC1->CR2 &= ~ADC_CR2_CONT;
    ADC1->SQR1 = 0;
    ADC1->CR2 |= ADC_CR2_ADON;
    for (volatile int i = 0; i < 1000; ++i) __NOP();
}

static uint32_t read_pot_adc_register(void)
{
    ADC1->SQR3 = 1U; /* channel 1 */
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t to = 100000;
    while (!(ADC1->SR & ADC_SR_EOC)) { if (!--to) return 0; }
    return ADC1->DR & 0xFFFU;
}

/* UI helpers */
static void show_welcome(void)
{
    ssd1306_clear();
    ssd1306_print(2, 14, "WELCOME");
    ssd1306_print(3, 14, "RFID VOTING SYSTEM");
    display_state = DS_WELCOME; display_until = 0;
}

static void show_caste_vote_screen(uint8_t sel, uint8_t anim_visible)
{
    ssd1306_clear();
    ssd1306_print(0, 8, "CASTE VOTE");
    if (anim_visible && sel == 0) ssd1306_print(2, 2, ">"); else ssd1306_print(2, 2, " ");
    ssd1306_print(2, 8, "CAND A");
    if (anim_visible && sel == 1) ssd1306_print(3, 2, ">"); else ssd1306_print(3, 2, " ");
    ssd1306_print(3, 8, "CAND B");
    if (anim_visible && sel == 2) ssd1306_print(4, 2, ">"); else ssd1306_print(4, 2, " ");
    ssd1306_print(4, 8, "CAND C");
    ssd1306_print(6, 0, "Turn pot to select");
    display_state = DS_CASTE_VOTE; display_until = 0;
}

static void show_vote_casted(uint8_t sel)
{
    ssd1306_clear();
    ssd1306_print(1, 12, "VOTE");
    ssd1306_print(2, 10, "CASTED");
    if (sel == 0) ssd1306_print(4, 8, "CAND A");
    else if (sel == 1) ssd1306_print(4, 8, "CAND B");
    else ssd1306_print(4, 8, "CAND C");
    display_state = DS_VOTE_CASTED;
    display_until = HAL_GetTick() + 3000U;
}

static void show_verified_with_uid(const uint8_t uid[5])
{
    char uidstr[64];
    snprintf(uidstr, sizeof(uidstr), "UID:%02X %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3], uid[4]);
    ssd1306_clear();
    ssd1306_print(1, 8, "VOTER ID VERIFIED");
    ssd1306_print(4, 10, uidstr);
    display_state = DS_VERIFIED; display_until = HAL_GetTick() + 3000U;
}

static void show_invalid_with_uid(const uint8_t uid[5])
{
    char uidstr[64];
    snprintf(uidstr, sizeof(uidstr), "UID:%02X %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3], uid[4]);
    ssd1306_clear();
    ssd1306_print(1, 8, "VOTER ID INVALID");
    ssd1306_print(4, 10, uidstr);
    display_state = DS_INVALID; display_until = HAL_GetTick() + 3000U;
}

static void show_vote_counts(uint32_t a, uint32_t b, uint32_t c)
{
    char buf[32];
    ssd1306_clear();
    ssd1306_print(0, 6, "VOTE COUNTS");
    snprintf(buf, sizeof(buf), "A: %lu", (unsigned long)a);
    ssd1306_print(2, 6, buf);
    snprintf(buf, sizeof(buf), "B: %lu", (unsigned long)b);
    ssd1306_print(3, 6, buf);
    snprintf(buf, sizeof(buf), "C: %lu", (unsigned long)c);
    ssd1306_print(4, 6, buf);
}

static uint8_t adc_to_selection(uint32_t v)
{
    if (v < adc_thresh_low) return 0;
    if (v < adc_thresh_high) return 1;
    return 2;
}

/* -------------------------------------------------------------------------- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* Keep using HAL systick implemented in stm32f4xx_it.c */

    MX_GPIO_Init_register();
    MX_SPI1_Init();

    MFRC522_Init(); /* uses HAL SPI */

    MX_ADC1_Init_register();

    i2c1_init();
    ssd1306_init();
    ssd1306_clear();

    show_welcome();

    anim_toggle_until = HAL_GetTick() + 500U;
    anim_state = 1;

    uint32_t btn_press_start = 0;
    uint8_t btn_prev = 1;

    for (;;)
    {
        status = MFRC522_Request(PICC_REQIDL, str);
        if (status == MI_OK) {
            if (MFRC522_Anticoll(str) == MI_OK) {
                memcpy(sNum, str, 5);
                uint32_t now = HAL_GetTick();
                uint32_t cand = now + MIN_LED_ON_MS;
                if (cand > led_on_until) led_on_until = cand;

                if (display_state == DS_WELCOME) {
                    uint8_t match = 0;
                    for (size_t i = 0; i < auth_count; ++i) {
                        if (memcmp(sNum, auth_uids[i], 5) == 0) { match = 1; break; }
                    }
                    if (match) show_verified_with_uid(sNum); else show_invalid_with_uid(sNum);
                }

                HAL_Delay(50);
            }
        }

        uint8_t btn_now = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 0 : 1; /* active low */
        uint32_t tick = HAL_GetTick();

        if (btn_prev == 1 && btn_now == 0) { btn_press_start = tick; btn_press_origin = display_state; }
        else if (btn_prev == 0 && btn_now == 1) {
            uint32_t held = tick - btn_press_start;
            if (held >= LONG_PRESS_MS) {
                if (btn_press_origin == DS_WELCOME) show_welcome();
            } else {
                if (display_state == DS_CASTE_VOTE) {
                    if (sel_idx == 0) votesA++; else if (sel_idx == 1) votesB++; else votesC++;
                    show_vote_casted(sel_idx);
                } else show_welcome();
            }
        }

        if (btn_now == 0) {
            uint32_t held = tick - btn_press_start;
            if (held >= LONG_PRESS_MS) {
                if (btn_press_origin == DS_WELCOME) {
                    show_vote_counts(votesA, votesB, votesC);
                    btn_prev = btn_now;
                    HAL_Delay(20);
                    continue;
                }
            }
        }

        btn_prev = btn_now;

        if ((display_state == DS_VERIFIED || display_state == DS_INVALID || display_state == DS_VOTE_CASTED) && tick >= display_until) {
            if (display_state == DS_VERIFIED) {
                uint32_t adc = read_pot_adc_register(); sel_idx = adc_to_selection(adc);
                anim_toggle_until = tick + 500U; anim_state = 1; show_caste_vote_screen(sel_idx, anim_state);
            } else {
                show_welcome();
            }
        }

        if (display_state == DS_CASTE_VOTE) {
            uint32_t adc = read_pot_adc_register(); uint8_t new_sel = adc_to_selection(adc);
            if (new_sel != sel_idx) { sel_idx = new_sel; anim_state = 1; anim_toggle_until = tick + 500U; }
            if (tick >= anim_toggle_until) { anim_state ^= 1; anim_toggle_until = tick + 500U; }
            show_caste_vote_screen(sel_idx, anim_state);
        }

        if (HAL_GetTick() < led_on_until) GPIOC->BSRR = (1U << (13 + 16)); else GPIOC->BSRR = (1U << 13);

        HAL_Delay(20);
    }
}

/* -------------------------------------------------------------------------- */
/* GPIO init: register-level for PC13 LED, PA4 CS, PB0 RST, PA0 button, PA1 analog */
static void MX_GPIO_Init_register(void)
{
    /* Enable GPIO clocks */
    RCC->AHB1ENR |= (1U << 2); /* GPIOC */
    RCC->AHB1ENR |= (1U << 0); /* GPIOA */
    RCC->AHB1ENR |= (1U << 1); /* GPIOB */

    /* PC13 output (active-low LED) */
    GPIOC->MODER &= ~(3U << (13 * 2));
    GPIOC->MODER |=  (1U << (13 * 2));
    GPIOC->OTYPER &= ~(1U << 13);
    GPIOC->OSPEEDR &= ~(3U << (13 * 2));
    GPIOC->PUPDR &= ~(3U << (13 * 2));
    GPIOC->BSRR = (1U << 13); /* LED OFF (PC13 high) */

    /* PA4 CS output (idle HIGH) */
    GPIOA->MODER &= ~(3U << (4 * 2));
    GPIOA->MODER |=  (1U << (4 * 2));
    GPIOA->OTYPER &= ~(1U << 4);
    GPIOA->OSPEEDR &= ~(3U << (4 * 2));
    GPIOA->PUPDR &= ~(3U << (4 * 2));
    GPIOA->BSRR = (1U << 4); /* CS HIGH (idle) */

    /* PB0 RST output (HIGH inactive) */
    GPIOB->MODER &= ~(3U << (0 * 2));
    GPIOB->MODER |=  (1U << (0 * 2));
    GPIOB->OTYPER &= ~(1U << 0);
    GPIOB->OSPEEDR &= ~(3U << (0 * 2));
    GPIOB->PUPDR &= ~(3U << (0 * 2));
    GPIOB->BSRR = (1U << 0); /* RST HIGH */

    /* PA0 button input with pull-up */
    GPIOA->MODER &= ~(3U << (0 * 2));
    GPIOA->PUPDR &= ~(3U << (0 * 2));
    GPIOA->PUPDR |=  (1U << (0 * 2)); /* pull-up */

    /* PA1 analog (pot) */
    GPIOA->MODER &= ~(3U << (1 * 2));
    GPIOA->MODER |=  (3U << (1 * 2)); /* analog mode = 11 */
    GPIOA->PUPDR &= ~(3U << (1 * 2)); /* no pull */
}

/* Keep SPI init via HAL for MFRC522 compatibility */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) { Error_Handler(); }
}

/* SystemClock_Config (same as CubeMX typical config) */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 72;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {
    GPIOC->BSRR = (1U << (13 + 16));
    HAL_Delay(150);
    GPIOC->BSRR = (1U << 13);
    HAL_Delay(150);
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
  (void)file; (void)line;
}
#endif

/* End of file */
