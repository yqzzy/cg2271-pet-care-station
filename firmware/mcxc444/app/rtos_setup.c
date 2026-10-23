/*
 * File: rtos_setup.c
 * Owner: Member 3
 * Status: Written for review; NOT yet built or tested on hardware.
 * Responsibility: Create RTOS objects and tasks, then start the scheduler.
 *
 * Each owner writes and tests their OWN task entry point. This file only registers them.
 * To add a task: declare its entry point below, uncomment its xTaskCreate line in
 * Rtos_CreateTasks(), and agree the priority with Member 3.
 *
 * Entry point from main(): after the board-generated hardware init, call Rtos_Start().
 * FreeRTOSConfig.h (docs/system-design.md): enable pre-emption AND time-slicing
 * (configUSE_PREEMPTION = 1, configUSE_TIME_SLICING = 1), configUSE_MUTEXES = 1,
 * configSUPPORT_DYNAMIC_ALLOCATION = 1, enough configTOTAL_HEAP_SIZE for the stacks and
 * queues below, and disable unused features. Software timers are NOT needed.
 * Tasks must use bounded waits and periodic delays, never spin.
 */
#include <stddef.h>
#include "rtos_setup.h"
#include "rtos_types.h"
#include "shared_state.h"
#include "../control/button.h"
#include "../actuators/feedback.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ---- Task priorities (PROPOSED, higher number = higher priority) ----
 * Design rule: actuator deadlines and local button response outrank bulk telemetry.
 * TODO: validate exact priorities and periods on hardware. */
#define PRIO_BUTTON     3   /* short-lived, must react quickly            */
#define PRIO_ACTUATOR   3   /* Member 1: motor deadlines                  */
#define PRIO_UART       2   /* Member 4                                   */
#define PRIO_SENSING    1   /* Member 2: slow sampling                    */

#define STACK_WORDS     (configMINIMAL_STACK_SIZE + 100)

/* ---- Task entry points owned by other members (names from docs/system-design.md) ----
 * void Actuator_Task(void *argument);    Member 1 (control/actuator_task.c)
 * void FoodSensor_Task(void *argument);  Member 2 (sensing/food_sensor.c)
 * void WaterSensor_Task(void *argument); Member 2 (sensing/water_sensor.c)
 * void UartLink_Task(void *argument);    Member 4 (communication/uart_link.c)
 * Declare each (or include its header) when the owner's code lands.
 * Actuator_Task must call Feedback_Update(Rtos_NowMs()) whenever it wakes.
 */

QueueHandle_t buttonQueue;
QueueHandle_t actuatorQueue;
QueueHandle_t telemetryQueue;

void Rtos_CreateObjects(void) {
    SharedState_Init();   /* creates the state mutex */

    buttonQueue    = xQueueCreate(BUTTON_QUEUE_LEN,    sizeof(ButtonEvent));
    actuatorQueue  = xQueueCreate(ACTUATOR_QUEUE_LEN,  sizeof(ActuatorCmd));
    telemetryQueue = xQueueCreate(TELEMETRY_QUEUE_LEN, sizeof(TelemetryMsg));

    configASSERT(buttonQueue != NULL);
    configASSERT(actuatorQueue != NULL);
    configASSERT(telemetryQueue != NULL);
}

void Rtos_CreateTasks(void) {
    BaseType_t ok;

    ok = xTaskCreate(Button_Task, "button", STACK_WORDS, NULL, PRIO_BUTTON, NULL);
    configASSERT(ok == pdPASS);

    /* Uncomment each line as its owner's task is pushed and builds:
     *
     * ok = xTaskCreate(Actuator_Task,     "actuator", STACK_WORDS, NULL, PRIO_ACTUATOR, NULL);
     * configASSERT(ok == pdPASS);
     * ok = xTaskCreate(FoodSensor_Task,   "food",     STACK_WORDS, NULL, PRIO_SENSING,  NULL);
     * configASSERT(ok == pdPASS);
     * ok = xTaskCreate(WaterSensor_Task,  "water",    STACK_WORDS, NULL, PRIO_SENSING,  NULL);
     * configASSERT(ok == pdPASS);
     * ok = xTaskCreate(UartLink_Task,     "uart",     STACK_WORDS, NULL, PRIO_UART,     NULL);
     * configASSERT(ok == pdPASS);
     */
    (void)ok;
}

void Rtos_Start(void) {
    Rtos_CreateObjects();   /* queues first: the button ISR uses buttonQueue         */
    Feedback_Init();        /* buzzer + light                                         */
    Button_Init();          /* enables the button interrupt, so after the queues      */
    Rtos_CreateTasks();

    vTaskStartScheduler();  /* never returns unless there is not enough heap          */
    for (;;) { }
}