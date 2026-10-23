#include "esp32_uart.h"
#define PET_RX_PIN 2
#define PET_TX_PIN 1
void setup()
{
    Serial.begin(9600); // Match your sample's Serial Monitor baud rate.

    if (!uartInit(Serial1, PET_RX_PIN, PET_TX_PIN)) {
        Serial.println("UART init failed");
        while (true) delay(1000);
    }

    Serial.println("1=ON, 0=OFF, p=consume button");
}
void loop()
{
    if (Serial.available()) {
        char command = (char)Serial.read();
        if (command == '1' || command == '0') {
            Serial.printf("Light accepted=%u\n", (unsigned)sendLight(command == '1'));
        } else if (command == 'p') {
            uint32_t sequence;
            if (consumeButton(&sequence)) Serial.printf("Consumed button %lu; ACK queued\n", (unsigned long)sequence);
            else Serial.println("No pending button");
        }
    }
    bool acknowledgedOn;
    if (consumeLightAck(&acknowledgedOn)) {
        // In production, Member 5 updates dashboard with this acknowledged setting.
        Serial.printf("Light command ACK consumed: requested state=%u\n", (unsigned)acknowledgedOn);
    }
    static uint32_t last;
    if ((uint32_t)(millis() - last) >= 1000) {
        last = millis();
        SensorValues values;
        if (getSensors(&values)) Serial.printf("water=%u food=%ld mg age=%lu ms\n",
            (unsigned)values.water_level, (long)values.food_milligrams,
            (unsigned long)(millis() - values.received_ms));
        UartResult result = getLightResult();
        Serial.printf("light seq=%lu status=%u\n", (unsigned long)result.sequence, (unsigned)result.status);
    }
    delay(10);
}
