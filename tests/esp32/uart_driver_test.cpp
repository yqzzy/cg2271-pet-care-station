#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

// Include the production implementation to exercise its parser and task loops
// without adding test hooks to the firmware API.
#define ESP_UART_DEBUG 0
#include "../../firmware/esp32/libraries/PetStationUart/src/esp32_uart.cpp"

namespace fake {
uint32_t now = 0;
unsigned mutexDepth = 0;
unsigned ioWhileLocked = 0;
unsigned queueReceivesLeft = 0;
}

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static void receive(HardwareSerial &port, const char *wire) {
    port.input = wire;
    try { receiveTask(nullptr); } catch (const fake::StopTask &) {}
    require(port.input.empty(), "RX must consume complete test input");
}

static void transmitOne() {
    fake::queueReceivesLeft = 1;
    try { sendTask(nullptr); } catch (const fake::StopTask &) {}
}

// This catches the original loss: the event was cleared and reported consumed
// even though a full TX queue silently rejected its BUTTON_ACK.
static void buttonAckSurvivesFullQueue(HardwareSerial &port) {
    receive(port, "BUTTON:1*1D\n");
    UartMessage filler = {"UNRELATED\n"};
    for (unsigned i = 0; i < UART_QUEUE_LENGTH; ++i)
        require(xQueueSend(txQueue, &filler, 0) == pdPASS, "fill TX queue");

    uint32_t sequence = 99;
    require(!consumeButton(&sequence), "full ACK queue must defer button consumption");
    require(sequence == 99, "deferred consume must leave caller output unchanged");
    receive(port, "BUTTON:1*1D\n"); // Retry while the ACK queue is still full.
    require(!consumeButton(&sequence), "duplicate while full must remain pending");
    txQueue->items.clear(); // Simulate TX draining unrelated traffic.

    unsigned events = 0;
    if (consumeButton(&sequence)) ++events;
    require(events == 1 && sequence == 1, "retry must deliver the original button once");
    require(!consumeButton(&sequence), "consumed button must not deliver twice");
    transmitOne();
    require(port.output == "BUTTON_ACK:1*0B\n", "accepted button must emit valid ACK packet");

    receive(port, "BUTTON:1*1D\n"); // Lost ACK: resend, no second application event.
    if (consumeButton(&sequence)) ++events;
    transmitOne();
    require(events == 1, "duplicate button must only replay its ACK");
    require(port.output == "BUTTON_ACK:1*0B\nBUTTON_ACK:1*0B\n", "duplicate must resend ACK");

    receive(port, "BUTTON:2*1E\n");
    if (consumeButton(&sequence)) ++events;
    require(events == 2 && sequence == 2, "next sequence must deliver a new event");
    transmitOne();
    receive(port, "BUTTON:1*1D\nBUTTON:3*00\n"); // Stale and corrupt packets.
    require(!consumeButton(&sequence), "stale/corrupt packet must not create an event");
    port.output.clear();
}

static void bridgePollingDoesNotWaitForMutex(HardwareSerial &port) {
    receive(port, "BUTTON:3*1F\n");
    require(xSemaphoreTake(stateMutex, 0) == pdPASS, "hold UART mutex");
    uint32_t sequence = 77;
    bool on = false;
    try {
        require(!consumeButton(&sequence), "busy mutex must defer button polling");
        require(sequence == 77, "busy button poll must preserve output");
        require(!sendLight(true), "busy mutex must defer light request");
        require(!consumeLightAck(&on), "busy mutex must defer ACK polling");
        UartResult unavailable = getLightResult();
        require(unavailable.status == UART_IDLE && unavailable.sequence == 0,
                "busy status poll must return neutral unavailable result");
    } catch (const fake::BlockingWait &) {
        xSemaphoreGive(stateMutex);
        throw std::runtime_error("bridge polling must never wait for the UART mutex");
    }
    require(stateMutex->locked, "busy polls must not release another task's mutex");
    xSemaphoreGive(stateMutex);
    require(consumeButton(&sequence) && sequence == 3, "busy poll must preserve pending button");
    transmitOne();
    port.output.clear();
}

static void matchingLightAckIsConsumedOnce(HardwareSerial &port) {
    require(sendLight(true), "queue first light request");
    transmitOne();
    require(port.output == "LIGHT:1,1*48\n", "light packet has sequence and checksum");
    receive(port, "LIGHT_ACK:2*40\nLIGHT_ACK:1*00\n");
    bool on = false;
    require(!consumeLightAck(&on), "unmatched/corrupt ACK must not complete light");
    require(getLightResult().status == UART_WAITING, "invalid ACK must leave light waiting");
    receive(port, "LIGHT_ACK:1*43\n");
    require(getLightResult().status == UART_CONSUMED, "matching ACK completes request");
    require(!sendLight(false), "unread ACK must not be overwritten");
    require(consumeLightAck(&on) && on, "ACK exposes original requested state");
    receive(port, "LIGHT_ACK:1*43\n");
    require(!consumeLightAck(&on), "repeated ACK must not create another completion");
    fake::now += 1000;
    transmitOne();
    require(port.output == "LIGHT:1,1*48\n", "completed request must not retry");
    port.output.clear();
}

static void lightRetriesReachTimeout(HardwareSerial &port) {
    require(sendLight(false), "queue second light request");
    transmitOne();
    // Empty queue waits 20 ms per TX iteration: schedule exact 200 ms boundaries.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        fake::now += 180;
        transmitOne();
    }
    require(port.output == "LIGHT:2,0*4A\nLIGHT:2,0*4A\nLIGHT:2,0*4A\n",
            "unacknowledged light must send initial packet and exactly two retries");
    UartResult result = getLightResult();
    require(result.sequence == 2 && result.status == UART_TIMED_OUT,
            "third elapsed retry deadline must time out the matching request");
    bool on = true;
    require(!consumeLightAck(&on), "timeout must not invent an ACK event");
    receive(port, "LIGHT_ACK:2*40\n");
    require(!consumeLightAck(&on), "late ACK after timeout must not create completion");
}

int main() {
    HardwareSerial port;
    try {
        require(uartInit(port, 16, 17), "initialise production UART driver");
        buttonAckSurvivesFullQueue(port);
        bridgePollingDoesNotWaitForMutex(port);
        matchingLightAckIsConsumedOnce(port);
        lightRetriesReachTimeout(port);
        require(fake::ioWhileLocked == 0, "UART writes must occur outside state mutex");
        std::puts("PASS: UART BUTTON ACK backpressure, duplicates, nonblocking polls, light ACK/retries/timeout");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
