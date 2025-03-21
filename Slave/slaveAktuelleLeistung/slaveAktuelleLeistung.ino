#include <WiFi.h>
#include <WiFiServer.h>
#include <ESPmDNS.h>

const char *ssid = "BalkonSolarDisplay";
const char *password = "12345678";
const int ledPin = 3;
WiFiServer server(80);
unsigned long lastResetTime = millis();
int wattCount = 0;

void registerWattUsage() {
    digitalWrite(ledPin, HIGH);
    delay(500);
    digitalWrite(ledPin, LOW);
    wattCount++;

    unsigned long elapsedTime = millis() - lastResetTime;
    float elapsedSeconds = elapsedTime / 1000.0;

    float currentWattPerSecond = wattCount / elapsedSeconds;

    Serial.print("Aktuelle Watt/s: ");
    Serial.println(currentWattPerSecond);
    Serial.print("Aktuelle Watt/h: ");
    float currentWattPerHour = currentWattPerSecond * 3600.0;
    Serial.println(currentWattPerHour);
    lastResetTime = 0;
    wattCount = 0;
}

void setup() {
    Serial.begin(115200);
    pinMode(ledPin, OUTPUT);

    // WLAN-Verbindung zum AP herstellen
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nVerbunden!");
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("esp-slaveCurrentWattage")) {
        Serial.println("Fehler beim Start von mDNS!");
    } else {
        Serial.println("mDNS-Dienst gestartet: esp-slaveCurrentWattage.local");
    }

    server.begin();
}

void loop() {
    WiFiClient client = server.available();
    if (client) {
        String request = client.readStringUntil('\r');
        client.flush();

        if (request.indexOf("GET /toggleLED") >= 0) {
          registerWattUsage();
        }

        client.stop();
    }
}
