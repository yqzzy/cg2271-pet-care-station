/* MCXC UART walkthrough:
 * 1. Public send functions format a packet and queue a COPY.
 * 2. sendTask waits on txQueue, sends a packet, then checks the retry timer.
 * 3. receiveTask parses packets and records ACK/pending state under stateMutex.
 * 4. Public consume functions copy/clear flags; application performs the action.
 * 5. A retry copy is separate from the temporary/current TX packet.
 */
/* UART2 driver adapted from your template: PTE22 TX, PTE23 RX, 9600 8N1. */
#include "mcxc_uart.h"
#include "board.h"
#include "clock_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "fsl_debug_console.h"

/* Set MCXC_UART_DEBUG=0 in build options (or below) to silence UART diagnostics.
 * Logs use NXP debug console, NEVER UART2.
 * No printing occurs in the UART ISR or while the state mutex is held. */
#ifndef MCXC_UART_DEBUG
#define MCXC_UART_DEBUG 1
#endif
#if MCXC_UART_DEBUG
#define MCXC_LOG(...) PRINTF("[MCXC UART] " __VA_ARGS__)
#else
#define MCXC_LOG(...) ((void)0)
#endif


#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Change these on BOTH boards if needed. Three attempts = initial + 2 retries. */
#define UART_MESSAGE_SIZE 64
#define UART_QUEUE_LENGTH 8
#define UART_RETRY_MS 200u
#define UART_MAX_ATTEMPTS 3u
#define UART_TX_PTE22 22
#define UART_RX_PTE23 23
#define UART2_INT_PRIO 2

typedef struct {
    char text[UART_MESSAGE_SIZE];
} UartMessage;

/* XOR checksum: combine the ASCII bytes of the message body.
 * ^ is bitwise XOR; ^= means "XOR into the current value".
 * Exclude the newline and the *HH checksum suffix from this calculation. */
static uint8_t calculateChecksum(const char *text, size_t length)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) sum ^= (uint8_t)text[i];
    return sum;
}

/* TX queue stores packet COPIES; RX queue stores complete received lines. */
static QueueHandle_t txQueue, rxQueue;
/* Protects pending flags, saved retry packet and application-visible results. */
static SemaphoreHandle_t stateMutex;
static TaskHandle_t txTaskHandle;
static bool initialised;

/* Only TX task writes send_buffer; ISR reads until it notifies completion. */
static char send_buffer[UART_MESSAGE_SIZE];
static volatile size_t send_position, send_length;
volatile uint32_t uartRxDropped; /* debugger diagnostics */

/* Protected by stateMutex. */
static uint32_t nextButtonSequence = 1;
/* Saved request stays intact while unrelated packets use the TX work buffer.
 * A new button request is refused while buttonWaiting is true. */
static UartMessage buttonPacket;
static UartResult buttonResult;
/* 0 = queued but not yet sent by TX; 1 = initial send, 2..3 = retries. */
static unsigned buttonAttempts;
/* Only TX advances this timestamp. Other traffic must never reset it. */
static uint32_t buttonLastSent;
static bool buttonWaiting;
/* One pending flag; the other two values are command DATA, not extra states.
 * lastLightSequence stays after consumption so a retry gets ACKed, not re-applied.
 * Sequence 0 means no light request has been received yet. */
static bool lightPending;
static bool desiredLightOn;
static uint32_t lastLightSequence;

static uint32_t nowMs(void)
{
    return (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
}


/* Same pattern as your demo: format a string, then queue a COPY of it. */
bool sendSensors(uint16_t water, int32_t food)
{
    UartMessage message = {0};
    snprintf(message.text, sizeof(message.text), "SENSOR:%u,%ld\n", (unsigned)water, (long)food);
    return xQueueSend(txQueue, &message, 0) == pdPASS;
}

bool sendButton(void)
{
    bool accepted = false;
    uint32_t queuedSequence = 0;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (!buttonWaiting && nextButtonSequence <= 999999999u) {
        uint32_t sequence = nextButtonSequence++;
        // Keep this saved string intact until ACK/timeout; queue receives a copy.
        snprintf(buttonPacket.text, sizeof(buttonPacket.text), "BUTTON:%lu\n", (unsigned long)sequence);
        if (xQueueSend(txQueue, &buttonPacket, 0) == pdPASS) {
            buttonResult.sequence = sequence;
            buttonResult.status = UART_WAITING;
            buttonWaiting = true;
            buttonAttempts = 0; /* TX starts timer after sending the initial packet. */
            buttonLastSent = 0;
            accepted = true;
            queuedSequence = sequence;
        }
    }
    xSemaphoreGive(stateMutex);
    if (accepted) MCXC_LOG("QUEUE BUTTON seq=%lu\n", (unsigned long)queuedSequence);
    else MCXC_LOG("REJECT BUTTON: previous request pending, queue full or sequence exhausted\n");
    (void)queuedSequence; /* also used when debug is compiled out */
    return accepted;
}

UartResult getButtonResult(void)
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    UartResult result = buttonResult;
    xSemaphoreGive(stateMutex);
    return result;
}

/* Caller holds stateMutex. The ACK means "control task consumed this request".
 * It does NOT claim the GPIO was written or the physical LED illuminated. */
static void sendLightAck(void)
{
    UartMessage reply = {0};
    snprintf(reply.text, sizeof(reply.text), "LIGHT_ACK:%lu\n", (unsigned long)lastLightSequence);
    (void)xQueueSend(txQueue, &reply, 0);
    // If queue is full or ACK lost, a duplicate LIGHT regenerates this ACK.
}

bool consumeLight(bool *on)
{
    if (!on) return false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    bool available = lightPending;
    uint32_t sequence = lastLightSequence; // Local snapshot only for debug log.
    if (available) {
        *on = desiredLightOn;       // Copy desired setting into caller's bool.
        lightPending = false;      // Clear flag: consume only once.
        sendLightAck();            // ONE ACK, sent on consumption.
    }
    xSemaphoreGive(stateMutex);
    if (available) MCXC_LOG("CONSUME LIGHT seq=%lu on=%u; ACK requested\n",
                           (unsigned long)sequence, (unsigned)*on);
    (void)sequence;
    return available;
}

/* Runs in RX TASK context, never ISR context. It only parses/records state;
 * motor/light/cloud work belongs to the application after consuming a flag. */
static void handleReceived(UartMessage *message)
{
    unsigned sequence, on;
    int end = 0;  // sscanf's %n stores where it stopped, to reject trailing junk.
    const char *note = "Ignored: unknown/malformed message or old/busy request";
    MCXC_LOG("RX %s", message->text);

    /* Wire example: LIGHT:1,1*HH\n. Find * then read EXACTLY two hex digits.
     * %n records the number of suffix characters scanned (must be 3: *HH).
     * Check checksum BEFORE setting any flag or accepting any ACK. */
    char *star = strchr(message->text, '*');
    unsigned receivedChecksum;
    int suffixLength = 0;
    if (!star || strlen(star) != 4 || star[3] != '\n' ||
        !isxdigit((unsigned char)star[1]) || !isxdigit((unsigned char)star[2]) ||
        sscanf(star, "*%2x%n", &receivedChecksum, &suffixLength) != 1 || suffixLength != 3 ||
        calculateChecksum(message->text, (size_t)(star - message->text)) != receivedChecksum) {
        MCXC_LOG("DROP: missing, malformed or incorrect checksum\n");
        return; // No ACK and no action. Reliable sender can retry normally.
    }
    /* Replace *HH\n with just \n, restoring the plain string parser expects.
     * This changes only the local RECEIVED copy, never a saved TX retry packet. */
    star[0] = '\n';
    star[1] = '\0';
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    // %9u reads at most 9 decimal digits, so IDs fit a 32-bit unsigned integer.
    if (sscanf(message->text, "BUTTON_ACK:%9u%n", &sequence, &end) == 1 &&
        message->text[end] == '\n' && sequence != 0 && sequence <= 999999999u) {
        if (buttonWaiting && sequence == buttonResult.sequence) {
            buttonWaiting = false;  // TX now stops retrying this button.
            buttonResult.status = UART_CONSUMED;
            note = "Button ACK matched; retries stopped";
        }
    } else if (sscanf(message->text, "LIGHT:%9u,%1u%n", &sequence, &on, &end) == 2 &&
               message->text[end] == '\n' && sequence != 0 && sequence <= 999999999u && on <= 1) {
        if (sequence == lastLightSequence) {
            // Already consumed? Re-send ACK only; never set pending again.
            if (on == (unsigned)desiredLightOn && !lightPending) sendLightAck();
            note = "Duplicate light request; no second consumption";
        } else if (!lightPending && sequence > lastLightSequence) {
            lastLightSequence = sequence;
            desiredLightOn = on != 0;
            lightPending = true;
            note = "New light command pending for consumeLight()";
        }
    }
    xSemaphoreGive(stateMutex);
    MCXC_LOG("%s\n", note);
    (void)note;
}


/* ISR keeps byte-handling short: RX collects until LF and queues a full line;
 * TX feeds one byte when hardware is ready. NO printf/parsing/actuator work here. */
void UART2_FLEXIO_IRQHandler(void)
{
    static UartMessage incoming;
    static size_t received = 0;
    static bool discardLine = false;
    BaseType_t wake = pdFALSE;
    uint8_t status = UART2->S1;

    if (status & (UART_S1_RDRF_MASK | UART_S1_OR_MASK | UART_S1_NF_MASK | UART_S1_FE_MASK | UART_S1_PF_MASK)) {
        char byte = (char)UART2->D;
        if (status & (UART_S1_OR_MASK | UART_S1_NF_MASK | UART_S1_FE_MASK | UART_S1_PF_MASK)) {
            discardLine = true; // Ignore damaged/oversized line until its newline.
            uartRxDropped++;
        }
        if (!discardLine) {
            if (received < sizeof(incoming.text) - 1) incoming.text[received++] = byte;
            else { discardLine = true; uartRxDropped++; }
        }
        if (byte == '\n') {
            if (!discardLine) {
                incoming.text[received] = '\0'; // End C string; newline stays in string.
                if (xQueueSendFromISR(rxQueue, &incoming, &wake) != pdPASS) uartRxDropped++;
            }
            received = 0;
            discardLine = false;
        }
    }
    if ((UART2->C2 & UART_C2_TIE_MASK) && (UART2->S1 & UART_S1_TDRE_MASK)) {
        if (send_position < send_length) UART2->D = (uint8_t)send_buffer[send_position++];
        else {
            UART2->C2 &= ~UART_C2_TE_MASK;
            UART2->C2 &= ~UART_C2_TIE_MASK;
            // Saved send_buffer is no longer in use; wake TX so it can send next item.
            // Hardware lets the final character finish after TE is cleared.
            vTaskNotifyGiveFromISR(txTaskHandle, &wake);
        }
    }
    portYIELD_FROM_ISR(wake);
}

/* Low-level sender: used for every queued packet and retry. Only TX task calls
 * it. This is the equivalent of your original sendMessage, with safe buffering. */
static void sendPacket(const UartMessage *message)
{
    /* Queue/saved retry strings stay plain. Add checksum only to this TX copy.
     * strlen excludes terminating \0; length-1 excludes the existing newline. */
    char wire[UART_MESSAGE_SIZE] = {0};
    size_t length = strlen(message->text);
    if (length == 0 || message->text[length - 1] != '\n' || length + 3 >= sizeof(wire)) {
        MCXC_LOG("DROP TX: malformed/oversized message\n");
        return;
    }
    memcpy(wire, message->text, length - 1);
    uint8_t sum = calculateChecksum(wire, length - 1);
    // %02X prints checksum as two hex digits; final newline still ends the frame.
    snprintf(wire + length - 1, sizeof(wire) - (length - 1), "*%02X\n", (unsigned)sum);
    MCXC_LOG("TX %s", wire);
    memcpy(send_buffer, wire, sizeof(send_buffer));
    send_length = strlen(send_buffer);
    send_position = 0;
    UART2->C2 |= UART_C2_TE_MASK;
    UART2->C2 |= UART_C2_TIE_MASK;
    // Wait for ISR to finish reading send_buffer, NOT for the remote ACK.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

/* 
 * 1. Wait for a queued message, for at most 20 ms.
 * 2. If a message arrived, send it.
 * 3. Check whether the saved reliable packet needs a retry or timeout.
 * A queue item wakes us immediately; 20 ms is just the idle retry-check interval. */
static void sendTask(void *unused)
{
    (void)unused;
    UartMessage message;
    TickType_t queueWait = pdMS_TO_TICKS(20);
    if (!queueWait) queueWait = 1;

    for (;;) {
        /* Queue COPIES the item into message. ACKs/sensors can pass through
         * while a separate buttonPacket remains saved for possible retries. */
        if (xQueueReceive(txQueue, &message, queueWait) == pdPASS) {
            sendPacket(&message);

            /* Start timer after original packet is handed to UART.
             * Unrelated queued messages must not reset this timer. */
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            if (buttonWaiting && buttonAttempts == 0 &&
                strcmp(message.text, buttonPacket.text) == 0) {
                buttonAttempts = 1;
                buttonLastSent = nowMs();
            }
            xSemaphoreGive(stateMutex);
        }

        bool retry = false;
        bool timedOut = false;
        uint32_t sequence = 0;
        unsigned retryNumber = 0;

        /* RX may update ACK state concurrently, so check/copy under mutex.
         * Never hold it while printing or physically sending a packet. */
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        uint32_t now = nowMs();
        if (buttonWaiting && buttonAttempts != 0 &&
            (uint32_t)(now - buttonLastSent) >= UART_RETRY_MS) {
            sequence = buttonResult.sequence;
            if (buttonAttempts >= UART_MAX_ATTEMPTS) {
                buttonWaiting = false;
                buttonResult.status = UART_TIMED_OUT;
                timedOut = true;
            } else {
                message = buttonPacket; /* COPY saved request; keep saved original. */
                buttonAttempts++;
                retryNumber = buttonAttempts - 1;
                buttonLastSent = now;
                retry = true;
            }
        }
        xSemaphoreGive(stateMutex);

        if (retry) {
            MCXC_LOG("RETRY BUTTON seq=%lu retry=%u/2\n", (unsigned long)sequence, retryNumber);
            sendPacket(&message);
        } else if (timedOut) {
            MCXC_LOG("TIMEOUT BUTTON seq=%lu after initial + 2 retries\n", (unsigned long)sequence);
        }
        (void)sequence; (void)retryNumber; /* no unused warnings with debug off */
    }
}

/* Reconstruct/receive complete lines and dispatch them. This task does NOT
 * manage retry timers; only TX does. ACKs change shared state under the mutex. */
static void receiveTask(void *unused)
{
    (void)unused;
    UartMessage message;
    UART2->C2 |= UART_C2_RIE_MASK;
    NVIC_EnableIRQ(UART2_FLEXIO_IRQn);
    TickType_t wait = pdMS_TO_TICKS(20);
    if (!wait) wait = 1;
    for (;;) {
        if (xQueueReceive(rxQueue, &message, wait) == pdPASS) handleReceived(&message);
    }
}

static void initUART2(void)
{
    const uint32_t baud_rate = 9600;

    NVIC_DisableIRQ(UART2_FLEXIO_IRQn);

    // Enable clocks to UART2 and PORTE.
    SIM->SCGC4 |= SIM_SCGC4_UART2_MASK;
    SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;

    // Disable TX, RX and their interrupts before configuration.
    UART2->C2 &= ~(UART_C2_TE_MASK |
                   UART_C2_RE_MASK |
                   UART_C2_TIE_MASK |
                   UART_C2_RIE_MASK);

    // Connect UART TX to PTE22.
    PORTE->PCR[UART_TX_PTE22] &= ~PORT_PCR_MUX_MASK;
    PORTE->PCR[UART_TX_PTE22] |= PORT_PCR_MUX(4);

    // Connect UART RX to PTE23.
    PORTE->PCR[UART_RX_PTE23] &= ~PORT_PCR_MUX_MASK;
    PORTE->PCR[UART_RX_PTE23] |= PORT_PCR_MUX(4);

    // Calculate the baud-rate divisor using integer rounding.
    uint32_t bus_clk = CLOCK_GetBusClkFreq();

    uint32_t sbr =
        (bus_clk + (baud_rate * 8)) / (baud_rate * 16);

    // Set the upper divisor bits first, then the lower bits.
    UART2->BDH &= ~UART_BDH_SBR_MASK;
    UART2->BDH |= ((sbr >> 8) & UART_BDH_SBR_MASK);
    UART2->BDL = (uint8_t)(sbr & 0xFF);

    // Disable loop mode.
    UART2->C1 &= ~UART_C1_LOOPS_MASK;
    UART2->C1 &= ~UART_C1_RSRC_MASK;

    // Disable parity.
    UART2->C1 &= ~UART_C1_PE_MASK;

    // Use 8-bit data.
    UART2->C1 &= ~UART_C1_M_MASK;

    // Enable receiving.
    // TX remains disabled until sendPacket() starts a packet.
    UART2->C2 |= UART_C2_RE_MASK;

    // Use the project's configured interrupt priority.
    NVIC_SetPriority(UART2_FLEXIO_IRQn, UART2_INT_PRIO);
    NVIC_ClearPendingIRQ(UART2_FLEXIO_IRQn);

    // RX interrupts are enabled by receiveTask(), shown below.
}

bool uartInit(void)
{
    if (initialised) return false;
    initialised = true;
    txQueue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(UartMessage));
    rxQueue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(UartMessage));
    stateMutex = xSemaphoreCreateMutex();
    if (!txQueue || !rxQueue || !stateMutex) return false;
    initUART2();
    if (xTaskCreate(sendTask, "uart_tx", configMINIMAL_STACK_SIZE + 192, NULL, 2, &txTaskHandle) != pdPASS) return false;
    if (xTaskCreate(receiveTask, "uart_rx", configMINIMAL_STACK_SIZE + 256, NULL, 2, NULL) != pdPASS) return false;
    return true;
}
