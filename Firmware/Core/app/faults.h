/*
 * faults.h
 *
 * Numeros e nomes dos faults do bms_faults_t. O numero de cada fault e a
 * posicao do campo no struct: e o que sai no CAN (0x703 fault1_code), nas
 * mascaras do JSON e na config ams_error_enable da EEPROM. Acrescentar faults
 * so no fim do struct, senao os numeros mudam para toda a gente.
 */

#ifndef APP_FAULTS_H_
#define APP_FAULTS_H_

#include "brain.h"

#define FAULT_COUNT ((uint8_t) (sizeof(bms_faults_t) / sizeof(bool)))

_Static_assert(sizeof(bms_faults_t) == (FAULT_COUNT * sizeof(bool)), "bms_faults_t so pode ter bools");
_Static_assert(sizeof(bms_faults_t) <= 64u, "as mascaras dos faults sao de 64 bits");

/* FS 2027 EV 5.8.3, 5.8.10 e 5.8.13: tensao, temperatura, corrente e perda de
 * medicao tem de abrir o SDC. Estes abrem sempre a linha AMS_ERROR, a config
 * da EEPROM so pode acrescentar outros */
extern const bms_faults_t faults_mandatory;

uint64_t Faults_ToMask(const bms_faults_t *faults);
void Faults_FromMask(uint64_t mask, bms_faults_t *faults);
const char* Faults_Name(uint8_t fault_index);     // "CELL_OVERVOLTAGE"; NULL fora da gama
int Faults_FindByName(const char *name);           // -1 se nao existir (maiusculas ou minusculas)
uint8_t Faults_Count(const bms_faults_t *faults);

#endif /* APP_FAULTS_H_ */
