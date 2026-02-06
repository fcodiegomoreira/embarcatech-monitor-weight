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

// ================= GLOBAIS =================
SemaphoreHandle_t xSemaforoBotao;
QueueHandle_t xFilaContador;
SemaphoreHandle_t xMutexConsole;

static uint32_t ultimo_tempo_botao = 0;
static volatile bool requisicao_em_curso = false;

calibration_data_t FlashParamsCalibration;

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
        if (xSemaphoreTake(xMutexConsole, pdMS_TO_TICKS(100)) == pdPASS)
        {
            va_list args;
            va_start(args, format);
            vprintf(format, args);
            va_end(args);
            xSemaphoreGive(xMutexConsole);
        }
    }
}

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

    requisicao_em_curso = false;
}

// ===================== TASK HTTP =====================
void http_post_task(void *pvParameters)
{
    int valor_recebido;
    static httpc_connection_t settings;
    static char uri_com_dados[64];

    while (true)
    {
        if (xQueueReceive(xFilaContador, &valor_recebido, portMAX_DELAY))
        {
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP)
            {
                requisicao_em_curso = true;

                memset(&settings, 0, sizeof(settings));
                settings.result_fn = http_client_callback;

                snprintf(uri_com_dados, sizeof(uri_com_dados),
                         "/data?contador=%d", valor_recebido);

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

                if (erro != ERR_OK)
                {
                    TaskPrint("HTTP: Erro %d\n", erro);
                    requisicao_em_curso = false;
                    vTaskDelay(pdMS_TO_TICKS(10000));
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

void release_product_button(void *pvParameters)
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
            TaskPrint("Botao A pressionado!\n");
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
    i2c_init(i2c1, 400000);
    gpio_set_function(14, GPIO_FUNC_I2C);
    gpio_set_function(15, GPIO_FUNC_I2C);
    gpio_pull_up(14);
    gpio_pull_up(15);

    ssd1306_t disp;
    disp.external_vcc = false;
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
        if (contador > 10)
            contador = 0;

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
        float peso = hx711_get_weight(
            HX711_DATA_PIN,
            HX711_SCLK_PIN,
            offset,
            scale);

        TaskPrint("Peso: %.2f g\n", peso);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ===================== MAIN =====================
int main()
{
    stdio_init_all();
    sleep_ms(5000);

    calibration_flash_read(&FlashParamsCalibration);

    if (FlashParamsCalibration.calibrated_flag != CALIBRATION_VALID_FLAG)
    {
        printf("Sistema nao calibrado!\n");
        execute_calibration(&FlashParamsCalibration);
        calibration_flash_write(&FlashParamsCalibration);
    }

    xMutexConsole = xSemaphoreCreateMutex();
    xSemaforoBotao = xSemaphoreCreateBinary();
    xFilaContador = xQueueCreate(1, sizeof(int));

    xTaskCreate(http_post_task, "HTTP", 4096, NULL, 1, NULL);
    xTaskCreate(release_product_button, "Botao", 512, NULL, 2, NULL);
    xTaskCreate(wifi_connect_device, "WiFi", 1024, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 1024, NULL, 1, NULL);
    xTaskCreate(hx711_task, "HX711", 1024, NULL, 2, NULL);

    vTaskStartScheduler();

    while (1) {}
}
