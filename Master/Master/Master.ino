#include <WiFi.h>
// #include <HTTPClient.h> // Not needed for serving a page
#include <WebServer.h> // <<< Added WebServer library
#include <ESPmDNS.h>
#include <Arduino.h>

extern "C" {
#include "driver/spi_slave.h"
}

// === Pin‑Definitionen ===
#define PIN_SPI_MISO 11  // nicht verwendet
#define PIN_SPI_MOSI 12  // Data In für den Slave (DIN)
#define PIN_SPI_SCLK 10  // Clock
#define PIN_SPI_CS 2
#define RXBUF_SIZE 256

// === WiFi Credentials ===
const char *ssid = "BalkonSolarDisplay";
const char *password = "12345678";

// === Web Server Instance ===
WebServer server(80); // Create a web server object on port 80

// === Global Variables ===
int lastRes = 0;
unsigned long lastUpdateMillis = 0; // To track time for energy calculation

float vrmsStore = 0.0;
float armsStore = 0.0;
float wrmsStore = 0.0;

float joule = 0.0;
float wattH = 0.0;

// === Function Prototypes ===
void handleRoot();
void handleNotFound();
void parseAndStore(const char *data);

// === Setup Function ===
void setup() {
  Serial.begin(9600);
  Serial.println("Startup loading...");
  delay(100); // Short delay for stability

  // --- WiFi Access Point Setup ---
  Serial.print("Starting Access Point: ");
  Serial.println(ssid);
  WiFi.softAP(ssid, password);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  // --- mDNS Setup (optional, access via http://esp32.local/) ---
  if (MDNS.begin("esp32")) { // Hostname "esp32"
    Serial.println("MDNS responder started");
    MDNS.addService("http", "tcp", 80); // Advertise web server
  } else {
    Serial.println("Error starting MDNS");
  }

  // --- SPI Slave Setup ---
  spi_bus_config_t buscfg = {
    .mosi_io_num = PIN_SPI_MOSI,
    .miso_io_num = PIN_SPI_MISO,
    .sclk_io_num = PIN_SPI_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = RXBUF_SIZE
  };

  spi_slave_interface_config_t slvcfg = {
    .spics_io_num = PIN_SPI_CS,
    .queue_size = 1,
    .mode = 0
  };

  esp_err_t ret = spi_slave_initialize(SPI3_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    Serial.printf("Fehler: SPI-Slave init fehlgeschlagen (%s)\n", esp_err_to_name(ret));
    while (true) { delay(1000); }
  }
  Serial.println("SPI-Slave bereit.");

  // --- Web Server Handler Setup ---
  server.on("/", HTTP_GET, handleRoot); // Call handleRoot for "/"
  server.onNotFound(handleNotFound);  // Handle invalid paths
  server.begin(); // Start the web server
  Serial.println("HTTP server started");

  lastUpdateMillis = millis(); // Initialize timing for energy calculation
}

// === Main Loop ===
void loop() {
  // --- Handle Web Server Clients ---
  server.handleClient(); // Check for incoming HTTP requests

  // --- Handle SPI Communication ---
  static char rxbuf[RXBUF_SIZE];
  spi_slave_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = RXBUF_SIZE * 8;
  t.rx_buffer = rxbuf;

  // Using a timeout for spi_slave_transmit instead of portMAX_DELAY
  // This prevents the web server from becoming unresponsive if no SPI data arrives.
  // Timeout is set to 10 milliseconds. Adjust if needed.
  esp_err_t ret = spi_slave_transmit(SPI3_HOST, &t, pdMS_TO_TICKS(10));

  if (ret == ESP_OK) {
    size_t received_bytes = t.trans_len / 8;
    if (received_bytes > 0) { // Process only if data was actually received
        if (received_bytes >= RXBUF_SIZE) {
          received_bytes = RXBUF_SIZE - 1;
        }
        rxbuf[received_bytes] = '\0';
        //Serial.printf("Rohdaten (%d Bytes): %s\n", received_bytes, rxbuf); // Optional: uncomment for debugging raw data
        parseAndStore(rxbuf);
    }
  } else if (ret != ESP_ERR_TIMEOUT) { // Log errors other than timeout
    Serial.printf("Fehler beim SPI-Empfang: %s\n", esp_err_to_name(ret));
  }
  // No delay needed here, as spi_slave_transmit has a timeout and server.handleClient() should run frequently.
  // delay(10); // Can be removed or kept small
}

// === Data Parsing Function ===
void parseAndStore(const char *data) {
  float vrms_f, arms_f, wrms_f;
  // Use sscanf to parse the data string
  int n = sscanf(data, "Vrms: %f; Arms: %f; Wrms: %f;", &vrms_f, &arms_f, &wrms_f);

  if (n == 3) { // Check if all 3 values were successfully parsed
    // Calculate time delta for energy calculation
    unsigned long currentTime = millis();
    float timeDiffSeconds = (currentTime - lastUpdateMillis) / 1000.0;

    // Update energy calculation only if time has passed and power is positive
    if (timeDiffSeconds > 0 && wrms_f >= 0) {
      joule += wrms_f * timeDiffSeconds; // Accumulate energy in Joules (Watt-seconds)
      wattH = joule / 3600.0;         // Convert Joules to Watt-hours
    }

    // Update stored values
    vrmsStore = vrms_f;
    armsStore = arms_f;
    wrmsStore = wrms_f;

    // Update the timestamp for the next calculation
    lastUpdateMillis = currentTime;

    // Print parsed values to Serial monitor (optional)
    Serial.printf("Parsed -> Vrms=%.2f, Arms=%.3f, Wrms=%.1f, WattH=%.3f\n", vrmsStore, armsStore, wrmsStore, wattH);

  } else {
    // Print an error message if parsing failed
    Serial.printf("Parse-Fehler: Format stimmt nicht (erwartet 3 floats, gefunden %d). Data: %s\n", n, data);
  }
}


// === Web Server Request Handlers ===

// Function to handle the root ("/") request
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>ESP32 Sensor Data</title>";
  // Add meta tag for auto-refresh every 10 seconds
  html += "<meta http-equiv='refresh' content='10'>";
  html += "<style>";
  html += "body { font-family: sans-serif; background-color: #f4f4f4; margin: 20px; }";
  html += "h1 { color: #333; text-align: center; }";
  html += ".container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); max-width: 400px; margin: auto; }";
  html += "p { font-size: 1.1em; line-height: 1.6; margin: 10px 0; }";
  html += ".label { font-weight: bold; color: #555; }";
  html += ".value { float: right; color: #007bff; }"; // Right align values
  html += ".clearfix::after { content: \"\"; clear: both; display: table; }"; // Clearfix for floats
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>ESP32 Sensor Data</h1>";

  // Display data using paragraph tags and spans for styling/layout
  html += "<p class='clearfix'><span class='label'>Watt-Hours (Wh):</span><span class='value'>" + String(wattH, 3) + "</span></p>"; // Display Wh with 3 decimal places
  html += "<p class='clearfix'><span class='label'>Voltage (Vrms):</span><span class='value'>" + String(vrmsStore, 2) + "</span></p>"; // Display Vrms with 2 decimal places
  html += "<p class='clearfix'><span class='label'>Current (Arms):</span><span class='value'>" + String(armsStore, 3) + "</span></p>"; // Display Arms with 3 decimal places
  html += "<p class='clearfix'><span class='label'>Power (Wrms):</span><span class='value'>" + String(wrmsStore, 1) + "</span></p>"; // Display Wrms with 1 decimal place

  html += "</div></body></html>";

  server.send(200, "text/html", html); // Send the HTML page to the client
}

// Function to handle requests for paths that are not found
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri(); // Get the requested URI
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message); // Send 404 response
}