#include <Arduino.h>
#include "config.h"

void setup() {
    Serial.begin(GS_SERIAL_BAUD_RATE);
}

void loop() {
    Serial.println("Hello from radio bootcamp!");
    delay(1000);
}
