#include <SPI.h>         // Arduino SPI-Bibliothek für ESP32/ESP8266
#include <Arduino.h>

// === Pin‑Definitionen ===
// Diese Pins müssen mit den Pins des Slaves verbunden werden:
//  Slave MOSI (DOUT) → Master MISO
//  Slave MISO (DIN)  → Master MOSI
//  Slave SCLK        → Master SCLK
//  Optional: Slave CS → Master CS
#define PIN_SPI_MISO 25  // vom Slave DOUT, beim Master als MISO konfigurieren
#define PIN_SPI_MOSI 26  // zum Slave DIN, beim Master als MOSI konfigurieren
#define PIN_SPI_SCLK 32  // gemeinsame Clock-Leitung
#define PIN_SPI_CS   33   // Chip‐Select, HIGH = Inaktiv, LOW = aktiv

SPIClass spiMaster(HSPI);

void setup() {
  Serial.begin(9600);
  delay(100);

  // SPI starten: (SCLK, MISO, MOSI, SS)
  spiMaster.begin(PIN_SPI_SCLK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);

  // CS‑Pin als Ausgang definieren und inaktiv (HIGH) setzen
  pinMode(PIN_SPI_CS, OUTPUT);
  digitalWrite(PIN_SPI_CS, HIGH);

  Serial.println("SPI-Master bereit. Sende Testdaten an den Slave...");
}

void loop() {
  // Testwerte generieren
  float vrms = random(22000, 24000) / 100.0;   // 220.00 … 239.99 V
  float arms = random( 1000, 1500) / 1000.0;   // 1.000 … 1.499 A
  float wrms = vrms * arms;                    // Leistung

  // String im erwarteten Format
  char txbuf[128];
  int len = snprintf(txbuf, sizeof(txbuf),
                     "Vrms: %.2f; Arms: %.3f; Wrms: %.1f;",
                     vrms, arms, wrms);

  // SPI‑Transaktion starten
  digitalWrite(PIN_SPI_CS, LOW);               // Slave aktivieren
  // Daten senden (kein Empfangpuffer nötig)
  spiMaster.transferBytes((uint8_t*)txbuf, nullptr, len + 1); 
  digitalWrite(PIN_SPI_CS, HIGH);              // Slave deaktivieren

  // Debug-Ausgabe
  Serial.print("Gesendet: ");
  Serial.println(txbuf);

  delay(1000);  // 1 Sekunde warten
}
