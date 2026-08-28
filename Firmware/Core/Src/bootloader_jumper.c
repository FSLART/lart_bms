/*
 * can_test.c
 *
 *  Created on: Mar 31, 2026
 *      Author: jpser
 */

/*
 * can_test.c
 */

#include <string.h>
#include "main.h"
#include "can.h"
#include "bootloader_jumper.h"
#include "fault_manager.h"
#include "powertrain_t26.h"
#include "uartDMA.h"

extern CAN_HandleTypeDef hcan2;

/* ------------------------------------------------
   CAN MESSAGE TO JUMP TO BOOTLOADER
   ------------------------------------------------ */
#define BL_CAN_CMD_ID         0x0DC
#define BL_CAN_CMD_DATA       0x01

/* ------------------------------------------------
   Bootloader
   ------------------------------------------------ */
#define BOOTLOADER_BASE_ADDR  0x1FFF0000U

typedef void (*pFunction)(void);

/* Flag set no IRQ, consumida no main loop */
volatile uint8_t triggerJumpToBootloader = 0;

void JumpToBootloader(void)
{
    uint32_t  blStack = *(__IO uint32_t*) BOOTLOADER_BASE_ADDR;
    uint32_t  blEntry = *(__IO uint32_t*)(BOOTLOADER_BASE_ADDR + 4U);
    pFunction jump    = (pFunction) blEntry;

    /* 1. DeInit HAL primeiro — precisam do SysTick ainda ativo */
    HAL_CAN_DeactivateNotification(&hcan2,
        CAN_IT_RX_FIFO0_MSG_PENDING |
        CAN_IT_RX_FIFO1_MSG_PENDING |
        CAN_IT_TX_MAILBOX_EMPTY     |
        CAN_IT_ERROR | CAN_IT_BUSOFF |
        CAN_IT_LAST_ERROR_CODE      |
        CAN_IT_ERROR_WARNING        |
        CAN_IT_ERROR_PASSIVE);
    HAL_CAN_Stop(&hcan2);
    HAL_CAN_DeInit(&hcan2);
    HAL_DeInit();
    HAL_RCC_DeInit();

    /* 2. Agora é seguro desativar SysTick */
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 3. Limpar o NVIC */
    for (uint32_t i = 0; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    /* 4. Habilitar SYSCFG clock + remap system flash para 0x00000000 */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

    /* 5. VTOR → aponta para o vector table do bootloader */
    SCB->VTOR = 0x00000000U;

    /* 6. Garantir Thread Mode + MSP */
    __set_CONTROL(0x00U);
    __ISB();

    /* 7. Saltar */
    __set_MSP(blStack);
    __DSB();
    __ISB();

    jump();

    while (1) {}
}

void Programmer_CAN_RX(CAN_RxHeaderTypeDef *hdr, uint8_t *data) {

	//chmama 1 x
	static struct powertrain_t26_start_programmer_t last_programmer_msg;

	if ((hdr == 0) || (data == 0)) {
		return;
	}

	// Status from the can programmer
	if ((hdr->IDE == CAN_ID_STD) && (hdr->StdId == POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID)) {

		//returns 0 if succsess
	    if (powertrain_t26_start_programmer_unpack(&last_programmer_msg, data, hdr->DLC) == 0) {
	    	triggerJumpToBootloader = 1;
	    }
	}
}

void Setup_Bootloader_Jumper(void)
{
    if (CAN2_RegisterRxCallback(Programmer_CAN_RX) == HAL_OK) {
        //printfDebug("Bootloader CAN2 callback registered\r\n");
    } else {
        //printfDebug("Bootloader CAN2 callback register FAILED\r\n");
    }
}

void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rxHeader;
    uint8_t rxData[8] = {0};

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO1, &rxHeader, rxData) != HAL_OK)
        return;

    if (hcan->Instance      != CAN2)            return;
    if (rxHeader.IDE        != CAN_ID_STD)      return;
    if (rxHeader.StdId      != BL_CAN_CMD_ID)   return;
    if ((rxData[0] & 0x01U) != BL_CAN_CMD_DATA) return;

    /* O jup acontece no main loop (Thread Mode) */
    triggerJumpToBootloader = 1;
}
