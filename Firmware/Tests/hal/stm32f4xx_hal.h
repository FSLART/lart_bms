/*
 * stm32f4xx_hal.h (falso) - o HAL do STM32F4 para os testes dos drivers no PC.
 *
 * So para o test_drivers.c: o can_bus.c, o uart_link.c e o bsp.c compilam tal
 * como estao, com o main.h verdadeiro (pinos do CubeMX) a incluir isto. Os
 * tipos, constantes e campos sao os do HAL (os mesmos valores), as funcoes
 * estao no fake_hal.c e gravam o que lhes pedem.
 */

#ifndef TEST_FAKE_STM32F4XX_HAL_H
#define TEST_FAKE_STM32F4XX_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum { HAL_OK = 0x00, HAL_ERROR = 0x01, HAL_BUSY = 0x02, HAL_TIMEOUT = 0x03 } HAL_StatusTypeDef;
typedef enum { RESET = 0, SET = !RESET } FlagStatus;
typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef enum { EXTI15_10_IRQn = 40 } IRQn_Type;

#define __IO volatile

/* ================== GPIO ================== */

typedef struct {
	char     name;            // 'A', 'B', 'C', 'H'
	uint32_t IDR;             // o que os testes poem nas entradas
	uint32_t ODR;             // o que o codigo escreveu
} GPIO_TypeDef;

extern GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc, fake_gpioh;
#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define GPIOC (&fake_gpioc)
#define GPIOH (&fake_gpioh)

#define GPIO_PIN_0   ((uint16_t) 0x0001)
#define GPIO_PIN_1   ((uint16_t) 0x0002)
#define GPIO_PIN_2   ((uint16_t) 0x0004)
#define GPIO_PIN_3   ((uint16_t) 0x0008)
#define GPIO_PIN_4   ((uint16_t) 0x0010)
#define GPIO_PIN_5   ((uint16_t) 0x0020)
#define GPIO_PIN_6   ((uint16_t) 0x0040)
#define GPIO_PIN_7   ((uint16_t) 0x0080)
#define GPIO_PIN_8   ((uint16_t) 0x0100)
#define GPIO_PIN_9   ((uint16_t) 0x0200)
#define GPIO_PIN_10  ((uint16_t) 0x0400)
#define GPIO_PIN_11  ((uint16_t) 0x0800)
#define GPIO_PIN_12  ((uint16_t) 0x1000)
#define GPIO_PIN_13  ((uint16_t) 0x2000)
#define GPIO_PIN_14  ((uint16_t) 0x4000)
#define GPIO_PIN_15  ((uint16_t) 0x8000)

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);

/* ================== CAN ================== */

typedef struct {
	uint32_t ESR;             // contadores e ultimo erro, como no bxCAN
} CAN_TypeDef;

extern CAN_TypeDef fake_can1_regs, fake_can2_regs;
#define CAN1 (&fake_can1_regs)
#define CAN2 (&fake_can2_regs)

typedef struct {
	uint32_t Prescaler;
	uint32_t Mode;
	uint32_t SyncJumpWidth;
	uint32_t TimeSeg1;
	uint32_t TimeSeg2;
	FunctionalState TimeTriggeredMode;
	FunctionalState AutoBusOff;
	FunctionalState AutoWakeUp;
	FunctionalState AutoRetransmission;
	FunctionalState ReceiveFifoLocked;
	FunctionalState TransmitFifoPriority;
} CAN_InitTypeDef;

typedef struct {
	CAN_TypeDef *Instance;
	CAN_InitTypeDef Init;
	uint32_t ErrorCode;
} CAN_HandleTypeDef;

typedef struct {
	uint32_t StdId;
	uint32_t ExtId;
	uint32_t IDE;
	uint32_t RTR;
	uint32_t DLC;
	FunctionalState TransmitGlobalTime;
} CAN_TxHeaderTypeDef;

typedef struct {
	uint32_t StdId;
	uint32_t ExtId;
	uint32_t IDE;
	uint32_t RTR;
	uint32_t DLC;
	uint32_t Timestamp;
	uint32_t FilterMatchIndex;
} CAN_RxHeaderTypeDef;

typedef struct {
	uint32_t FilterIdHigh;
	uint32_t FilterIdLow;
	uint32_t FilterMaskIdHigh;
	uint32_t FilterMaskIdLow;
	uint32_t FilterFIFOAssignment;
	uint32_t FilterBank;
	uint32_t FilterMode;
	uint32_t FilterScale;
	uint32_t FilterActivation;
	uint32_t SlaveStartFilterBank;
} CAN_FilterTypeDef;

#define CAN_ID_STD                  0x00000000U
#define CAN_ID_EXT                  0x00000004U
#define CAN_RTR_DATA                0x00000000U
#define CAN_RX_FIFO0                0x00000000U
#define CAN_FILTERMODE_IDMASK       0x00000000U
#define CAN_FILTERMODE_IDLIST       0x00000001U
#define CAN_FILTERSCALE_16BIT       0x00000000U
#define CAN_FILTERSCALE_32BIT       0x00000001U
#define CAN_SJW_1TQ                 0x00000000U
#define CAN_BTR_TS1_Pos             16U
#define CAN_BTR_TS2_Pos             20U
#define CAN_BS1_13TQ                (12U << CAN_BTR_TS1_Pos)
#define CAN_BS2_2TQ                 (1U << CAN_BTR_TS2_Pos)
#define CAN_IT_RX_FIFO0_MSG_PENDING 0x00000002U
#define CAN_TX_MAILBOX0             0x00000001U
#define CAN_TX_MAILBOX1             0x00000002U
#define CAN_TX_MAILBOX2             0x00000004U
#define CAN_ESR_EWGF                0x00000001U
#define CAN_ESR_EPVF                0x00000002U
#define CAN_ESR_BOFF                0x00000004U
#define CAN_ESR_LEC_Pos             4U
#define CAN_ESR_LEC                 (0x7U << CAN_ESR_LEC_Pos)
#define CAN_ESR_TEC_Pos             16U
#define CAN_ESR_TEC                 (0xFFU << CAN_ESR_TEC_Pos)
#define CAN_ESR_REC_Pos             24U
#define CAN_ESR_REC                 (0xFFU << CAN_ESR_REC_Pos)

HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *hcan, const CAN_FilterTypeDef *filter);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t interrupts);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, const CAN_TxHeaderTypeDef *header, const uint8_t data[], uint32_t *mailbox);
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *hcan, uint32_t mailboxes);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *hcan);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *hcan, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t data[]);
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan);

/* ================== UART ================== */

typedef enum {
	HAL_UART_STATE_RESET = 0x00U,
	HAL_UART_STATE_READY = 0x20U,
	HAL_UART_STATE_BUSY_TX = 0x21U,
	HAL_UART_STATE_BUSY_RX = 0x22U
} HAL_UART_StateTypeDef;

typedef struct {
	int id;                              // 1 = USART1, 2 = USART2
	volatile HAL_UART_StateTypeDef gState;
	volatile HAL_UART_StateTypeDef RxState;
} UART_HandleTypeDef;

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

/* ================== I2C (EEPROM) ================== */

typedef struct { int id; } I2C_HandleTypeDef;
#define I2C_MEMADD_SIZE_8BIT 0x00000001U

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t device, uint16_t address, uint16_t address_size, uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t device, uint16_t address, uint16_t address_size, uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t device, uint32_t trials, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *hi2c);

/* ================== ADC, TIM, SPI, WWDG ================== */

typedef struct { int id; } ADC_TypeDef;
extern ADC_TypeDef fake_adc1_regs, fake_adc2_regs;
#define ADC1 (&fake_adc1_regs)
typedef struct { ADC_TypeDef *Instance; } ADC_HandleTypeDef;

typedef struct { volatile uint32_t CR1, EGR, CNT, PSC, ARR, CCR1, SR; } TIM_TypeDef;
typedef struct { uint32_t Prescaler, CounterMode, Period, ClockDivision, AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
#define TIM_CHANNEL_1 0x00000000U
#define TIM_EGR_UG    0x00000001U
#define __HAL_TIM_SET_PRESCALER(h, v)     ((h)->Instance->PSC = (v))
#define __HAL_TIM_SET_AUTORELOAD(h, v)    do { (h)->Instance->ARR = (v); (h)->Init.Period = (v); } while (0)
#define __HAL_TIM_GET_AUTORELOAD(h)       ((h)->Instance->ARR)
#define __HAL_TIM_SET_COMPARE(h, ch, v)   ((h)->Instance->CCR1 = (v))

typedef struct { int unused; } SPI_HandleTypeDef;
typedef struct { int unused; } WWDG_HandleTypeDef;

HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint32_t *data, uint32_t length);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim, uint32_t channel);

/* ================== FLASH, IWDG, sistema ================== */

#define FLASH_TYPEPROGRAM_WORD 0x00000002U
#define FLASH_FLAG_EOP    0x00000001U
#define FLASH_FLAG_OPERR  0x00000002U
#define FLASH_FLAG_WRPERR 0x00000010U
#define FLASH_FLAG_PGAERR 0x00000020U
#define FLASH_FLAG_PGPERR 0x00000040U
#define FLASH_FLAG_PGSERR 0x00000080U

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t data);
void fake_flash_cache_event(char event);    // 'd' disable, 'r' reset, 'e' enable
void fake_flash_clear_flags(uint32_t flags);
#define __HAL_FLASH_CLEAR_FLAG(flags)       fake_flash_clear_flags(flags)
#define __HAL_FLASH_DATA_CACHE_DISABLE()    fake_flash_cache_event('d')
#define __HAL_FLASH_DATA_CACHE_RESET()      fake_flash_cache_event('r')
#define __HAL_FLASH_DATA_CACHE_ENABLE()     fake_flash_cache_event('e')

typedef struct { volatile uint32_t KR; } IWDG_TypeDef;
extern IWDG_TypeDef fake_iwdg_regs;
#define IWDG (&fake_iwdg_regs)

uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
void NVIC_SystemReset(void);
void __DMB(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t primask);

#endif /* TEST_FAKE_STM32F4XX_HAL_H */
