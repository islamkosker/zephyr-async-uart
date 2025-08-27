#ifdef  CONFIG_BOARD_NUCLEO_F070RB

#include <zephyr/init.h>
#include <stm32f0xx.h>  
static int dma_remap_fix(void)
{

    /* 1) SYSCFG clock ON (F0: APB2) */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;

    /* 2) USART1 DMA remap:
          TX: Ch2 -> Ch4
          RX: Ch3 -> Ch5
    */
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_USART1TX_DMA_RMP |
                     SYSCFG_CFGR1_USART1RX_DMA_RMP;

    return 0;
}
SYS_INIT(dma_remap_fix, PRE_KERNEL_1, 0);

#endif