/*
 * File: rtos_setup.h
 * Owner: Member 3
 */
#ifndef RTOS_SETUP_H
#define RTOS_SETUP_H

void Rtos_CreateObjects(void);   /* queues + shared state mutex            */
void Rtos_CreateTasks(void);     /* registers every task entry point        */
void Rtos_Start(void);           /* init everything and start the scheduler (never returns) */

#endif /* RTOS_SETUP_H */