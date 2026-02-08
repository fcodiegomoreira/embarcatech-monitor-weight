#ifndef DATA_UTILS_H
#define DATA_UTILS_H

#include <stdint.h>

/**
 * @brief Converte um byte no formato BCD (Binary-Coded Decimal) para decimal inteiro.
 * Exemplo: 0x50 torna-se 50.
 * * @param bcd O valor em BCD (0x00 a 0x99).
 * @return int O valor convertido para base 10.
 */
int bcd_to_decimal(uint8_t bcd);

/**
 * @brief Converte um valor decimal inteiro para o formato BCD.
 * Exemplo: 50 torna-se 0x50. (Útil se precisar enviar BCD de volta)
 * * @param decimal O valor em base 10 (0 a 99).
 * @return uint8_t O valor codificado em BCD.
 */
uint8_t decimal_to_bcd(uint8_t decimal);

#endif // DATA_UTILS_H