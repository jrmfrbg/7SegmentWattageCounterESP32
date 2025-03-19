#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

const char *ssid = "ESP32-NET";
const char *password = "12345678";
WiFiUDP udp;
const int port = 4210;
int rawWattage = 0;
int wattagePin = A0;

void setup() {
  rawWattage = 0;
  Serial.begin(115200);
    WiFi.softAP(ssid, password);
    udp.begin(port);
}

void loop() {
  if (analogRead(wattagePin) > 512) {
    rawWattage++;
  }
}