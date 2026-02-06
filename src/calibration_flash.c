#include "calibration_flash.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

/* ================= CONFIGURAÇÃO DA FLASH ================= */

#define FLASH_PAGE_SIZE       4096
#define FLASH_SECTOR_SIZE     4096
#define FLASH_TARGET_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

/* ================= IMPLEMENTAÇÃO ================= */

void calibration_flash_read(calibration_data_t *data)
{
    const uint8_t *flash_ptr =
        (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

    memcpy(data, flash_ptr, sizeof(calibration_data_t));
}

void calibration_flash_write(const calibration_data_t *data)
{
    uint8_t buffer[FLASH_PAGE_SIZE];

    /* Preenche com 0xFF (flash apagada) */
    memset(buffer, 0xFF, FLASH_PAGE_SIZE);

    /* Copia os dados para o início da página */
    memcpy(buffer, data, sizeof(calibration_data_t));

    uint32_t ints = save_and_disable_interrupts();

    /* Apaga o setor */
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);

    /* Grava o setor */
    flash_range_program(FLASH_TARGET_OFFSET, buffer, FLASH_PAGE_SIZE);

    restore_interrupts(ints);
}

bool calibration_flash_is_valid(void)
{
    calibration_data_t data;
    calibration_flash_read(&data);

    return (data.calibrated_flag == CALIBRATION_VALID_FLAG);
}
