#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

const char *ssid = "ESP32-NET";
const char *password = "12345678";
WiFiUDP udp;
const int port = 4210;
char incomingPacket[255];

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("Verbunden!");
    udp.begin(port);
}

void loop() {
    int packetSize = udp.parsePacket();
    if (packetSize) {
        int len = udp.read(incomingPacket, 255);
        if (len > 0) incomingPacket[len] = 0;
        Serial.print("Empfangen: ");
        Serial.println(incomingPacket);
    }
    delay(100);
}
