#include <WiFi.h>
// #include <HTTPClient.h> // Not needed for serving a page
#include <WebServer.h> // <<< Added WebServer library
#include <ESPmDNS.h>
#include <Arduino.h>
#include <Preferences.h> // <<< Added for flash storage

extern "C" {
#include "driver/spi_slave.h"
}

// === Pin‑Definitionen ===
#define PIN_SPI_MISO 11  // nicht verwendet
#define PIN_SPI_MOSI 12  // Data In für den Slave (DIN)
#define PIN_SPI_SCLK 10  // Clock
#define PIN_SPI_CS 2
#define RXBUF_SIZE 256

// === WiFi Credentials (Default fallback) ===
String default_ssid = "Lieber-Scholli";
String default_password = "12345678";
String device_hostname = "scholli"; // <<< Added hostname for easy identification

// === Current WiFi Credentials ===
String current_ssid = "";
String current_password = "";
bool wifi_client_mode = false; // Track if we're in client mode

// === Preferences object for flash storage ===
Preferences preferences;

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
void handleData();
void handleResetEnergy();
void handleSaveWiFi(); // <<< Added prototype for WiFi save handler
void loadWiFiCredentials(); // <<< Added prototype for loading WiFi credentials
void saveWiFiCredentials(const String& ssid, const String& password); // <<< Added prototype for saving WiFi credentials
void connectToWiFi(); // <<< Added prototype for WiFi connection

// === Setup Function ===
void setup() {
  Serial.begin(9600);
  Serial.println("Startup loading...");
  delay(100); // Short delay for stability

  // --- Load WiFi credentials from flash ---
  loadWiFiCredentials();

  // --- WiFi Setup ---
  connectToWiFi();

  // --- mDNS Setup (optional, access via http://scholli-master.local/) ---
  if (MDNS.begin(device_hostname.c_str())) { // Use the hostname variable
    Serial.println("MDNS responder started");
    Serial.println("Access device via: http://" + device_hostname + ".local/");
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
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/reset_energy", HTTP_POST, handleResetEnergy);
  server.on("/save_wifi", HTTP_POST, handleSaveWiFi); // <<< Added WiFi save handler
  server.onNotFound(handleNotFound);
  server.begin(); // Start the web server
  Serial.println("HTTP server started");

  lastUpdateMillis = millis(); // Initialize timing for energy calculation
}

// === WiFi Functions ===

void loadWiFiCredentials() {
  preferences.begin("wifi", false); // Open preferences in read-write mode
  
  current_ssid = preferences.getString("ssid", "");
  current_password = preferences.getString("password", "");
  
  preferences.end();
  
  Serial.println("WiFi credentials loaded from flash:");
  Serial.println("SSID: " + (current_ssid.length() > 0 ? current_ssid : String("not set")));
  Serial.println("Password: " + (current_password.length() > 0 ? String("set") : String("not set")));
}

void saveWiFiCredentials(const String& ssid, const String& password) {
  preferences.begin("wifi", false); // Open preferences in read-write mode
  
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  
  preferences.end();
  
  Serial.println("WiFi credentials saved to flash:");
  Serial.println("SSID: " + ssid);
  Serial.println("Password: set");
}

void connectToWiFi() {
  // Set hostname for easier identification
  WiFi.setHostname(device_hostname.c_str());
  
  // Try to connect to saved WiFi credentials first
  if (current_ssid.length() > 0 && current_password.length() > 0) {
    Serial.println("Attempting to connect to saved WiFi: " + current_ssid);
    WiFi.begin(current_ssid.c_str(), current_password.c_str());
    
    // Wait up to 10 seconds for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      wifi_client_mode = true;
      Serial.println();
      Serial.println("WiFi connected successfully!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("Hostname: ");
      Serial.println(device_hostname);
      return;
    } else {
      Serial.println();
      Serial.println("Failed to connect to saved WiFi. Starting Access Point...");
    }
  }
  
  // Fall back to Access Point mode
  wifi_client_mode = false;
  Serial.print("Starting Access Point: ");
  Serial.println(default_ssid);
  WiFi.softAP(default_ssid.c_str(), default_password.c_str());
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
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
    .status-indicator {
      display: inline-block;
      padding: 0.25rem 0.5rem;
      border-radius: 12px;
      font-size: 0.8rem;
      font-weight: bold;
      margin-left: 0.5rem;
    }
    .status-ap {
      background-color: #fbbf24;
      color: #92400e;
    }
    .status-client {
      background-color: #34d399;
      color: #065f46;
    }
    .wifi-info {
      font-size: 0.9rem;
      color: #6b7280;
      margin-top: 0.25rem;
    }
    .wifi-info a {
      color: #7c3aed;
      text-decoration: none;
    }
    .wifi-info a:hover {
      text-decoration: underline;
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
    button:disabled {
        background: #94a3b8;
        cursor: not-allowed;
    }
    .reset-button-container { /* Container for the reset button */
        margin-top: 1.5rem; /* Space above the button */
        text-align: center; /* Center the button */
    }
    .message {
      padding: 0.75rem;
      border-radius: 4px;
      margin-top: 0.5rem;
      display: none;
    }
    .message.success {
      background-color: #d1fae5;
      color: #065f46;
      border: 1px solid #34d399;
    }
    .message.error {
      background-color: #fee2e2;
      color: #991b1b;
      border: 1px solid #f87171;
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
    <div id="wifi-status"></div>
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
      <summary>WiFi Einstellungen</summary>
      <form id="wifiForm">
        <div>
          <label for="ssid_input">SSID</label> 
          <input id="ssid_input" type="text" placeholder="Deine SSID" required>
        </div>
        <div>
          <label for="password_input">Passwort</label> 
          <input id="password_input" type="password" placeholder="Dein Passwort" required>
        </div>
        <button type="submit" id="saveWifiButton">Speichern</button>
        <div id="wifiMessage" class="message"></div>
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
          
          // Update WiFi status
          if (d.wifi_mode) {
            const statusText = d.wifi_mode === 'client' ? 'Verbunden' : 'Access Point';
            const statusClass = 'status-' + d.wifi_mode;
            
            let wifiStatusHtml = 'WiFi: <span class="status-indicator ' + statusClass + '">' + statusText + '</span>';
            
            // Add IP and hostname info
            if (d.ip) {
              wifiStatusHtml += '<div class="wifi-info">IP: ' + d.ip;
              if (d.hostname && d.wifi_mode === 'client') {
                wifiStatusHtml += ' | <a href="http://' + d.hostname + '.local/" target="_blank">' + d.hostname + '.local</a>';
              }
              wifiStatusHtml += '</div>';
            }
            
            document.getElementById('wifi-status').innerHTML = wifiStatusHtml;
          }
        })
        .catch(err => console.error('Fetch /data fehlgeschlagen:', err));
    }

    document.getElementById('resetEnergyButton').addEventListener('click', function() {
        if (confirm('Möchten Sie die Energiezähler (Wh und Joule) wirklich zurücksetzen?')) {
            fetch('/reset_energy', {
                method: 'POST'
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

    // WiFi form handling
    document.getElementById('wifiForm').addEventListener('submit', function(e) {
        e.preventDefault();
        
        const ssid = document.getElementById('ssid_input').value;
        const password = document.getElementById('password_input').value;
        const messageDiv = document.getElementById('wifiMessage');
        const saveButton = document.getElementById('saveWifiButton');
        
        if (!ssid || !password) {
            showMessage('Bitte füllen Sie beide Felder aus.', 'error');
            return;
        }
        
        saveButton.disabled = true;
        saveButton.textContent = 'Speichern...';
        
        const formData = new FormData();
        formData.append('ssid', ssid);
        formData.append('password', password);
        
        fetch('/save_wifi', {
            method: 'POST',
            body: formData
        })
        .then(res => res.text())
        .then(data => {
            showMessage('WiFi-Einstellungen gespeichert! Das Gerät wird sich neu verbinden.', 'success');
            document.getElementById('ssid_input').value = '';
            document.getElementById('password_input').value = '';
            
            // Inform user about potential reconnection and show current status
            setTimeout(() => {
                showMessage('Das Gerät versucht sich mit dem neuen WiFi zu verbinden. Die Verbindungsinformationen werden automatisch aktualisiert.', 'success');
            }, 2000);
            
            // Force immediate data refresh to show updated connection info
            setTimeout(() => {
                fetchData();
            }, 3000);
        })
        .catch(err => {
            console.error('WiFi save error:', err);
            showMessage('Fehler beim Speichern der WiFi-Einstellungen.', 'error');
        })
        .finally(() => {
            saveButton.disabled = false;
            saveButton.textContent = 'Speichern';
        });
    });
    
    function showMessage(text, type) {
        const messageDiv = document.getElementById('wifiMessage');
        messageDiv.textContent = text;
        messageDiv.className = 'message ' + type;
        messageDiv.style.display = 'block';
        
        setTimeout(() => {
            messageDiv.style.display = 'none';
        }, 5000);
    }

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
  json += "\"wrms\":"   + String(wrmsStore, 1) + ",";
  json += "\"wifi_mode\":\"" + String(wifi_client_mode ? "client" : "ap") + "\",";
  json += "\"ip\":\"" + (wifi_client_mode ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
  json += "\"hostname\":\"" + device_hostname + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

// Function to handle energy reset requests
void handleResetEnergy() {
  Serial.println("Resetting energy counters (Joule and Wh)...");
  joule = 0.0;
  wattH = 0.0;
  Serial.println("Energy counters have been reset.");
  server.send(200, "text/plain", "Energy counters reset successfully.");
}

// <<< New function to handle WiFi credential saving
void handleSaveWiFi() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  String new_ssid = server.arg("ssid");
  String new_password = server.arg("password");
  
  if (new_ssid.length() == 0 || new_password.length() == 0) {
    server.send(400, "text/plain", "SSID and password are required");
    return;
  }
  
  // Save credentials to flash
  saveWiFiCredentials(new_ssid, new_password);
  
  // Update current credentials
  current_ssid = new_ssid;
  current_password = new_password;
  
  server.send(200, "text/plain", "WiFi credentials saved successfully");
  
  // Attempt to reconnect in the background (non-blocking)
  Serial.println("Attempting to connect to new WiFi credentials...");
  
  // Small delay then try to connect
  delay(1000);
  
  WiFi.disconnect();
  delay(1000);
  
  connectToWiFi();
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