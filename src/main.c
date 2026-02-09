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
#include "screen_display.h"
#include "telemetry_control.h"

// ================= CONFIGURAÇÕES =================
#define WIFI_SSID       "Gesilane"
#define WIFI_PASSWORD   "bruxxf6d"
#define SERVER_IP       "192.168.1.2"
#define SERVER_PORT     5000

#define LED_EXTERNO_PIN 11
#define BOTAO_A_PIN     5
#define BOTAO_B_PIN     6

static volatile bool sistema_bloqueado = false;

// ================= GLOBAIS =================
SemaphoreHandle_t xSemaforoBotao;
QueueHandle_t xFilaContador;
QueueHandle_t xFilaPeso; // Substitua xFilaContador por esta
SemaphoreHandle_t xMutexConsole;

static uint32_t ultimo_tempo_botao = 0;
static volatile bool requisicao_em_curso = false;

static calibration_data_t FlashParamsCalibration;

// ====== FreeRTOS Static Memory ======
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                  StackType_t **ppxIdleTaskStackBuffer,
                                  uint32_t *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                   StackType_t **ppxTimerTaskStackBuffer,
                                   uint32_t *pulTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

// ================= PRINT THREAD-SAFE =================
void TaskPrint(const char *format, ...)
{
    if (xMutexConsole != NULL)
    {
        if (xSemaphoreTake(xMutexConsole, pdMS_TO_TICKS(10)) == pdPASS)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
            xSemaphoreGive(xMutexConsole);
        }
    }
}

TaskHandle_t xHTTPTaskHandle = NULL;

// ===================== CALLBACK HTTP =====================
static void http_client_callback(void *arg, httpc_result_t httpc_result,
                                 u32_t rx_content_len, u32_t srv_res, err_t err)
{
    int res_limpo = (int)((intptr_t)httpc_result & 0xFF);

    if (res_limpo == 0)
    {
        TaskPrint("HTTP: Sucesso! Status: %u\n", (unsigned int)srv_res);
    }
    else if (res_limpo > 0 && res_limpo < 10)
    {
        TaskPrint("HTTP: Falha Cod %d\n", res_limpo);
    }

    // Após o término (sucesso ou falha), voltamos a prioridade da Task HTTP para 1
    if (xHTTPTaskHandle != NULL) {
        vTaskPrioritySet(xHTTPTaskHandle, 1);
    }

    requisicao_em_curso = false;
}

// ===================== TASK HTTP =====================
void http_post_task(void *pvParameters)
{
    float valor_recebido; // Alterado para float para receber o peso
    static httpc_connection_t settings;
    static char uri_com_dados[64];

    xHTTPTaskHandle = xTaskGetCurrentTaskHandle();

    while (true)
    {
        // Agora recebe da xFilaPeso
        if (xQueueReceive(xFilaPeso, &valor_recebido, portMAX_DELAY))
        {   

            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP)
            {   
                vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

                requisicao_em_curso = true;

                memset(&settings, 0, sizeof(settings));
                settings.result_fn = http_client_callback;

                // Alterado para formatar como peso (float) em vez de contador
                snprintf(uri_com_dados, sizeof(uri_com_dados),
                         "/data?peso=%.2f", valor_recebido);

                cyw43_arch_lwip_begin();
                err_t erro = httpc_get_file_dns(
                    SERVER_IP,
                    SERVER_PORT,
                    uri_com_dados,
                    &settings,
                    (httpc_result_fn)http_client_callback,
                    NULL,
                    NULL);
                cyw43_arch_lwip_end();

                if(erro != ERR_OK)
                {
                    TaskPrint("HTTP: Erro de disparo %d\n", erro);
                    // IMPORTANTE: Se o disparo falhou, o callback não será chamado.
                    // Precisamos devolver a prioridade para o resto do sistema respirar.
                    vTaskPrioritySet(NULL, 1); 
                    requisicao_em_curso = false;
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
            }
        }
    }
}

// ===================== BOTÃO =====================
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

void calibration_system_button(void *pvParameters)
{
    gpio_init(BOTAO_A_PIN);
    gpio_set_dir(BOTAO_A_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_A_PIN);

    gpio_set_irq_enabled_with_callback(
        BOTAO_A_PIN,
        GPIO_IRQ_EDGE_FALL,
        true,
        &release_product_irq_handler);

    while (true)
    {
        if (xSemaphoreTake(xSemaforoBotao, portMAX_DELAY) == pdPASS)
        {   
            sistema_bloqueado = true;

            UBaseType_t prioridadeOriginal = uxTaskPriorityGet(NULL);
            vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

            execute_calibration(&FlashParamsCalibration);
            calibration_flash_write(&FlashParamsCalibration);

            vTaskPrioritySet(NULL, prioridadeOriginal);
            vTaskDelay(pdMS_TO_TICKS(100));
            
            oled_screen_finished_calibration();

            sistema_bloqueado = false;
        }
    }
}

// ===================== WIFI =====================
void wifi_connect_device(void *pvParameters)
{
    if (cyw43_arch_init())
        vTaskDelete(NULL);

    cyw43_arch_enable_sta_mode();
    cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK,
        30000);

    while (true)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ===================== OLED =====================
void oled_task(void *pvParameters)
{
    int contador = 0;
    char buffer[20];

    while (true)
    {
        if (sistema_bloqueado) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        oled_screen_show_vending();

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ===================== HX711 TASK =====================
void hx711_task(void *pvParameters)
{
    hx711_init(HX711_DATA_PIN, HX711_SCLK_PIN);
    sleep_ms(3000);

    long offset = FlashParamsCalibration.tare;
    float scale = FlashParamsCalibration.scale_factor;

    TaskPrint("HX711 iniciado\n");
    TaskPrint("Offset: %ld | Scale: %.4f\n", offset, scale);

    while (true)
    {

        if (sistema_bloqueado) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        float peso = hx711_get_weight(
            HX711_DATA_PIN,
            HX711_SCLK_PIN,
            offset,
            scale);

        TaskPrint("Peso: %.2f g\n", peso);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void bomba_task(void *pvParameters) {
    // Pegamos a fila que foi passada por parâmetro na criação da task
    QueueHandle_t fila = (QueueHandle_t)pvParameters;
    int tempo_recebido;
    
    // Configuração do pino (Exemplo: pino 12)
    const uint PINO_BOMBA = 12; 
    gpio_init(PINO_BOMBA);
    gpio_set_dir(PINO_BOMBA, GPIO_OUT);
    gpio_put(PINO_BOMBA, 0); // Garante que começa desligada

    while (true) {
        // xQueueReceive trava a task aqui até que chegue algo na fila
        // portMAX_DELAY significa: "espere o tempo que for preciso"
        if (xQueueReceive(fila, &tempo_recebido, portMAX_DELAY)) {
            
            TaskPrint("BOMBA: Ativando por %d ms...\n", tempo_recebido);
            
            gpio_put(PINO_BOMBA, 1);               // LIGA
            vTaskDelay(pdMS_TO_TICKS(tempo_recebido)); // ESPERA (sem travar o resto do sistema)
            gpio_put(PINO_BOMBA, 0);               // DESLIGA
            
            vTaskDelay(pdMS_TO_TICKS(500));

            float peso_final = hx711_get_weight(
                HX711_DATA_PIN, 
                HX711_SCLK_PIN, 
                FlashParamsCalibration.tare, 
                FlashParamsCalibration.scale_factor
            );

            // 2. Enviar o valor lido para a fila de telemetria HTTP
            if (xFilaPeso != NULL) {
                xQueueSend(xFilaPeso, &peso_final, 0);
            }
            
            TaskPrint("BOMBA: Ciclo finalizado.\n");
        }
    }
}

// ===================== MAIN =====================
int main()
{
    stdio_init_all();

    xMutexConsole = xSemaphoreCreateMutex();
    xSemaforoBotao = xSemaphoreCreateBinary();
    xFilaContador = xQueueCreate(1, sizeof(int));

    xFilaPeso = xQueueCreate(5, sizeof(float)); // Fila para valores float

    oled_screen_init_device();

    gpio_init(BOTAO_B_PIN);
    gpio_set_dir(BOTAO_B_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_B_PIN);

    char buffer[20];

    calibration_flash_read(&FlashParamsCalibration);


    if (FlashParamsCalibration.calibrated_flag != CALIBRATION_VALID_FLAG)
    {   
        execute_calibration(&FlashParamsCalibration);

        bool button_a;

        do 
        {
            tight_loop_contents();
        } while (gpio_get(BOTAO_B_PIN) == 1);
        
        calibration_flash_write(&FlashParamsCalibration);
        oled_screen_finished_calibration();
    }

    QueueHandle_t xFilaTemp = xQueueCreate(5, sizeof(int));
    telemetry_set_bomba_queue(xFilaTemp);

    xTaskCreate(http_post_task, "HTTP", 4096, NULL, 1, NULL);
    xTaskCreate(calibration_system_button, "Botao", 2048, NULL, 2, NULL);
    xTaskCreate(wifi_connect_device, "WiFi", 1024, NULL, 2, NULL);
    xTaskCreate(oled_task, "OLED", 2048, NULL, 1, NULL);
    xTaskCreate(hx711_task, "HX711", 2048, NULL, 2, NULL);
    xTaskCreate(serial_telemetry_task, "Telemetry", 1024, NULL, 1, NULL);
    xTaskCreate(bomba_task, "BombaTask", 1024, (void*)xFilaTemp, 2, NULL);

    vTaskStartScheduler();

    while (1) 
    {

    }
}
