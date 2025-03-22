#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

const char *ssid = "BalkonSolarDisplay";  
const char *password = "12345678";  

const int buttonPin = 10;
const int ledPin = 3;

void startupBlinkLED() {
  for (int i = 0; i < 10; i++) {
    digitalWrite(ledPin, HIGH);
    delay(10);
    digitalWrite(ledPin, LOW);
    delay(200);
  }
}

void setup() {
    Serial.begin(115200);
    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(ledPin, OUTPUT);
    startupBlinkLED();

    WiFi.softAP(ssid, password);
    Serial.println("Access Point gestartet");
}

void loop() {
    if (digitalRead(buttonPin) == LOW) {
        Serial.println("Taster gedrückt!");

        HTTPClient http;
        http.begin("http://esp-slaveTotalWattage.local/toggleLED");
        http.begin("http://esp-slaveCurrentWattage.local/toggleLED");
        int httpResponseCode = http.GET();
        http.end();

        delay(500);
    }
}