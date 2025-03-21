#include <WiFi.h>
#include <WiFiServer.h>
#include <ESPmDNS.h>

const char *ssid = "BalkonSolarDisplay";
const char *password = "12345678";
int totalWattage = 0;
const int ledPin = 3;
WiFiServer server(80);

void ledPrintTotalWattage() {
  for (int i = 0; i < totalWattage; i++) {
    digitalWrite(ledPin, HIGH);
    delay(250);
    digitalWrite(ledPin, LOW);
    delay(250);
  }
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

    if (!MDNS.begin("esp-slave")) {
        Serial.println("Fehler beim Start von mDNS!");
    } else {
        Serial.println("mDNS-Dienst gestartet: esp-slave.local");
    }

    server.begin();
}

void loop() {
    WiFiClient client = server.available();
    if (client) {
        String request = client.readStringUntil('\r');
        client.flush();

        if (request.indexOf("GET /toggleLED") >= 0) {
            totalWattage++;
            Serial.println("totalWattage=");
            Serial.println(totalWattage);
            ledPrintTotalWattage();
        }

        client.stop();
    }
}
