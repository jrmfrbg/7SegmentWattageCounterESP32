#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

const char *ssid = "BalkonSolarDisplay";  
const char *password = "12345678";  

const int buttonPin = 10;

void setup() {
    Serial.begin(115200);
    pinMode(buttonPin, INPUT_PULLUP);

    WiFi.softAP(ssid, password);
    Serial.println("Access Point gestartet");
}

void loop() {
    if (digitalRead(buttonPin) == LOW) {
        Serial.println("Taster gedrückt!");

        HTTPClient http;
        http.begin("http://esp-slave.local/toggleLED");
        int httpResponseCode = http.GET();
        http.end();

        delay(500);
    }
}
