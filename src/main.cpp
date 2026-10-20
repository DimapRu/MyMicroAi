#include <Arduino.h>
#include "AppStateMachine.h"
#include "UsbBridge.h"

namespace {
AppStateMachine app;
}

void setup() {
    // Room for a whole chat message from the PC while the UI is busy redrawing.
    Serial.setRxBufferSize(UsbBridge::LINE_MAX_BYTES + 1024);
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
