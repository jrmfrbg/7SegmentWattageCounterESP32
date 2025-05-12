#include <WiFi.h>
#include <WiFiServer.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // Für das Parsen der JSON-Antwort
#include <stdlib.h> // For strtof

#define NUM_LEDS 132
#define DATA_PIN 10

CRGB leds[NUM_LEDS];
int currentNumb = 0;
int segmentStartLed[] = { 0, 3, 6, 9, 12, 15, 18 };
int segmentLedCount[] = { 3, 3, 3, 3, 3, 3, 3 };
//int debugPrint = 0;
int digitStartLed[] = { 0, 22, 44, 66, 88, 110 };
String payloadString = "foo";
const char *masterIp = "192.168.4.1"; // Standard-IP des ESP32 im AP-Modus
const uint16_t masterPort = 80;
const char *dataEndpoint = "/data";


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
float wattage = 0000.0;
float lastwattage = 0000.0;
WiFiServer server(80);
int brightDevider = 0;


//function 4 in Stream, writes the values it recieves to the LEDs
//input: int, int, bool; digitVar: what digit to write to, segmentVar: what segment of Digit to write to, state to write: true=on false=off;
//output: writes the leds to leds[]

void writeLeds(int digitVar, int segmentVar, bool state) {

  int start = segmentStartLed[segmentVar] + digitStartLed[digitVar];
  int count = segmentLedCount[segmentVar];
  for (int i = 0; i < count; i++) {
    if (state == true) {
      leds[start + i] = CRGB((255), (0), (0));
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
  int stri = 0;
  for (int i = 0; i <= 5; i++) {
    char currentChar = numberString[stri];
    char dotChar = '.';

    if(currentChar == '-') {
        writeMinus(i, true);
    }
    else if (currentChar == dotChar) {
      writeDecPoint(i, true);
      i--;
      //displayNumber(currentChar - '0', i);
    }
    else {
      displayNumber(currentChar - '0', i);
    }
    stri++;
  }
}


void writeMinus(int digitVar, bool state) {

  if (state == true) {
    for(int i = 0; i <= 2; i++) {
      leds[18 + i] = CRGB((200), (0), (0));
    }
  }
}

//function 2 ins stream
//input: (int, bool): what dec point to write to, status of dec point on/off, send by writeDigit
//output: writes the current decimalpoint to leds[]
void writeDecPoint(int digitVar, bool state) {

  if (state == true) {
    leds[digitStartLed[digitVar] - 1] = CRGB((200), (0), (0));
  }

  else {
    //leds[digitStartLed[digitVar] - 1] = CRGB::Black;
  }
}

//function 3 in stream
//input: what digit to write, where to write
//output: int, bool, bool
void displayNumber(int digit, int digitCount) {
  for (int i = 0; i < 7; i++) {
    writeLeds(digitCount, i, segments[digit][i]);
  }
}

void setup() {
  Serial.begin(9600);
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nVerbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
  
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  writeDigit(wattage);
  FastLED.show();
  Serial.println("ESP32 started");
  //server.begin();
}

void loop() {
  HTTPClient http;
  if (WiFi.status() == WL_CONNECTED) {
    String serverUrl = "http://" + String(masterIp) + ":" + String(masterPort) + String(dataEndpoint);
    http.begin(serverUrl);
    Serial.print("http.GET=");
    int httpCode = http.GET();  // GET-Anfrage senden
    Serial.println(httpCode);
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        payloadString = http.getString();
        Serial.println("Antwort vom Server:");
        Serial.println(payloadString);
        int state = 0;
        String wattSlave;
        for (char c : payloadString) {
          if (state <= 3) {
            if (c == ':') {
              state ++;
            }
          }
          else if (state == 4) {
            if (c == '}') {
              break;
            }
            wattSlave += c;
            //Serial.print("wattSlave = ");
            //Serial.println(wattSlave);
            wattage = wattSlave.toFloat();
          }
        }
      }
    } else {
      Serial.println("error with HTTP");
    }
  }

  Serial.print("wattage=");
  Serial.println(wattage);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  if(lastwattage != wattage) {
    writeDigit(wattage);
    FastLED.show();
    lastwattage = wattage;
    Serial.println("-------------------------------------------------------------------------------------");
  }
  
  Serial.print("total wattage: ");
  Serial.println(wattage);
  delay(500);
}