/*
 * fake_spi_chain.h - uma cadeia de ADBMS6830 ao nivel do SPI, para testar o
 * driver da ADI (adBms6830GenericType.c, ParseCreate.c) sem o fake_chain.c
 * por cima. Faz o que o datasheet diz:
 *
 *   comando  2 bytes + PEC15; com o PEC errado a cadeia ignora o comando
 *   escrita  6 bytes + PEC10 por slave, o primeiro bloco vai para o ULTIMO
 *            slave da cadeia (o mais longe do master); PEC10 errado = ignorado
 *   leitura  o slave mais perto do master responde primeiro: dados + 6 bits do
 *            contador de comandos + PEC10 sobre os dados e o contador
 *
 * Os PEC sao calculados aqui com uma implementacao propria, bit a bit, a partir
 * dos polinomios do datasheet (nao com as tabelas da ADI): os testes conferem
 * uma com a outra.
 */

#ifndef FAKE_SPI_CHAIN_H
#define FAKE_SPI_CHAIN_H

#include <stdbool.h>
#include <stdint.h>

#define SPI_CHAIN_MAX     16u
#define SPI_REG_MAX       70u     // RDASALL: 68 B + PEC

typedef struct {
	uint8_t  regs[0x800][SPI_REG_MAX];   // por codigo de comando de LEITURA (11 bits)
	uint8_t  cc;                         // contador de comandos (6 bits)
	bool     corrupt_next_read;          // estraga um bit na proxima resposta
} spi_device_t;

typedef struct {
	uint8_t      count;                  // slaves na cadeia
	spi_device_t devices[SPI_CHAIN_MAX];
	uint16_t     last_command;           // ultimo comando com PEC15 bom
	uint32_t     commands;               // comandos aceites
	uint32_t     bad_command_pec;
	uint32_t     bad_data_pec;
	uint32_t     wakeups;
	uint32_t     transactions;           // CS em baixo
	bool         cs_low;
	bool         rx_fail;                // o SPI nao recebe nada (o buffer fica como estava)
	uint32_t     timer_count;            // o que o getTimCount devolve
} spi_chain_t;

extern spi_chain_t spi_chain;

void spi_chain_reset(uint8_t count);

// tamanho da resposta de cada slave a um comando de leitura (8 ou o de um RD*ALL)
uint8_t spi_chain_read_size(uint16_t command);

uint16_t ref_pec15(const uint8_t *data, uint8_t length);
uint16_t ref_pec10(const uint8_t *data, uint8_t length, uint8_t counter6);

#endif
