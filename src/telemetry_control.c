#include <stdio.h>
#include <string.h>
#include "telemetry_control.h"
#include "data_utils.h"
#include "pico/stdlib.h"
#include "task.h"

// Variável privada (encapsulada)
static QueueHandle_t xFilaBombaPrivada = NULL;

// Protótipo da TaskPrint (externo)
extern void TaskPrint(const char *format, ...);

// Função para conectar a fila criada no main ao módulo
void telemetry_set_bomba_queue(QueueHandle_t queue) {
    xFilaBombaPrivada = queue;
}

void serial_telemetry_task(void *pvParameters) {
    uint8_t packet[5];
    int state = 0; 
    int bytes_received = 1;

    while (true) {
        // Leitura não bloqueante
        int c = getchar_timeout_us(0); 
        
        if (c != PICO_ERROR_TIMEOUT) {
            uint8_t byte = (uint8_t)c;

            switch (state) {
                case 0: // Procurando Início (0xAA)
                    if (byte == 0xAA) {
                        packet[0] = byte;
                        bytes_received = 1;
                        state = 1;
                    }
                    break;

                case 1: // Recebendo Corpo (3 bytes)
                    packet[bytes_received++] = byte;
                    if (bytes_received >= 4) { 
                        state = 2;
                    }
                    break;

                case 2: // Validando Fim (0x55)
                    if (byte == 0x55) {
                        packet[4] = byte;
                        
                        uint8_t comando = packet[1];
                        // Conversão BCD usando o módulo data_utils
                        int tempo = (bcd_to_decimal(packet[2]) * 100) + bcd_to_decimal(packet[3]);

                        // Validação de Comando e Faixa de Tempo
                        if (comando == 0x01 && tempo >= 2000 && tempo <= 9000) {
                            TaskPrint("Telemetry: Comando Ativar recebido. Tempo: %d ms\n", tempo);
                            
                            // Tenta enviar para a fila privada
                            if (xFilaBombaPrivada != NULL) {
                                if (xQueueSend(xFilaBombaPrivada, &tempo, 0) != pdPASS) {
                                    TaskPrint("Telemetry: Erro ao enviar para a fila (Fila cheia)\n");
                                }
                            } else {
                                TaskPrint("Telemetry: Erro! Fila da bomba nao configurada.\n");
                            }
                        } else {
                            TaskPrint("Telemetry: Pacote invalido (CMD %02X / Tempo %d)\n", comando, tempo);
                        }
                    }
                    state = 0; // Reinicia para o próximo pacote
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}