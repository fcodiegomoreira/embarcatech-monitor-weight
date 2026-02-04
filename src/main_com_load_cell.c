#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hx711.h"
#include "hardware/sync.h"
#include "hardware/i2c.h"

#include "ssd1306.h"
#include "image.h"
#include "acme_5_outlines_font.h"
#include "bubblesstandard_font.h"
#include "crackers_font.h"
#include "BMSPA_font.h"

// ================= CONFIGURAÇÕES =================
#define WIFI_SSID       "Gesilane"
#define WIFI_PASSWORD   "bruxxf6d"

#define LED_EXTERNO_PIN 11

// Pinos do Sensor HX711
#define HX711_DATA_PIN  18 
#define HX711_SCLK_PIN  19 
// =================================================

// ====== FreeRTOS (alocação estática obrigatória) ======
void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    uint32_t *pulIdleTaskStackSize) 
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(
    StaticTask_t **ppxTimerTaskTCBBuffer,
    StackType_t **ppxTimerTaskStackBuffer,
    uint32_t *pulTimerTaskStackSize) 
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

// ===================== TASK 1 =====================
void led_interno_task(void *pvParameters) 
{
    if (cyw43_arch_init()) 
    {
        printf("Falha ao inicializar Wi-Fi\n");
        vTaskDelete(NULL);
    }

    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");

    if (cyw43_arch_wifi_connect_timeout_ms(
            WIFI_SSID, WIFI_PASSWORD,
            CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) 
    {
        printf("Falha ao conectar no Wi-Fi\n");
    } 
    else 
    {
        printf("Wi-Fi conectado com sucesso!\n");
    }

    while (true) 
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ===================== TASK 2 =====================
void led_externo_task(void *pvParameters) 
{
    gpio_init(LED_EXTERNO_PIN);
    gpio_set_dir(LED_EXTERNO_PIN, GPIO_OUT);

    while (true) 
    {
        gpio_put(LED_EXTERNO_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_put(LED_EXTERNO_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ===================== TASK 3 =====================
void sensor_pressao_task(void *pvParameters) 
{
    hx711_config_t config;
    config.pin_dt = HX711_DATA_PIN;
    config.pin_sck = HX711_SCLK_PIN;
    config.offset = 0;
    config.scale = 1.0f;

    hx711_init(config.pin_dt, config.pin_sck);

    printf("Estabilizando sensor... Mantenha sem carga.\n");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 1. Tara (O sensor deve estar vazio)
    config.offset = hx711_get_tare(config.pin_dt, config.pin_sck, 10);
    printf("Tara concluída! Offset: %ld\n", config.offset);
    printf("--------------------------------------------------\n");

    // 2. Preparação para o Peso
    printf("Prepare o peso de 85g...\n");
    
    // Contagem regressiva de 3 segundos
    for(int i = 3; i > 0; i--) {
        printf("Coloque o peso! Iniciando em %d...\n", i);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Espera 1 segundo por ciclo
    }

    printf("Calculando fator de escala... mantenha o peso parado.\n");

    // 3. Leitura para o Scale
    long leitura_com_peso = 0;
    int amostras = 15; // Tirando a média de 15 leituras para maior precisão
    
    for(int i = 0; i < amostras; i++) {
        leitura_com_peso += hx711_read(config.pin_dt, config.pin_sck);
        vTaskDelay(pdMS_TO_TICKS(100)); // Pequeno intervalo entre amostras
    }
    leitura_com_peso /= amostras;

    // Fórmula: Scale = (Leitura_Bruta - Offset) / Peso_Conhecido (85g)
    config.scale = (float)(leitura_com_peso - config.offset) / 85.0f;

    printf("Calibracao terminada! Seu SCALE e: %.4f\n", config.scale);
    printf("--------------------------------------------------\n");

    // 4. Loop de medição contínua
    while (true) {
        float peso_final = hx711_get_weight(config.pin_dt, config.pin_sck, config.offset, config.scale);
        printf("Peso: %.2f g\n", peso_final);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ===================== TASK 4 (OLED com contador) =====================
void oled_task(void *pvParameters)
{
    i2c_init(i2c1, 400000);
    gpio_set_function(14, GPIO_FUNC_I2C);
    gpio_set_function(15, GPIO_FUNC_I2C);
    gpio_pull_up(14);
    gpio_pull_up(15);

    ssd1306_t disp;
    disp.external_vcc = false;

    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(&disp);

    int contador = 0;
    char buffer[20];

    while (true)
    {
        ssd1306_clear(&disp);

        // Texto fixo
        ssd1306_draw_string(&disp, 0, 8, 1, "Contador:");

        // Converte número para string
        snprintf(buffer, sizeof(buffer), "%d", contador);

        // Mostra o valor do contador
        ssd1306_draw_string(&disp, 0, 24, 2, buffer);

        ssd1306_show(&disp);

        contador++;

        if (contador > 10)
        {
            contador = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Atualiza a cada 1 segundo
    }
}

// ===================== main =====================
int main() 
{
    stdio_init_all();

    xTaskCreate(led_interno_task, "WiFi_Task", 1024, NULL, 1, NULL);
    xTaskCreate(led_externo_task, "LED_Ext_Task", 256, NULL, 1, NULL);
    xTaskCreate(sensor_pressao_task, "Sensor_Task", 1024, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED_Task", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while(1) 
    {

    }
}
