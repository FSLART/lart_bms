/*******************************************************************************
Copyright (c) 2020 - Analog Devices Inc. All Rights Reserved.
This software is proprietary & confidential to Analog Devices, Inc.
and its licensor.
******************************************************************************
* @file:    mcuWrapper.c
* @brief:   BMS SPI driver functions
* @version: $Revision$
* @date:    $Date$
* Developed by: ADIBMS Software team, Bangalore, India
*****************************************************************************/
/*! \addtogroup MCU DRIVER
*  @{
*/

/*! @addtogroup Mcu Driver
*  @{
*/
#include "common.h"
#include "mcuWrapper.h"

/* Tempos do isoSPI, ADBMS6830B datasheet Rev.0 (Tabela 9 e "Waking Up the
 * Serial Interface"). Sem HAL_Delay: o CS e temporizado pelo TIM5 a 1 MHz */
#define ISOSPI_READY_HOLD_US 3000u   /* tIDLE min 4,3 ms: abaixo disto a porta ainda esta READY */
#define WAKE_IDLE_EDGE_US    20u     /* entre flancos do CS: > tREADY (10 us) */
#define SPI_TIME_OUT         5u      /* ms; maior transacao (16 slaves, 132 B a 1 MHz) ~1,1 ms */

SPI_HandleTypeDef *hspi         = &hspi1;       /* MUC SPI Handler      */

static uint32_t last_activity_us;   /* fim da ultima transacao (CS em cima) */
static uint32_t poll_start_us;
static uint32_t spi_errors;

/**
 *******************************************************************************
 * Function: adBmsTimerClockHz / adBmsTimerInit / adBmsMicros
 * @brief Base de tempo em us (TIM5, 32 bits, free-running)
 *
 * @details O prescaler e calculado a partir do relogio real dos timers do APB1,
 * para o contador andar sempre a 1 MHz mesmo que o CubeMX mude o relogio.
 *******************************************************************************
*/
uint32_t adBmsTimerClockHz(void)
{
  uint32_t timer_clock_hz = HAL_RCC_GetPCLK1Freq();
  /* com o APB1 dividido, os timers correm ao dobro do PCLK1 */
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
  {
    timer_clock_hz *= 2u;
  }
  return timer_clock_hz;
}

void adBmsTimerInit(void)
{
  __HAL_TIM_SET_PRESCALER(&htim5, (adBmsTimerClockHz() / 1000000u) - 1u);
  htim5.Instance->EGR = TIM_EGR_UG;   /* carrega o prescaler ja */
  HAL_TIM_Base_Start(&htim5);
}

uint32_t adBmsMicros(void)
{
  /* um contador parado prendia o wait_us para sempre */
  if ((htim5.Instance->CR1 & TIM_CR1_CEN) == 0u)
  {
    adBmsTimerInit();
  }
  return __HAL_TIM_GET_COUNTER(&htim5);
}

static void wait_us(uint32_t wait_time_us)
{
  uint32_t start_us = adBmsMicros();
  while ((adBmsMicros() - start_us) < wait_time_us)
  {
  }
}

uint32_t adBmsSpiErrors(void)
{
  return spi_errors;
}

static void spi_check(HAL_StatusTypeDef status)
{
  if (status != HAL_OK)
  {
    spi_errors++;
  }
}

/**
 *******************************************************************************
 * Function: adBmsCsLow
 * @brief Select chip select low
 *
 * @details This function does spi chip select low.
 *
 * @return None
 *
 *******************************************************************************
*/
void adBmsCsLow()
{
  HAL_GPIO_WritePin(GPIO_PORT, CS_PIN, GPIO_PIN_RESET);
}

/**
 *******************************************************************************
 * Function: adBmsCsHigh
 * @brief Select chip select High
 *
 * @details This function does spi chip select high.
 *
 * @return None
 *
 *******************************************************************************
*/
void adBmsCsHigh()
{
  HAL_GPIO_WritePin(GPIO_PORT, CS_PIN, GPIO_PIN_SET);
  last_activity_us = adBmsMicros();
}

/**
 *******************************************************************************
 * Function: spiWriteBytes
 * @brief Writes an array of bytes out of the SPI port.
 *
 * @details This function wakeup bms ic in IsoSpi mode send dumy byte data in spi line..
 *
 * @param [in]  size            Numberof bytes to be send on the SPI line
 *
 * @param [in]  *tx_Data    Tx data pointer
 *
 * @return None
 *
 *******************************************************************************
*/
void spiWriteBytes
(
uint16_t size,                     /*Option: Number of bytes to be written on the SPI port*/
uint8_t *tx_Data                       /*Array of bytes to be written on the SPI port*/
)
{
  spi_check(HAL_SPI_Transmit(hspi, tx_Data, size, SPI_TIME_OUT)); /* SPI1 , data, size, timeout */
}

/**
 *******************************************************************************
 * Function: spiWriteReadBytes
 * @brief Writes and read a set number of bytes using the SPI port.
 *
 * @details This function writes and read a set number of bytes using the SPI port.
 *
 * @param [in]  *tx_data    Tx data pointer
 *
 * @param [in]  *rx_data    Rx data pointer
 *
 * @param [in]  size            Data size
 *
 * @return None
 *
 *******************************************************************************
*/
void spiWriteReadBytes
(
uint8_t *tx_data,                   /*array of data to be written on SPI port*/
uint8_t *rx_data,                   /*Input: array that will store the data read by the SPI port*/
uint16_t size                           /*Option: number of bytes*/
)
{
  spi_check(HAL_SPI_Transmit(hspi, tx_data, 4, SPI_TIME_OUT));
  spi_check(HAL_SPI_Receive(hspi, rx_data, size, SPI_TIME_OUT));
}

/**
 *******************************************************************************
 * Function: spiReadBytes
 * @brief Read number of bytes using the SPI port.
 *
 * @details This function Read a set number of bytes using the SPI port.
 *
 * @param [in]  size            Data size
 *
 * @param [in]  *rx_data    Rx data pointer
 *
 * @return None
 *
 *******************************************************************************
*/
void spiReadBytes(uint16_t size, uint8_t *rx_data)
{
  spi_check(HAL_SPI_Receive(hspi, rx_data, size, SPI_TIME_OUT));
}

/**
 *******************************************************************************
 * Function: startTimer() / stopTimer() / getTimCount()
 * @brief Tempo de conversao para o adBmsPollAdc do vendor, em us (TIM5).
 *
 * @details Antes usava o TIM2 e punha-lhe o contador a 0, o que estragava o
 * input capture do TIM2. O TIM5 nunca e reposto: so se guarda o inicio.
 *******************************************************************************
*/
void startTimer()
{
  poll_start_us = adBmsMicros();
}

void stopTimer()
{
}

uint32_t getTimCount()
{
  return adBmsMicros() - poll_start_us;
}


/**
 *******************************************************************************
 * Function: adBmsWakeupIc
 * @brief Acorda a porta isoSPI do IDLE (nucleo em STANDBY).
 *
 * @details O vendor chama isto antes de cada transacao. Se houve atividade ha
 * menos de tIDLE a porta ainda esta READY e nao se faz nada. Senao, um par de
 * flancos de CS (-1/+1) por slave, separados por mais de tREADY. Acordar do
 * SLEEP (pulsos > tWAKE) e feito pela maquina de estados ADBMS_WAKE_LOW/HIGH.
 *
 * @param [in]  total_ic    Total_ic
 *
 * @return None
 *
 *******************************************************************************
*/
void adBmsWakeupIc(uint8_t total_ic)
{
  if ((adBmsMicros() - last_activity_us) < ISOSPI_READY_HOLD_US)
  {
    return;
  }
  for (uint8_t pulse_counter = 0; pulse_counter < total_ic; pulse_counter++)
  {
    adBmsCsLow();
    wait_us(WAKE_IDLE_EDGE_US);
    adBmsCsHigh();
    wait_us(WAKE_IDLE_EDGE_US);
  }
}

/** @}*/
/** @}*/
