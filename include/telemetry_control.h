#ifndef TELEMETRY_CONTROL_H
#define TELEMETRY_CONTROL_H

#include "FreeRTOS.h"
#include "queue.h"

/**
 * @brief Configura a fila que será usada para enviar comandos para a bomba.
 * @param queue Handle da fila criada no main.
 */
void telemetry_set_bomba_queue(QueueHandle_t queue);

/**
 * @brief Task do FreeRTOS que processa o protocolo serial.
 * Protocolo: [0xAA][CMD][BCD1][BCD2][0x55]
 */
void serial_telemetry_task(void *pvParameters);

#endif