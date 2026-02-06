#ifndef HX711_H   // Guarda de inclusão: evita que o arquivo seja lido duas vezes
#define HX711_H

#include "pico/stdlib.h"

typedef struct {
    uint pin_dt;
    uint pin_sck;
    long offset;
    float scale;
} hx711_config_t;

typedef struct calibration_data calibration_data_t;

#define HX711_DATA_PIN  18 
#define HX711_SCLK_PIN  19 

// --- Protótipos das Funções ---

// Inicializa os pinos GPIO
void hx711_init(uint pin_dt, uint pin_sck);

// Verifica se o HX711 está pronto para conversão
bool hx711_is_ready(uint pin_dt);

// Lê o valor bruto (24 bits) do sensor
long hx711_read(uint pin_dt, uint pin_sck);

// Realiza a tara (zera a balança)
long hx711_get_tare(uint pin_dt, uint pin_sck, int samples);

// Realiza a leitura do peso 
float hx711_get_weight(uint pin_dt, uint pin_sck, long offset, float scale);

void execute_calibration(calibration_data_t *calib);

#endif // HX711_H