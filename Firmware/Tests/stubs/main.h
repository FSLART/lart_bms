/*
 * main.h (stub) - so para os testes no PC.
 *
 * Substitui o main.h e o HAL do STM32: tipos e macros minimos que o codigo do
 * BMS usa, com perifericos falsos em memoria. As funcoes estao em
 * fake_chain.c (testes da camada ADBMS) e test_wrapper.c (mcuWrapper).
 */

#ifndef TEST_STUB_MAIN_H
#define TEST_STUB_MAIN_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { RESET = 0, SET = 1 } FlagStatus;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;

typedef struct { volatile uint32_t CR1, DIER, SR, EGR, CNT, PSC, ARR; } TIM_TypeDef;
typedef struct { volatile uint32_t ODR; } GPIO_TypeDef;
typedef struct { volatile uint32_t CFGR, CSR; } RCC_TypeDef;

typedef struct { uint32_t Prescaler, CounterMode, Period, ClockDivision, AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { int unused; } SPI_HandleTypeDef;
typedef struct { int unused; } ADC_HandleTypeDef;
typedef struct { int unused; } I2C_HandleTypeDef;
typedef struct { int unused; } UART_HandleTypeDef;

extern TIM_TypeDef fake_tim5, fake_tim6;
extern RCC_TypeDef fake_rcc;
extern GPIO_TypeDef fake_gpioa;

#define TIM5 (&fake_tim5)
#define TIM6 (&fake_tim6)
#define RCC  (&fake_rcc)

#define BMS_CS_Pin       0x0010u
#define BMS_CS_GPIO_Port (&fake_gpioa)

#define TIM_CR1_CEN                   0x1u
#define TIM_EGR_UG                    0x1u
#define TIM_FLAG_UPDATE               0x1u
#define TIM_COUNTERMODE_UP            0u
#define TIM_AUTORELOAD_PRELOAD_ENABLE 0x80u
#define RCC_CFGR_PPRE1                (0x7u << 10)
#define RCC_CFGR_PPRE1_DIV1           0u
#define RCC_CFGR_PPRE1_DIV2           (0x4u << 10)
#define RCC_CSR_WWDGRSTF              (1u << 30)
#define TIM6_IRQn                     54

#define __HAL_TIM_GET_FLAG(h, f)       ((((h)->Instance->SR) & (f)) == (f))
#define __HAL_TIM_CLEAR_FLAG(h, f)     ((h)->Instance->SR &= ~(uint32_t) (f))
#define __HAL_TIM_SET_PRESCALER(h, v)  ((h)->Instance->PSC = (v))
#define __HAL_TIM_GET_COUNTER(h)       fake_tim_counter(h)
#define __HAL_RCC_TIM6_CLK_ENABLE()    ((void) 0)

uint32_t fake_tim_counter(TIM_HandleTypeDef *htim);
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size, uint32_t timeout);

#endif
