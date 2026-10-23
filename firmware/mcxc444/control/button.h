/*
 * File: button.h
 * Owner: Member 3
 */
#ifndef BUTTON_H
#define BUTTON_H

void Button_Init(void);                 /* call AFTER the queues are created */
void Button_Task(void *argument);       /* FreeRTOS task entry point         */

#endif /* BUTTON_H */