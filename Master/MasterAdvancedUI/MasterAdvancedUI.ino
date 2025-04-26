#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h> // Abhängigkeit für ESPAsyncWebServer
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h> // Für die Button-Funktion benötigt

// --- Konfiguration ---
const char *config_ap_ssid = "ESP32-S3-Config"; // Name des Konfigurations-APs
const char *config_ap_password = "password";    // Passwort für Konfigurations-AP (leer lassen für offen)

const int buttonPin = 10;
const int ledPin = 3;

// --- Globale Variablen ---
AsyncWebServer server(80); // Webserver auf Port 80
Preferences preferences;   // Zum Speichern/Lesen im Flash NVS

String saved_ssid = "";
String saved_password = "";

bool is_station_mode = false; // Flag, um den aktuellen Modus zu verfolgen

// --- HTML-Seite (Code2) als Raw String Literal in PROGMEM ---
const char PAGE_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de" class="dark">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Mein lieber Scholli - Konfiguration</title> 
    <script src="https://cdn.tailwindcss.com"></script>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;700&display=swap" rel="stylesheet">
    <style>
        body { font-family: 'Inter', sans-serif; }
        .gradient-dark { background: linear-gradient(to right, #4c1d95, #14b8a6); }
        .gradient-light { background: linear-gradient(to right, #c4b5fd, #99f6e4); }
        .toggle-checkbox:checked + .toggle-label { background-color: #14b8a6; }
        .toggle-checkbox:checked + .toggle-label .toggle-dot {
            transform: translateX(100%);
            background-color: white;
        }
        .toggle-checkbox { display: none; }
        .toggle-label {
            display: block;
            width: 4rem;
            height: 2rem;
            background-color: #4c1d95;
            border-radius: 9999px;
            cursor: pointer;
            position: relative;
            transition: background-color 0.2s ease-in-out;
        }
        .toggle-dot {
            display: block;
            width: 1.75rem;
            height: 1.75rem;
            background-color: white;
            border-radius: 50%;
            position: absolute;
            top: 0.125rem;
            left: 0.125rem;
            transition: transform 0.2s ease-in-out;
        }
        /* Minimale Anpassungen für Konfigurationsseite */
    </style>
</head>
<body class="min-h-screen flex flex-col transition-colors duration-300 dark:bg-gray-900 dark:text-white bg-gray-100 text-gray-900">

    <header class="p-6 shadow-md dark:gradient-dark gradient-light text-white dark:text-white text-gray-800">
        <div class="container mx-auto flex justify-between items-center">
            <h1 class="text-3xl font-bold">ESP32 WiFi Konfiguration</h1>
             <div class="flex items-center space-x-2">
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" class="w-6 h-6 dark:text-yellow-300 text-gray-700"><path stroke-linecap="round" stroke-linejoin="round" d="M21.752 15.002A9.72 9.72 0 0 1 18 15.75c-5.385 0-9.75-4.365-9.75-9.75 0-1.33.266-2.597.748-3.752A9.753 9.753 0 0 0 3 11.25C3 16.635 7.365 21 12.75 21a9.753 9.753 0 0 0 9.002-5.998Z" /></svg>
                <input type="checkbox" id="darkModeToggle" class="toggle-checkbox">
                <label for="darkModeToggle" class="toggle-label"><span class="toggle-dot"></span></label>
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" class="w-6 h-6 dark:text-gray-300 text-yellow-500"><path stroke-linecap="round" stroke-linejoin="round" d="M12 3v2.25m6.364.386-1.591 1.591M21 12h-2.25m-.386 6.364-1.591-1.591M12 18.75V21m-6.364-.386 1.591-1.591M3 12h2.25m.386-6.364 1.591 1.591M12 6.375a5.625 5.625 0 1 1-11.25 0 5.625 5.625 0 0 1 11.25 0Z" /></svg>
            </div>
        </div>
    </header>

    <main class="container mx-auto p-6 flex-grow">
         <div class="dark:bg-gray-800 bg-white p-6 rounded-lg shadow-md mb-6">
            <h2 class="text-xl font-semibold mb-4 dark:text-gray-200 text-gray-800">WiFi Einstellungen</h2>
            <form method="POST" action="/save" class="space-y-4">
                <div>
                    <label for="ssid" class="block mb-1 dark:text-gray-200 text-gray-800">Netzwerkname (SSID)</label>
                    <input id="ssid" name="ssid" type="text" placeholder="Deine SSID" required class="w-full dark:bg-gray-700 bg-gray-100 dark:text-white text-gray-900 border border-gray-300 dark:border-gray-600 rounded p-2 focus:outline-none focus:ring-2 focus:ring-teal-500">
                </div>
                <div>
                    <label for="password" class="block mb-1 dark:text-gray-200 text-gray-800">Passwort</label>
                    <input id="password" name="password" type="password" placeholder="Dein Passwort" class="w-full dark:bg-gray-700 bg-gray-100 dark:text-white text-gray-900 border border-gray-300 dark:border-gray-600 rounded p-2 focus:outline-none focus:ring-2 focus:ring-teal-500">
                </div>
                <button type="submit" class="px-4 py-2 bg-teal-500 hover:bg-teal-600 dark:bg-teal-400 dark:hover:bg-teal-500 text-white rounded transition">Speichern & Neu starten</button>
            </form>
        </div>

        <div class="mt-6 text-center dark:text-gray-400 text-gray-600">
             Verbinden Sie sich mit dem WLAN "$config_ap_ssid", um die Konfiguration vorzunehmen.<br>
             Öffnen Sie im Browser http://192.168.4.1 oder http://esp32-s3-config.local
        </div>

    </main>
    
    <footer class="p-6 mt-8 dark:bg-gray-800 bg-gray-200 shadow-inner">
        <div class="container mx-auto text-center dark:text-gray-400 text-gray-600 text-sm">
             &copy; 2025 - ESP32 Konfiguration
        </div>
    </footer>

     <script>
        // JavaScript für den Dark/Light Mode Toggle (vereinfacht für die Config-Seite)
        const toggle = document.getElementById('darkModeToggle');
        const htmlElement = document.documentElement;
        const bodyElement = document.body;
        const headerElement = document.querySelector('header');

        function switchTheme(isDarkMode) {
            if (isDarkMode) {
                htmlElement.classList.add('dark');
                bodyElement.classList.remove('bg-gray-100', 'text-gray-900');
                bodyElement.classList.add('dark:bg-gray-900', 'dark:text-white');
                headerElement.classList.remove('gradient-light', 'text-gray-800');
                headerElement.classList.add('dark:gradient-dark', 'dark:text-white');
                 localStorage.setItem('theme', 'dark');
            } else {
                htmlElement.classList.remove('dark');
                bodyElement.classList.remove('dark:bg-gray-900', 'dark:text-white');
                bodyElement.classList.add('bg-gray-100', 'text-gray-900');
                headerElement.classList.remove('dark:gradient-dark', 'dark:text-white');
                headerElement.classList.add('gradient-light', 'text-gray-800');
                localStorage.setItem('theme', 'light');
            }
        }

        toggle.addEventListener('change', () => {
            switchTheme(!toggle.checked); // Umgekehrte Logik, da checked = light mode bedeutet
        });

        const savedTheme = localStorage.getItem('theme');
        const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;

        if (savedTheme) {
            const isDark = savedTheme === 'dark';
            toggle.checked = !isDark;
            switchTheme(isDark);
        } else {
             toggle.checked = !prefersDark;
             switchTheme(prefersDark);
        }
    </script>
</body>
</html>
)rawliteral";


// --- Funktionen ---

// LED-Blinken beim Start
void startupBlinkLED() {
  for (int i = 0; i < 5; i++) { // Weniger Blinken, um Start zu beschleunigen
    digitalWrite(ledPin, HIGH);
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(100);
  }
}

// Startet den Konfigurations-Access-Point und Webserver
void startConfigAP() {
    Serial.println("Keine gespeicherten WiFi-Daten gefunden oder Verbindung fehlgeschlagen.");
    Serial.println("Starte Konfigurations-Access-Point...");
    WiFi.softAP(config_ap_ssid, config_ap_password);
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP IP Adresse: ");
    Serial.println(apIP);

    // mDNS für leichtere Erreichbarkeit im AP-Modus starten
    if (MDNS.begin("esp32-s3-config")) { // Hostname für mDNS
        Serial.println("mDNS Responder gestartet. Erreichbar unter http://esp32-s3-config.local");
        MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("Fehler beim Starten des mDNS Responders!");
    }

    // Handler für die Webseite definieren
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println("Anfrage für / empfangen.");
        request->send_P(200, "text/html", PAGE_html); // Sendet die HTML-Seite aus PROGMEM
    });

    // Handler zum Speichern der WiFi-Daten
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        String newSsid;
        String newPassword;
        bool success = true;

        if (request->hasParam("ssid", true)) { // true = POST Parameter
            newSsid = request->getParam("ssid", true)->value();
            Serial.print("Empfangene SSID: ");
            Serial.println(newSsid);
        } else {
             Serial.println("SSID Parameter fehlt!");
             success = false;
        }

        if (request->hasParam("password", true)) {
            newPassword = request->getParam("password", true)->value();
            // Passwort nicht im Klartext loggen aus Sicherheitsgründen
            Serial.println("Passwort empfangen.");
        } else {
             Serial.println("Passwort Parameter fehlt!");
             success = false; // Passwort kann auch leer sein, aber der Parameter sollte da sein
        }

        if(success && !newSsid.isEmpty()) { // SSID darf nicht leer sein
            preferences.begin("wifi-config", false); // Namespace öffnen (read/write)
            preferences.putString("ssid", newSsid);
            preferences.putString("password", newPassword);
            preferences.end(); // Wichtig: Änderungen speichern und Namespace schließen

            Serial.println("WiFi-Daten gespeichert.");
            String response = "<html><head><title>Gespeichert</title><meta http-equiv='refresh' content='5;url=/'></head><body>";
            response += "<h1>Daten gespeichert!</h1><p>Der ESP32 wird in 5 Sekunden neu gestartet und versucht, sich mit dem neuen Netzwerk zu verbinden.</p>";
             response += "<p>SSID: " + newSsid + "</p></body></html>";
            request->send(200, "text/html", response);

            delay(5000); // Kurz warten, damit der Browser die Antwort verarbeiten kann
            ESP.restart(); // Neustart, um die neuen Daten zu verwenden
        } else {
             Serial.println("Fehler beim Speichern der Daten.");
             request->send(400, "text/plain", "Fehler: SSID oder Passwort fehlen oder SSID ist leer.");
        }
    });

    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Nicht gefunden");
    });

    server.begin(); // Webserver starten
    Serial.println("HTTP-Server gestartet. Warte auf Konfiguration...");
    is_station_mode = false;
}


void setup() {
    Serial.begin(115200);
    Serial.println("\n\n--- ESP32 Start ---");

    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW); // LED initial aus

    startupBlinkLED(); // Visuelles Feedback beim Start

    // Versuche, gespeicherte WiFi-Daten zu laden
    preferences.begin("wifi-config", true); // Namespace öffnen (read-only)
    saved_ssid = preferences.getString("ssid", "");
    saved_password = preferences.getString("password", "");
    preferences.end(); // Namespace schließen

    if (saved_ssid.length() > 0) {
        Serial.print("Gespeicherte SSID gefunden: ");
        Serial.println(saved_ssid);
        Serial.println("Versuche Verbindung zum WLAN herzustellen...");

        WiFi.mode(WIFI_STA); // In den Station-Modus wechseln
        WiFi.begin(saved_ssid.c_str(), saved_password.c_str());

        // Warte auf Verbindung (mit Timeout)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) { // ca. 15 Sekunden Timeout
            delay(500);
            Serial.print(".");
            attempts++;
            digitalWrite(ledPin, !digitalRead(ledPin)); // LED blinken lassen während Verbindungsversuch
        }
        digitalWrite(ledPin, LOW); // LED aus nach Versuch

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi verbunden!");
            Serial.print("IP Adresse: ");
            Serial.println(WiFi.localIP());
            digitalWrite(ledPin, HIGH); // LED anlassen als Zeichen für erfolgreiche Verbindung
            is_station_mode = true;
             // Hier könnte mDNS für den Station-Modus gestartet werden, falls gewünscht
             // if (MDNS.begin("mein-esp32")) { ... }
        } else {
            Serial.println("\nVerbindung fehlgeschlagen.");
             WiFi.disconnect(true); // Verbindung trennen und Daten löschen
             WiFi.mode(WIFI_OFF); // WiFi vor AP-Start ausschalten
             startConfigAP(); // Fallback zum Konfigurations-AP
        }
    } else {
        // Keine SSID gespeichert -> Starte Konfigurations-AP
        startConfigAP();
    }
}

void loop() {
    // Die AsyncWebServer Bibliothek benötigt kein explizites Handling im Loop.

    // Button-Funktionalität nur ausführen, wenn im Station-Modus (verbunden)
    if (is_station_mode) {
        if (digitalRead(buttonPin) == LOW) {
            Serial.println("Taster gedrückt (Station Mode)!");

            // Sicherstellen, dass WiFi noch verbunden ist
            if(WiFi.status() == WL_CONNECTED) {
                HTTPClient http;
                bool success1 = false;
                bool success2 = false;

                Serial.println("Sende Anfrage an esp-slaveTotalWattage.local...");
                http.begin("http://esp-slaveTotalWattage.local/toggleLED"); // Hostnamen oder IP verwenden
                int httpResponseCode1 = http.GET();
                if (httpResponseCode1 > 0) {
                    Serial.printf("Antwortcode TotalWattage: %d\n", httpResponseCode1);
                    success1 = true;
                } else {
                    Serial.printf("Fehler TotalWattage: %s\n", http.errorToString(httpResponseCode1).c_str());
                }
                http.end(); // Verbindung freigeben

                delay(100); // Kurze Pause zwischen Anfragen

                Serial.println("Sende Anfrage an esp-slaveCurrentWattage.local...");
                http.begin("http://esp-slaveCurrentWattage.local/toggleLED"); // Hostnamen oder IP verwenden
                int httpResponseCode2 = http.GET();
                 if (httpResponseCode2 > 0) {
                    Serial.printf("Antwortcode CurrentWattage: %d\n", httpResponseCode2);
                    success2 = true;
                } else {
                    Serial.printf("Fehler CurrentWattage: %s\n", http.errorToString(httpResponseCode2).c_str());
                }
                http.end(); // Verbindung freigeben

                 // Kurzes Blinken zur Bestätigung, falls erfolgreich
                 if(success1 || success2) {
                    digitalWrite(ledPin, LOW); delay(50); digitalWrite(ledPin, HIGH); delay(50);
                    digitalWrite(ledPin, LOW); delay(50); digitalWrite(ledPin, HIGH);
                 }

            } else {
                 Serial.println("WiFi nicht verbunden. Kann keine HTTP-Anfrage senden.");
                 // Optional: Versuche, die Verbindung wiederherzustellen oder gehe in einen Fehlerzustand
                 // ESP.restart(); // Z.B. Neustart versuchen
                 is_station_mode = false; // Gehe davon aus, dass die Verbindung verloren ist
                 digitalWrite(ledPin, LOW); // LED aus
                 // Hier könnte man auch wieder versuchen, den AP zu starten
                 // startConfigAP();
            }
             delay(500); // Entprellen des Tasters
        }
    } else {
         // Im AP-Modus läuft der Webserver im Hintergrund.
         // Hier könnte man die LED blinken lassen, um den AP-Modus anzuzeigen.
         // digitalWrite(ledPin, !digitalRead(ledPin));
         // delay(1000);
    }

     // MDNS muss im Loop aktualisiert werden, wenn es im Station-Modus verwendet wird
     // if (is_station_mode) {
     //     MDNS.update();
     // }
}