.syntax unified
.cpu cortex-m0
.thumb

/*
 * The STM32 startup vectors weakly alias these names to Default_Handler.
 * With GCC 7 LTO, references from that startup assembly do not by themselves
 * retain same-named C definitions.  These non-LTO assembly shims provide
 * strong vector symbols and force the corresponding C implementations to be
 * retained by the linker.
 */
.macro INTERRUPT_VECTOR_SHIM vector, implementation
    .section .text.\vector,"ax",%progbits
    .global \vector
    .type \vector, %function
    .thumb_func
\vector:
    ldr r3, 1f
    bx r3
    .align 2
1:
    .word \implementation
    .size \vector, .-\vector
.endm

INTERRUPT_VECTOR_SHIM ADC1_IRQHandler, ADC1_IRQHandler_Impl
INTERRUPT_VECTOR_SHIM EXTI4_15_IRQHandler, EXTI4_15_IRQHandler_Impl
INTERRUPT_VECTOR_SHIM TIM17_IRQHandler, TIM17_IRQHandler_Impl
INTERRUPT_VECTOR_SHIM SysTick_Handler, SysTick_Handler_Impl
INTERRUPT_VECTOR_SHIM NMI_Handler, NMI_Handler_Impl
