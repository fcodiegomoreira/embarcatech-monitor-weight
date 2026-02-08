#include "hx711.h"
#include <stdio.h>
#include "calibration_flash.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

// Inicializa os pinos configurando direções e estados iniciais
void hx711_init(uint pin_dt, uint pin_sck) 
{
    gpio_init(pin_dt);
    gpio_set_dir(pin_dt, GPIO_IN);
    // pull-up opcional se o módulo não tiver resistores próprios
    gpio_pull_up(pin_dt); 

    gpio_init(pin_sck);
    gpio_set_dir(pin_sck, GPIO_OUT);
    gpio_put(pin_sck, 0);
}

// Verifica se o sensor está pronto (Pino DT em nível BAIXO)
bool hx711_is_ready(uint pin_dt) 
{
    return gpio_get(pin_dt) == 0;
}

// Lê o valor bruto de 24 bits com proteção de timeout
long hx711_read(uint pin_dt, uint pin_sck) 
{
    // Timeout de 500ms para evitar travamentos
    uint32_t timeout_us = 500000;
    absolute_time_t timeout_time = make_timeout_time_us(timeout_us);

    while (!hx711_is_ready(pin_dt)) {
        if (time_reached(timeout_time)) {
            return 0; // Retorna 0 se falhar (ou você pode retornar um código de erro)
        }
        tight_loop_contents();
    }

    long value = 0;
    
    // Desabilita interrupções durante a leitura para manter o timing crítico
    uint32_t ints = save_and_disable_interrupts();

    for (int i = 0; i < 24; i++) {
        gpio_put(pin_sck, 1);
        sleep_us(1); // Mínimo exigido pelo HX711
        
        value = (value << 1) | gpio_get(pin_dt);
        
        gpio_put(pin_sck, 0);
        sleep_us(1);
    }

    // 25º pulso para configurar Ganho de 128 (Canal A)
    gpio_put(pin_sck, 1);
    sleep_us(1);
    gpio_put(pin_sck, 0);
    sleep_us(1);

    // Reabilita interrupções
    restore_interrupts(ints);

    // Extensão de sinal para 32 bits (Complemento de 2)
    if (value & 0x800000) {
        value |= 0xFF000000;
    }

    return value;
}

// Calcula a média para Tara (Offset)
long hx711_get_tare(uint pin_dt, uint pin_sck, int samples) 
{
    long sum = 0;
    int valid_samples = 0;

    for (int i = 0; i < samples; i++) 
    {
        long val = hx711_read(pin_dt, pin_sck);
        if (val != 0) 
        { // Considera apenas leituras que não deram timeout
            sum += val;
            valid_samples++;
        }
        sleep_ms(10); // Pequeno intervalo entre amostras
    }

    if (valid_samples > 0) {
        return sum / valid_samples;
    } else {
        return 0;
    }
}

float hx711_get_weight(uint pin_dt, uint pin_sck, long offset, float scale) 
{
    long raw = hx711_read(pin_dt, pin_sck);
    if (raw == 0) return 0.0f; // Caso ocorra timeout
    
    return (float)(raw - offset) / scale;
}

void execute_calibration(calibration_data_t *calib)
{
    hx711_config_t config;

    config.pin_dt  = HX711_DATA_PIN;
    config.pin_sck = HX711_SCLK_PIN;
    config.offset  = 0;
    config.scale   = 1.0f;

    hx711_init(config.pin_dt, config.pin_sck);

    printf("Estabilizando sensor... Mantenha sem carga.\n");
    busy_wait_ms(7000);   // ⬅️ agora NÃO é mais vTaskDelay

    config.offset = hx711_get_tare(config.pin_dt, config.pin_sck, 10);
    printf("Tara concluída! Offset: %ld\n", config.offset);
    printf("--------------------------------------------------\n");

    printf("Prepare o peso de 85g...\n");
    for (int i = 3; i > 0; i--)
    {
        printf("Coloque o peso! Iniciando em %d...\n", i);
        busy_wait_ms(1500);
    }

    printf("Calculando fator de escala... mantenha o peso parado.\n");

    long leitura_com_peso = 0;
    int amostras = 15;

    for (int i = 0; i < amostras; i++)
    {
        leitura_com_peso += hx711_read(config.pin_dt, config.pin_sck);
        busy_wait_ms(100);
    }
    leitura_com_peso /= amostras;

    config.scale = (float)(leitura_com_peso - config.offset) / 85.0f;

    printf("Calibracao terminada!\n");
    printf("Offset: %ld\n", config.offset);
    printf("Scale : %.4f\n", config.scale);
    printf("--------------------------------------------------\n");


    /* 🔽 Preenche a struct recebida por ponteiro */
    calib->calibrated_flag = CALIBRATION_VALID_FLAG;
    calib->tare            = config.offset;   // SEM cast
    calib->scale_factor    = config.scale;
}
