#include <WiFi.h>
#include <WiFiServer.h>
#include <ESPmDNS.h>
#include <FastLED.h>

#define NUM_LEDS 21
#define DATA_PIN 10
#define FOTOTRANS_PIN 0

CRGB leds[NUM_LEDS];

int fototransVal = 0;
int currentNumb = 0;
int segmentStartLed[] = {0, 3, 6, 9, 12, 15, 18};
int segmentLedCount[] = {3, 3, 3, 3, 3, 3, 3};

bool segments[10][7] = {
  {1, 1, 1, 1, 1, 1, 0}, // 0
  {1, 0, 0, 0, 0, 2, 0}, // 1
  {0, 1, 1, 0, 1, 1, 1}, // 2
  {1, 1, 0, 0, 1, 1, 1}, // 3
  {1, 0, 0, 1, 0, 1, 1}, // 4
  {1, 1, 0, 1, 1, 0, 1}, // 5
  {1, 1, 1, 1, 1, 0, 1}, // 6
  {1, 0, 0, 0, 1, 1, 0}, // 7
  {1, 1, 1, 1, 1, 1, 1}, // 8
  {1, 1, 0, 1, 1, 1, 1}  // 9
};

const char *ssid = "BalkonSolarDisplay";
const char *password = "12345678";
int totalWattage = 0;
WiFiServer server(80);
int brightDevider = 0;


void writeLeds(int segmentVar, bool state) {
    int start = segmentStartLed[segmentVar];
    int count = segmentLedCount[segmentVar];
    //brightDevider = (analogRead(FOTOTRANS_PIN) / 16);
    for (int i = 0; i < count; i++) {
        if (state) {
        leds[start + i] = CRGB((1 * 255), (0), (1 * 255)); // Setzt nur RGB, kein Weiß
        } 
        else {
            leds[start + i] = CRGB::Black;
        }
    }
}

void displayNumber(int number) {
    for (int i = 0; i < 7; i++) {
        writeLeds(i, segments[number][i]);
    }
}

void setup() {
    pinMode(FOTOTRANS_PIN, INPUT);
    Serial.begin(115200);
    FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(255);
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
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    displayNumber(totalWattage);
    FastLED.show();

    server.begin();
}

void loop() {
    WiFiClient client = server.available();
    if (client) {
        String request = client.readStringUntil('\r');
        client.flush();

        if (request.indexOf("GET /toggleLED") >= 0) {

            totalWattage++;
            if (totalWattage == 10) {
              totalWattage = 0;
            }
            Serial.print("totalWattage=");
            Serial.println(totalWattage);
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            displayNumber(totalWattage);
            FastLED.show();
            Serial.print("foto transistor Value: ");
            Serial.println(analogRead(FOTOTRANS_PIN));
        }

        client.stop();
    }
}