#include "data_utils.h"

int bcd_to_decimal(uint8_t bcd) {
    // bcd >> 4 isola os 4 bits superiores (dezena)
    // bcd & 0x0F isola os 4 bits inferiores (unidade)
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

uint8_t decimal_to_bcd(uint8_t decimal) {
    // (decimal / 10) pega o dígito da dezena e desloca para os 4 bits superiores
    // (decimal % 10) pega o dígito da unidade e coloca nos 4 bits inferiores
    return ((decimal / 10) << 4) | (decimal % 10);
}