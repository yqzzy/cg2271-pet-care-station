/* ESP32 UART walkthrough:
 * 1. Public send functions format a packet and queue a COPY.
 * 2. sendTask waits on txQueue, sends a packet, then checks the retry timer.
 * 3. receiveTask parses packets and records ACK/pending state under stateMutex.
 * 4. Public consume functions copy/clear flags; application performs the action.
 * 5. A retry copy is separate from the temporary/current TX packet.
 */
#include "esp32_uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

/* Set ESP_UART_DEBUG=0 in build options (or below) to silence UART diagnostics.
 * Logs use USB/debug Serial, NEVER the HardwareSerial passed to uartInit.
 * No printing occurs in the UART ISR or while the state mutex is held. */
#ifndef ESP_UART_DEBUG
#define ESP_UART_DEBUG 1
#endif
#if ESP_UART_DEBUG
#define ESP_LOG(...) Serial.printf("[ESP32 UART] " __VA_ARGS__)
#else
#define ESP_LOG(...) ((void)0)
#endif


#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Change these on BOTH boards if needed. Three attempts = initial + 2 retries. */
#define UART_MESSAGE_SIZE 64
#define UART_QUEUE_LENGTH 8
#define UART_RETRY_MS 200u
#define UART_MAX_ATTEMPTS 3u

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

static HardwareSerial *uart;
/* Queue stores complete packet COPIES, not pointers to local strings. */
static QueueHandle_t txQueue;
/* Protects pending flags, saved retry packet and application-visible results. */
static SemaphoreHandle_t stateMutex;
static bool initialised;
/* Protected by stateMutex. */
static SensorValues sensors;
static bool sensorsReceived;
/* One button flag; sequence 0 means no event yet. Cleared flag + same ID
 * means already consumed, so replay its ACK without another notification. */
static bool buttonPending;
static uint32_t buttonSequence;
static uint32_t nextLightSequence = 1;
/* Saved request stays intact while unrelated packets use the TX work buffer.
 * A new light request is refused while lightWaiting is true. */
static UartMessage lightPacket;
static UartResult lightResult;
static bool lightWaiting;
/* One unread ACK event. Requested value is kept until application consumes ACK. */
static bool lightAckPending;
static bool lightRequestedOn;
/* 0 = queued; 1 = initial send; 2..3 = retries. */
static unsigned lightAttempts;
/* Only TX advances this timestamp. Other traffic must never reset it. */
static uint32_t lightLastSent;


bool getSensors(SensorValues *out)
{
    if (!out) return false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    bool available = sensorsReceived;
    if (available) *out = sensors;
    xSemaphoreGive(stateMutex);
    return available;
}

/* Caller holds stateMutex. */
static void sendButtonAck(void)
{
    UartMessage reply = {0};
    snprintf(reply.text, sizeof(reply.text), "BUTTON_ACK:%lu\n", (unsigned long)buttonSequence);
    (void)xQueueSend(txQueue, &reply, 0);
}

bool consumeButton(uint32_t *sequence)
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    bool available = buttonPending;
    uint32_t consumedSequence = buttonSequence;
    if (available) {
        if (sequence) *sequence = buttonSequence;
        buttonPending = false;
        sendButtonAck();
    }
    xSemaphoreGive(stateMutex);
    if (available) ESP_LOG("CONSUME BUTTON seq=%lu; BUTTON_ACK requested\n", (unsigned long)consumedSequence);
    (void)consumedSequence;
    return available;
}

bool sendLight(bool on)
{
    bool accepted = false;
    uint32_t queuedSequence = 0;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    // Keep the unread ACK/value until application consumes it; don't overwrite it.
    if (!lightWaiting && !lightAckPending && nextLightSequence <= 999999999u) {
        uint32_t sequence = nextLightSequence++;
        // Keep saved request; TX queue receives a separate copy of the string.
        snprintf(lightPacket.text, sizeof(lightPacket.text), "LIGHT:%lu,%u\n", (unsigned long)sequence, (unsigned)on);
        if (xQueueSend(txQueue, &lightPacket, 0) == pdPASS) {
            lightResult.sequence = sequence;
            lightResult.status = UART_WAITING;
            lightRequestedOn = on;
            lightWaiting = true;
            lightAttempts = 0; /* TX starts timer after sending the initial packet. */
            lightLastSent = 0;
            accepted = true;
            queuedSequence = sequence;
        }
    }
    xSemaphoreGive(stateMutex);
    if (accepted) ESP_LOG("QUEUE LIGHT seq=%lu\n", (unsigned long)queuedSequence);
    else ESP_LOG("REJECT LIGHT: previous request/ACK pending, queue full or sequence exhausted\n");
    (void)queuedSequence; /* also used when debug is compiled out */
    return accepted;
}

/* true ONCE per matching ACK. This is how cloud code notices completion
 * without comparing old/new status values or sequence numbers itself. */
bool consumeLightAck(bool *on)
{
    if (!on) return false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    bool available = lightAckPending;
    if (available) {
        *on = lightRequestedOn;
        lightAckPending = false;
    }
    xSemaphoreGive(stateMutex);
    if (available) ESP_LOG("CONSUME LIGHT ACK on=%u\n", (unsigned)*on);
    return available;
}

UartResult getLightResult(void)
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    UartResult result = lightResult;
    xSemaphoreGive(stateMutex);
    return result;
}

/* Runs in RX TASK context, never ISR context. It only parses/records state;
 * motor/light/cloud work belongs to the application after consuming a flag. */
static void handleReceived(UartMessage *message)
{
    unsigned sequence, water;
    long long food; // Wide temporary for parsing; range-check before storing int32_t.
    int end = 0;
    const char *note = "Ignored: unknown/malformed message or unmatched ACK";
    ESP_LOG("RX %s", message->text);

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
        ESP_LOG("DROP: missing, malformed or incorrect checksum\n");
        return; // No ACK and no action. Reliable sender can retry normally.
    }
    /* Replace *HH\n with just \n, restoring the plain string parser expects.
     * This changes only the local RECEIVED copy, never a saved TX retry packet. */
    star[0] = '\n';
    star[1] = '\0';
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    // %n records end position; the next character must be newline, not extra data.
    if (sscanf(message->text, "SENSOR:%5u,%11lld%n", &water, &food, &end) == 2 &&
        message->text[end] == '\n' && water <= UINT16_MAX && food >= INT32_MIN && food <= INT32_MAX) {
        sensors.water_level = (uint16_t)water;
        sensors.food_milligrams = (int32_t)food;
        sensors.received_ms = millis();
        sensorsReceived = true;
        note = "Sensor readings updated; no ACK needed";
    } else if (sscanf(message->text, "BUTTON:%9u%n", &sequence, &end) == 1 &&
               message->text[end] == '\n' && sequence != 0 && sequence <= 999999999u) {
        if (sequence == buttonSequence) {
            if (!buttonPending) sendButtonAck(); // Replay lost ACK, not notification.
            note = "Duplicate button; no second notification";
        } else if (!buttonPending && sequence > buttonSequence) {
            buttonSequence = sequence;
            buttonPending = true;
            note = "Button pending for consumeButton()";
        }
    } else if (sscanf(message->text, "LIGHT_ACK:%9u%n", &sequence, &end) == 1 &&
               message->text[end] == '\n' && lightWaiting && sequence == lightResult.sequence) {
        lightWaiting = false;                // Stop retries immediately on valid ACK.
        lightResult.status = UART_CONSUMED;   // No GPIO/physical illumination claim.
        lightAckPending = true;               // Expose completion ONCE to cloud code.
        note = "Light consumption ACK matched; retries stopped";
        // Repeated ACKs are ignored once lightWaiting is false.

    }
    xSemaphoreGive(stateMutex);
    ESP_LOG("%s\n", note);
    (void)note;
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
        ESP_LOG("DROP TX: malformed/oversized message\n");
        return;
    }
    memcpy(wire, message->text, length - 1);
    uint8_t sum = calculateChecksum(wire, length - 1);
    // %02X prints checksum as two hex digits; final newline still ends the frame.
    snprintf(wire + length - 1, sizeof(wire) - (length - 1), "*%02X\n", (unsigned)sum);
    ESP_LOG("TX %s", wire);
    size_t expected = strlen(wire);
    size_t written = uart->write((const uint8_t *)wire, expected);
    if (written != expected) ESP_LOG("TX short write: %u/%u bytes\n", (unsigned)written, (unsigned)expected);
    (void)written;
}

/* The full sending flow is here. No remote-ACK wait and no deadline helper.
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
         * while a separate lightPacket remains saved for possible retries. */
        if (xQueueReceive(txQueue, &message, queueWait) == pdPASS) {
            sendPacket(&message);

            /* Start timer after original packet is handed to UART.
             * Unrelated queued messages must not reset this timer. */
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            if (lightWaiting && lightAttempts == 0 &&
                strcmp(message.text, lightPacket.text) == 0) {
                lightAttempts = 1;
                lightLastSent = millis();
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
        uint32_t now = millis();
        if (lightWaiting && lightAttempts != 0 &&
            (uint32_t)(now - lightLastSent) >= UART_RETRY_MS) {
            sequence = lightResult.sequence;
            if (lightAttempts >= UART_MAX_ATTEMPTS) {
                lightWaiting = false;
                lightResult.status = UART_TIMED_OUT;
                timedOut = true;
            } else {
                message = lightPacket; /* COPY saved request; keep saved original. */
                lightAttempts++;
                retryNumber = lightAttempts - 1;
                lightLastSent = now;
                retry = true;
            }
        }
        xSemaphoreGive(stateMutex);

        if (retry) {
            ESP_LOG("RETRY LIGHT seq=%lu retry=%u/2\n", (unsigned long)sequence, retryNumber);
            sendPacket(&message);
        } else if (timedOut) {
            ESP_LOG("TIMEOUT LIGHT seq=%lu after initial + 2 retries\n", (unsigned long)sequence);
        }
        (void)sequence; (void)retryNumber; /* no unused warnings with debug off */
    }
}

/* Reconstruct/receive complete lines and dispatch them. This task does NOT
 * manage retry timers; only TX does. ACKs change shared state under the mutex. */
static void receiveTask(void *unused)
{
    (void)unused;
    UartMessage incoming = {0};
    size_t received = 0;
    bool discardLine = false;
    for (;;) {
        // ESP32 driver already buffers incoming bytes; build one newline-ended string.
        for (unsigned count = 0; count < 128 && uart->available() > 0; ++count) {
            int value = uart->read();
            if (value < 0) break;
            char byte = (char)value;
            if (!discardLine) {
                if (received < sizeof(incoming.text) - 1) incoming.text[received++] = byte;
                else discardLine = true;
            }
            if (byte == '\n') {
                if (!discardLine) {
                    incoming.text[received] = '\0';
                    handleReceived(&incoming);
                } else ESP_LOG("Dropped oversized line\n");
                received = 0;
                discardLine = false;
            }
        }
        TickType_t wait = pdMS_TO_TICKS(5);
        vTaskDelay(wait ? wait : 1);
    }
}

bool uartInit(HardwareSerial &port, int rx_pin, int tx_pin)
{
    if (initialised) return false;
    initialised = true;
    txQueue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(UartMessage));
    stateMutex = xSemaphoreCreateMutex();
    if (!txQueue || !stateMutex) return false;
    uart = &port;
    uart->setRxBufferSize(256);
    uart->begin(9600, SERIAL_8N1, rx_pin, tx_pin);
    if (xTaskCreate(sendTask, "uart_tx", 3072, nullptr, 2, nullptr) != pdPASS) return false;
    if (xTaskCreate(receiveTask, "uart_rx", 4096, nullptr, 2, nullptr) != pdPASS) return false;
    return true;
}
