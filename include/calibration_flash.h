#ifndef CALIBRATION_FLASH_H
#define CALIBRATION_FLASH_H

#include <stdint.h>
#include <stdbool.h>

/* Valor que indica calibração válida */
#define CALIBRATION_VALID_FLAG   123

/* Estrutura armazenada na flash */
typedef struct {
    uint8_t  calibrated_flag;   // 123 = calibrado
    uint16_t tare;              // valor da tara
    float    scale_factor;      // fator de calibração
} calibration_data_t;

/* API pública do módulo */

/**
 * @brief Lê os dados de calibração da flash
 * @param data Ponteiro para estrutura de saída
 */
void calibration_flash_read(calibration_data_t *data);

/**
 * @brief Grava os dados de calibração na flash
 * @param data Ponteiro para estrutura com os dados
 */
void calibration_flash_write(const calibration_data_t *data);

/**
 * @brief Verifica se a balança já está calibrada
 * @return true se calibrada, false caso contrário
 */
bool calibration_flash_is_valid(void);

#endif /* CALIBRATION_FLASH_H */
