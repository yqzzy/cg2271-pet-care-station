/*
 * File: uart_link.h
 * Provisional owner: Member 4 (first version written by Member 3 as a stand-in; Member 4 to review)
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: Application adapter between the RTOS app and Member 4's UART module
 *                 (mcxc_uart.h / mcxc_uart.c). It does not parse packets or retry: the UART
 *                 module already does that.
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>

/* Call once after board/debug init and BEFORE the scheduler starts. Wraps uartInit().
 * Returns false on failure: the caller must stop startup. */
bool UartLink_Init(void);

/* FreeRTOS task: forwards debounced button presses (telemetryQueue) with sendButton(), and
 * reports the latest food/water snapshot with sendSensors() once a second. */
void UartLink_Task(void *argument);

#endif /* UART_LINK_H */