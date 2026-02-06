#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hx711.h"
#include "hardware/sync.h"
#include "hardware/i2c.h"
#include "lwip/apps/http_client.h"
#include "ssd1306.h"
#include <stdarg.h>
#include "calibration_flash.h"

// ================= CONFIGURAÇÕES =================
#define WIFI_SSID       "Gesilane"
#define WIFI_PASSWORD   "bruxxf6d"
#define SERVER_IP       "192.168.1.2" 
#define SERVER_PORT     5000

#define LED_EXTERNO_PIN 11
#define BOTAO_A_PIN     5

// Globais
SemaphoreHandle_t xSemaforoBotao;
QueueHandle_t xFilaContador;
SemaphoreHandle_t xMutexConsole;
static uint32_t ultimo_tempo_botao = 0;
static volatile bool requisicao_em_curso = false;
calibration_data_t FlashParamsCalibration;
hx711_config_t SensorDataCalibration;

// ====== FreeRTOS Static Memory ======
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB; *ppxIdleTaskStackBuffer = uxIdleTaskStack; *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB; *ppxTimerTaskStackBuffer = uxTimerTaskStack; *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}


void TaskPrint(const char *format, ...) {
    if (xMutexConsole != NULL) {
        // Tenta pegar o Mutex. Espera até 100ms se estiver ocupado
        if (xSemaphoreTake(xMutexConsole, pdMS_TO_TICKS(100)) == pdPASS) {
            va_list args;
            va_start(args, format);
            vprintf(format, args); // vprintf é a versão do printf para argumentos variados
            va_end(args);
            xSemaphoreGive(xMutexConsole); // Solta o console para a próxima task
        }
    }
}

// ===================== CALLBACK HTTP =====================
static void http_client_callback(void *arg, httpc_result_t httpc_result, 
                                 u32_t rx_content_len, u32_t srv_res, err_t err)
{
    // Forçamos a conversão para um tipo de dado simples
    int res_limpo = (int)((intptr_t)httpc_result & 0xFF); 

    // Se o resultado for 0 (HTTPC_RESULT_OK), imprimimos o status do servidor
    if (res_limpo == 0) 
    {
        TaskPrint("HTTP: Sucesso! Status: %u\n", (unsigned int)srv_res);
    } 
    
    else 
    if (res_limpo > 0 && res_limpo < 10) 
    {
        TaskPrint("HTTP: Falha Cod %d\n", res_limpo);
    }


    requisicao_em_curso = false; 
}

// ===================== TASK: ENVIO HTTP (MODIFICADA) =====================
void http_post_task(void *pvParameters) 
{
    int valor_recebido;
    static httpc_connection_t settings;
    static char uri_com_dados[64];

    while (true) 
    {
        if (xQueueReceive(xFilaContador, &valor_recebido, portMAX_DELAY)) 
        {            
            // Se houver falha persistente, este bloco garante que não tentaremos 
            // abrir conexões infinitas atropelando o hardware
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) 
            {
                requisicao_em_curso = true;
                
                memset(&settings, 0, sizeof(settings));
                settings.result_fn = http_client_callback;
                settings.use_proxy = 0;

                snprintf(uri_com_dados, sizeof(uri_com_dados), "/data?contador=%d", valor_recebido);

                cyw43_arch_lwip_begin();
err_t erro_conexao = httpc_get_file_dns(SERVER_IP, SERVER_PORT, uri_com_dados, &settings, (httpc_result_fn)http_client_callback, NULL, NULL);
cyw43_arch_lwip_end();

        if (erro_conexao != ERR_OK) 
        {
            TaskPrint("HTTP: Erro %d detectado. Resetando...\n", erro_conexao);
            
            requisicao_em_curso = false; 
            
            vTaskDelay(pdMS_TO_TICKS(10000)); 
        }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); 
    }
}

// ===================== OUTRAS TASKS =====================

void release_product_irq_handler(uint gpio, uint32_t events) 
{
    uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());

    if (tempo_atual - ultimo_tempo_botao > 250) 
    {
        ultimo_tempo_botao = tempo_atual;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        xSemaphoreGiveFromISR(xSemaforoBotao, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void release_product_button(void *pvParameters) 
{
    gpio_init(BOTAO_A_PIN); 
    gpio_set_dir(BOTAO_A_PIN, GPIO_IN); 
    gpio_pull_up(BOTAO_A_PIN);

    gpio_set_irq_enabled_with_callback(BOTAO_A_PIN, GPIO_IRQ_EDGE_FALL, true, &release_product_irq_handler);

    while (true) 
    {
        if (xSemaphoreTake(xSemaforoBotao, portMAX_DELAY) == pdPASS) 
        {
            printf("Botao A pressionado!\n");
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void wifi_connect_device(void *pvParameters) 
{
    if (cyw43_arch_init()) 
    { 
        vTaskDelete(NULL); 
    }

    cyw43_arch_enable_sta_mode();
    cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);

    while (true) 
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); 
        vTaskDelay(pdMS_TO_TICKS(500));

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); 
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}



        // float peso_final = hx711_get_weight(config.pin_dt, config.pin_sck, config.offset, config.scale);
        // TaskPrint("Peso: %.2f g\n", peso_final);
        // vTaskDelay(pdMS_TO_TICKS(1000));
    

void oled_task(void *pvParameters) 
{
    i2c_init(i2c1, 400000);
    gpio_set_function(14, GPIO_FUNC_I2C); 
    gpio_set_function(15, GPIO_FUNC_I2C);
    gpio_pull_up(14); gpio_pull_up(15);

    ssd1306_t disp; disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);

    int contador = 0;
    char buffer[20];

    while (true) 
    {
        ssd1306_clear(&disp);
        ssd1306_draw_string(&disp, 0, 8, 1, "Contador:");
        snprintf(buffer, sizeof(buffer), "%d", contador);
        ssd1306_draw_string(&disp, 0, 24, 2, buffer);
        ssd1306_show(&disp);
        
        xQueueOverwrite(xFilaContador, &contador);
        
        contador++;
        if (contador > 10) contador = 0;
        vTaskDelay(pdMS_TO_TICKS(10000)); // Atualiza a cada 10 segundos
    }
}

// ===================== MAIN =====================
int main()
{
    stdio_init_all();
    sleep_ms(5000);

    // Lê dados de calibração da flash
    calibration_flash_read(&FlashParamsCalibration);

    // Verifica se a calibração é válida
    if (FlashParamsCalibration.calibrated_flag != CALIBRATION_VALID_FLAG)
    {
        printf("Sistema nao calibrado!\n\n");
        execute_calibration(&FlashParamsCalibration);

        FlashParamsCalibration.calibrated_flag = CALIBRATION_VALID_FLAG;
        FlashParamsCalibration.tare = SensorDataCalibration.offset;
        FlashParamsCalibration.scale_factor = SensorDataCalibration.scale;

        calibration_flash_write(&FlashParamsCalibration);
    }
    else
    {
        printf("Calibrado");
    }

    // Inicializa RTOS
    xMutexConsole = xSemaphoreCreateMutex();
    xSemaforoBotao = xSemaphoreCreateBinary();
    xFilaContador = xQueueCreate(1, sizeof(int));

    xTaskCreate(http_post_task, "HTTP_Task", 4096, NULL, 1, NULL);
    xTaskCreate(release_product_button, "Botao_Task", 512, NULL, 2, NULL);
    xTaskCreate(wifi_connect_device, "WiFi_Task", 1024, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED_Task", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {}
}

