#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h" // Deve ser o primeiro do FreeRTOS
#include "task.h"     // Opcional se não usar tasks aqui
#include "semphr.h"   // Agora ele vai reconhecer os tipos
#include "screen_display.h"

static ssd1306_t disp;

void oled_screen_init_device(void)
{
    i2c_init(i2c1, 400000);

    gpio_set_function(14, GPIO_FUNC_I2C);
    gpio_set_function(15, GPIO_FUNC_I2C);

    gpio_pull_up(14);
    gpio_pull_up(15);

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
}

void oled_screen_start_calibration(void)
{
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, "Iniciando calibracao!");
    ssd1306_draw_string(&disp, 30, 25, 1, "Pressione o");
    ssd1306_draw_string(&disp, 20, 37, 1, "botao Iniciar!");
    ssd1306_show(&disp);
}

void oled_screen_finished_calibration(void)
{
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 34, 25, 1, "Finished");
    ssd1306_draw_string(&disp, 25, 37, 1, "calibration");
    ssd1306_show(&disp);
}
