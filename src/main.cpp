#include <Arduino.h>
#include "AppStateMachine.h"

namespace {
AppStateMachine app;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println(F("--- MyMicroAI ---"));
    app.begin();
}

void loop() {
    app.update();
    delay(10);
}
