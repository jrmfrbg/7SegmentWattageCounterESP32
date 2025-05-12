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
void handleData(); // Added prototype
void handleResetEnergy(); // Added prototype for reset handler

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
  if (MDNS.begin("mlScholliMaster")) { // Hostname "mlScholliMaster"
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
  server.on("/data", HTTP_GET, handleData); // Ensure HTTP_GET is specified
  server.on("/reset_energy", HTTP_POST, handleResetEnergy); // Handler for resetting energy
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
        parseAndStore(rxbuf);
    }
  } else if (ret != ESP_ERR_TIMEOUT) { // Log errors other than timeout
    Serial.printf("Fehler beim SPI-Empfang: %s\n", esp_err_to_name(ret));
  }
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
    if (timeDiffSeconds > 0) {
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
    // Serial.printf("Parsed -> Vrms=%.2f, Arms=%.3f, Wrms=%.1f, WattH=%.3f\n", vrmsStore, armsStore, wrmsStore, wattH);

  } else {
    // Print an error message if parsing failed
    Serial.printf("Parse-Fehler: Format stimmt nicht (erwartet 3 floats, gefunden %d). Data: %s\n", n, data);
  }
}


// === Web Server Request Handlers ===

// Function to handle the root ("/") request
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Mein lieber Scholli - Interface</title>
  <style>
    /* Basic reset */
    body, h1, h2, h3, p, div, header, main, footer, input, button, details, summary {
      margin: 0; padding: 0; box-sizing: border-box;
    }
    body {
      font-family: sans-serif;
      background-color: #f4f4f4;
      color: #333;
      display: flex;
      flex-direction: column;
      min-height: 100vh;
    }
    header {
      background: linear-gradient(to right, #c4b5fd, #99f6e4);
      color: #222;
      padding: 1rem;
      text-align: center;
      box-shadow: 0 2px 5px rgba(0,0,0,0.1);
    }
    main {
      flex: 1;
      padding: 1rem;
      max-width: 800px;
      margin: auto;
    }
    h1 { font-size: 1.75rem; margin-bottom: 0.5rem; }
    h2 { font-size: 1.5rem; margin: 1rem 0; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 1rem;
    }
    .card {
      background: #fff;
      padding: 1rem;
      border-radius: 8px;
      box-shadow: 0 2px 5px rgba(0,0,0,0.1);
      border-left: 4px solid #7c3aed;
      transition: background-color 0.3s ease, color 0.3s ease;
    }
    .card h3 {
      font-size: 1.1rem;
      margin-bottom: 0.5rem;
      color: #555;
    }
    .card p.value {
      font-size: 2rem;
      font-weight: bold;
      color: #7c3aed;
    }
    details {
      background: #fff;
      padding: 1rem;
      border-radius: 8px;
      margin-top: 1rem;
      box-shadow: 0 2px 5px rgba(0,0,0,0.1);
    }
    summary {
      font-size: 1.1rem;
      font-weight: bold;
      cursor: pointer;
      margin-bottom: 0.5rem;
      color: #333;
    }
    form > div {
      margin-bottom: 0.75rem;
    }
    label {
      display: block;
      font-weight: bold;
      margin-bottom: 0.25rem;
      color: #555;
    }
    input {
      width: 100%;
      padding: 0.5rem;
      border: 1px solid #ccc;
      border-radius: 4px;
    }
    button { /* General button style */
      padding: 0.5rem 1rem;
      background: #14b8a6; /* Teal color from original "Speichern" */
      color: #fff;
      border: none;
      border-radius: 4px;
      cursor: pointer;
      font-size: 1rem; /* Ensure consistent font size */
      margin-top: 0.5rem; /* Add some top margin */
    }
    button:hover {
        opacity: 0.9;
    }
    .reset-button-container { /* Container for the reset button */
        margin-top: 1.5rem; /* Space above the button */
        text-align: center; /* Center the button */
    }
    footer {
      text-align: center;
      padding: 1rem;
      background: #e5e7eb;
      font-size: 0.9rem;
      color: #555;
    }
    .clearfix::after { content: ""; display: table; clear: both; }
  </style>
</head>
<body>
  <header>
    <h1>Mein lieber Scholli - Interface</h1>
  </header>

  <main>
    <h2>Aktuelle Messwerte</h2>
    <div class="grid">
      <div class="card">
        <h3>Spannung (Vrms)</h3>
        <p class="value"><span id="vrms">–</span> V</p>
      </div>
      <div class="card">
        <h3>Leistung (Wrms)</h3>
        <p class="value"><span id="wrms">–</span> W</p>
      </div>
      <div class="card">
        <h3>Stromstärke (Arms)</h3>
        <p class="value"><span id="arms">–</span> A</p>
      </div>
      <div class="card">
        <h3>Watt-Stunden (Wh)</h3>
        <p class="value"><span id="wh">–</span> Wh</p>
      </div>
    </div>

    <div class="reset-button-container">
        <button id="resetEnergyButton">Energie Zähler Reset</button>
    </div>

    <details>
      <summary>WiFi Einstellungen (hopefully working soon)</summary>
      <form>
        <div>
          <label for="ssid_input">SSID</label> <input id="ssid_input" type="text" placeholder="Deine SSID">
        </div>
        <div>
          <label for="password_input">Passwort</label> <input id="password_input" type="password" placeholder="Dein Passwort">
        </div>
        <button type="button">Speichern</button>
      </form>
    </details>
  </main>

  <footer>
    <p>Mitwirkende: GitHub jrmfrbg – FreiLab – Ihle Engineering – Balkon.solar</p>
    <p>&copy; 2025 Jorim Stern</p>
  </footer>

  <script>
    // Daten alle 0.5 Sekunden abrufen
    setInterval(fetchData, 500);

    function fetchData() {
      fetch('/data')
        .then(res => {
            if (!res.ok) {
                throw new Error('Network response was not ok: ' + res.statusText);
            }
            return res.json();
        })
        .then(d => {
          document.getElementById('vrms').textContent = d.vrms.toFixed(2);
          document.getElementById('wrms').textContent = d.wrms.toFixed(1);
          document.getElementById('arms').textContent = d.arms.toFixed(3);
          document.getElementById('wh').textContent   = d.wattH.toFixed(3);
        })
        .catch(err => console.error('Fetch /data fehlgeschlagen:', err));
    }

    document.getElementById('resetEnergyButton').addEventListener('click', function() {
        if (confirm('Möchten Sie die Energiezähler (Wh und Joule) wirklich zurücksetzen?')) {
            fetch('/reset_energy', {
                method: 'POST' // Specify POST method
            })
            .then(res => {
                if (res.ok) {
                    console.log('Energiezähler zurückgesetzt.');
                    fetchData(); // Daten neu laden, um die Anzeige zu aktualisieren
                } else {
                    alert('Fehler beim Zurücksetzen der Energiezähler.');
                }
            })
            .catch(err => {
                console.error('Fetch /reset_energy fehlgeschlagen:', err);
                alert('Fehler bei der Kommunikation mit dem Server.');
            });
        }
    });

    // Initialer Ladevorgang
    fetchData();
  </script>
</body>
</html>

)rawliteral";

  server.send(200, "text/html", html);
}

void handleData() {
  // Build a JSON object with your float variables
  String json = "{";
  json += "\"wattH\":"  + String(wattH,  3) + ",";
  json += "\"vrms\":"   + String(vrmsStore, 2) + ",";
  json += "\"arms\":"   + String(armsStore, 3) + ",";
  json += "\"wrms\":"   + String(wrmsStore, 1);
  json += "}";
  
  server.send(200, "application/json", json);
}

// Function to handle energy reset requests
void handleResetEnergy() {
  Serial.println("Resetting energy counters (Joule and Wh)...");
  joule = 0.0;
  wattH = 0.0;
  // Optionally, you can also reset lastUpdateMillis if you want the calculation
  // to behave as if it just started, but it's generally not needed for just resetting accumulated energy.
  // lastUpdateMillis = millis(); 
  Serial.println("Energy counters have been reset.");
  server.send(200, "text/plain", "Energy counters reset successfully.");
}


// Function to handle requests for paths that are not found
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri(); // Get the requested URI
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST"; // Updated to show POST
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message); // Send 404 response
}