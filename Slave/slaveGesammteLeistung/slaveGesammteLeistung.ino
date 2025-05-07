#include <WiFi.h>
#include <WiFiServer.h>
#include <ESPmDNS.h>
#include <FastLED.h>

#define NUM_LEDS 120
#define DATA_PIN 10
#define FOTOTRANS_PIN 0

CRGB leds[NUM_LEDS];

int fototransVal = 0;
int currentNumb = 0;
int segmentStartLed[] = { 0, 3, 6, 9, 12, 15, 18 };
int segmentLedCount[] = { 3, 3, 3, 3, 3, 3, 3 };
int debugPrint = 0;
int digitStartLed[] = { 0, 20, 40, 60, 80, 100 };

bool segments[10][7] = {
  { 1, 1, 1, 1, 1, 1, 0 },  // 0
  { 1, 0, 0, 0, 0, 1, 0 },  // 1
  { 0, 1, 1, 0, 1, 1, 1 },  // 2
  { 1, 1, 0, 0, 1, 1, 1 },  // 3
  { 1, 0, 0, 1, 0, 1, 1 },  // 4
  { 1, 1, 0, 1, 1, 0, 1 },  // 5
  { 1, 1, 1, 1, 1, 0, 1 },  // 6
  { 1, 0, 0, 0, 1, 1, 0 },  // 7
  { 1, 1, 1, 1, 1, 1, 1 },  // 8
  { 1, 1, 0, 1, 1, 1, 1 }   // 9
};

const char *ssid = "BalkonSolarDisplay";
const char *password = "12345678";
float totalWattage = 0000.0;
WiFiServer server(80);
int brightDevider = 0;




void writeLeds(int digitVar, int segmentVar, bool state) {
  
  int start = segmentStartLed[segmentVar] + digitStartLed[digitVar];
  for (int i = 0; i < count; i++) {
    if (state == true) {
      leds[start + i] = CRGB((1 * 255), (0), (1 * 255));
    } else {
      leds[start + i] = CRGB::Black;
    }
  }
}

//Function 1 in stream 
//Input: get total wattage from void loop
//Parse the input. Each digit of the input float is sent to displayNumber(). the dec point is send to writeDecPoint()

//Output:
//writeDecPoint(int, bool): what dec point to write to, status of dec point on/off
//displayNumber(char, int): what digit to write to LED steipe, i is used to calculate, where to write
void writeDigit(float number) {
  std::string numberString;
  numberString = std::__cxx11::to_string(number);
  for (int i = 0; i <= 5; i++) {
    char currentChar = numberString[i];
    char dotChar = '.';
    if (currentChar == dotChar) {
      writeDecPoint(i, true);
    }

    else {
      writeDecPoint(i, false);
      displayNumber(currentChar - 0, i);
    }
  }
}


//function 2 ins stream
//input: (int, bool): what dec point to write to, status of dec point on/off, send by writeDigit
//output: writes the current decimalpoint to leds[]
void writeDecPoint(int digitVar, bool state) {
  if (state = true) {
    leds[digitStartLed[digitVar] - 1] = CRGB(1 * 255), (0), (1 * 255);
  } else {
    leds[digitStartLed[digitVar] - 1] = CRGB::Black;
  }
}

//function 3 in stream
//input: what digit to write, where to write
//output: int, bool, bool
void displayNumber(int digit, int digitCount) {
  Serial.println("displayNumber 1");
  for (int i = 0; i < 7; i++) {
    Serial.println("displayNumber 2");
    Serial.print("");
    writeLeds(i, digitCount, segments[digit][i]);
  }
}

void setup() {
  pinMode(FOTOTRANS_PIN, INPUT);
  Serial.begin(115200);
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  // WLAN-Verbindung zum AP herstellen
  /*
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
    */
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  writeDigit(totalWattage);
  FastLED.show();
  Serial.println("ESP32 started");
  //server.begin();
}

void loop() {
  Serial.println(totalWattage);
  debugPrint = 0;
  Serial.println(debugPrint);
  debugPrint++;
  totalWattage = totalWattage + 0.1;
  if (totalWattage == 9999.9) {
    totalWattage = 0;
  }
  Serial.println(debugPrint);
  debugPrint++;
  Serial.print("totalWattage=");
  Serial.println(totalWattage);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  Serial.println(debugPrint);
  debugPrint++;
  writeDigit(totalWattage);
  Serial.println(debugPrint);
  debugPrint++;
  FastLED.show();
  Serial.println(debugPrint);
  debugPrint++;
  Serial.print("foto transistor Value: ");
  Serial.println(debugPrint);
  debugPrint++;
  Serial.println(analogRead(FOTOTRANS_PIN));
  Serial.println(debugPrint);
  debugPrint++;
  delay(500);
}