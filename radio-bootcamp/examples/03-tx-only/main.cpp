#include <Arduino.h>
#include <RadioLib.h>
#include "config.h"

Module radioModule(NSS_PIN, DIO1_PIN, RST_PIN, BUSY_PIN);
SX1262 radio900(&radioModule);
SX1268 radio433(&radioModule);
SX126x *radio = nullptr;

void setup() {
    Serial.begin(GS_SERIAL_BAUD_RATE);
    while (!Serial) {
        delay(10);
    }

    pinMode(FREQ_PIN, INPUT);
    analogReadResolution(10);
    delay(400);

    // Band read
    bool is900 = analogRead(FREQ_PIN) > 350;
    float frequency = is900 ? 914.50f : 433.00f;
    radio = is900 ? static_cast<SX126x *>(&radio900)
                  : static_cast<SX126x *>(&radio433);

    Serial.print("Initializing radio at ");
    Serial.print(frequency);
    Serial.println(" MHz");

    // MHz, bandwidth (kHz), spreading factor, coding rate, sync word,
    // power (dBm), preamble length, TCXO voltage, use LDO.
    int state = is900
        ? radio900.begin(frequency, 250.0f, 8, 8, 0x12, 22, 12, 3.0f, false)
        : radio433.begin(frequency, 250.0f, 8, 8, 0x12, 22, 12, 3.0f, false);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("Radio initialization failed, code: ");
        Serial.println(state);
        while (true) {
            delay(1000);
        }
    }

    Serial.println("Radio initialization successful!");
}

void loop() {
    int state = radio->transmit("Hello from radio bootcamp!");

    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("Message sent!");
    } else {
        Serial.print("Transmit failed, code: ");
        Serial.println(state);
    }

    delay(1000);
}
