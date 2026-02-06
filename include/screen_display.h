#ifndef SCREEN_DISPLAY_H
#define SCREEN_DISPLAY_H

#include "ssd1306.h"
#include "hardware/i2c.h"

// Definições de hardware (ajuste se necessário)
#define I2C_PORT i2c1
#define SDA_PIN 14
#define SCL_PIN 15
#define OLED_ADDR 0x3C

void oled_screen_init_device(void);
void oled_screen_start_calibration(void);

#endif