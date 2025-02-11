/*
  Roaming Library - A NodeMCU-based File Server
  Created for portable file sharing with a retro aesthetic
  Includes:
  - Open WiFi Access Point
  - Captive Portal
  - SD Card file serving
  - Support for PDF, EPUB, DOC, RTF, and TXT files
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <SD.h>

// Access Point settings
const char* AP_SSID = "R04M1NG_L1BR4RY";  // Name of the WiFi network
IPAddress apIP(192, 168, 4, 1);        // IP address of the NodeMCU in AP mode
const byte DNS_PORT = 53;              // DNS server port

// Create web server object
ESP8266WebServer server(80);
DNSServer dnsServer;

// LED pin
const int ledPin = LED_BUILTIN;
bool ledState = false;

// SD card CS pin
const int SD_CS_PIN = D8;

// Function declarations
void handleRoot();
void handleToggle();
void handleFileList();
void handleFileDownload();
void handleNotFound();
void handleCaptivePortal();

// Function to check if file is allowed
bool isAllowedFile(const String& filename) {
    String lowerFilename = filename;
    lowerFilename.toLowerCase();
    return lowerFilename.endsWith(".pdf") || 
           lowerFilename.endsWith(".epub") || 
           lowerFilename.endsWith(".doc") || 
           lowerFilename.endsWith(".rtf") ||
           lowerFilename.endsWith(".txt");
}

// Captive Portal Detection
bool captivePortal() {
    if (!isIp(server.hostHeader())) {
        server.sendHeader("Location", String("http://") + toStringIp(apIP), true);
        server.send(302, "text/plain", "");
        server.client().stop();
        return true;
    }
    return false;
}

bool isIp(String str) {
    for (size_t i = 0; i < str.length(); i++) {
        int c = str.charAt(i);
        if (c != '.' && (c < '0' || c > '9')) {
            return false;
        }
    }
    return true;
}

String toStringIp(IPAddress ip) {
    String res = "";
    for (int i = 0; i < 3; i++) {
        res += String((ip >> (8 * i)) & 0xFF) + ".";
    }
    res += String(((ip >> 8 * 3)) & 0xFF);
    return res;
}

void setup() {
    // Start Serial for debugging
    Serial.begin(115200);
    delay(100);
    Serial.println("\nStarting setup...");
    
    // Initialize LED pin
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
    
    // Initialize SD card
    Serial.print("Initializing SD card...");
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
    } else {
        Serial.println("SD card initialization done.");
    }
    
    // Set up Access Point
    Serial.println("Configuring access point...");
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);
    bool apConfigSuccess = WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    Serial.println(apConfigSuccess ? "AP Config Success" : "AP Config Failed!");
    
    // Start AP without password
    bool apStartSuccess = WiFi.softAP(AP_SSID, NULL, 1);
    Serial.println(apStartSuccess ? "AP Start Success" : "AP Start Failed!");
    
    if (apStartSuccess) {
        Serial.println("Access Point Started Successfully");
        Serial.printf("SSID: %s\n", AP_SSID);
        Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
    }
    
    // Set up DNS server for captive portal
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);  // Redirect all DNS requests to our IP
    
    // Add server routes
    server.on("/", handleRoot);
    server.on("/toggle", handleToggle);
    server.on("/list", handleFileList);
    server.on("/download", handleFileDownload);
    server.on("/generate_204", handleRoot);  // Android captive portal
    server.on("/fwlink", handleRoot);       // Microsoft captive portal
    server.onNotFound(handleCaptivePortal);  // Catch-all handler
    
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
}

void handleCaptivePortal() {
    if (captivePortal()) {
        return;
    }
    handleNotFound();
}

void handleRoot() {
    // Add headers for captive portal
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
   html += "<style>";
    html += "body { ";
    html += "    font-family: 'Courier New', monospace; ";
    html += "    background-color: #000; ";
    html += "    color: #0f0; ";
    html += "    margin: 20px; ";
    html += "    line-height: 1.6; ";
    html += "}";
    html += "h1, h2 { ";
    html += "    color: #0f0; ";
    html += "    text-shadow: 0 0 5px #0f0; ";
    html += "    text-transform: uppercase; ";
    html += "    text-align: center; ";
    html += "}";
    html += "a { ";
    html += "    color: #0f0; ";
    html += "    text-decoration: none; ";
    html += "}";
    html += "a:hover { ";
    html += "    color: #fff; ";
    html += "    text-shadow: 0 0 10px #0f0; ";
    html += "}";
    html += ".container { ";
    html += "    border: 1px solid #0f0; ";
    html += "    padding: 20px; ";
    html += "    margin: 10px 0; ";
    html += "    box-shadow: 0 0 10px #0f0; ";
    html += "}";
    html += ".status { ";
    html += "    border-left: 3px solid #0f0; ";
    html += "    padding-left: 10px; ";
    html += "    margin: 10px 0; ";
    html += "    text-align: center; ";
    html += "}";
    html += ".pixelart { ";
    html += "    width: 150px; ";
    html += "    height: 150px; ";
    html += "    margin: 20px auto; ";
    html += "    display: block; ";
    html += "    animation: glow 2s ease-in-out infinite alternate; ";
    html += "}";
    html += ".goat-png { ";
    html += "    width: 150px; ";
    html += "    height: 150px; ";
    html += "    margin: 20px auto; ";
    html += "    display: block; ";
    html += "    filter: hue-rotate(90deg) brightness(200%); ";
    html += "}";
    html += ".access-link { ";
    html += "    margin: 20px 0; ";
    html += "    font-size: 1.2em; ";
    html += "}";
    html += ".glitch-wrapper { ";
    html += "    padding: 20px; ";
    html += "    text-align: center; ";
    html += "    margin-bottom: 20px; ";
    html += "}";
    html += ".glitch { ";
    html += "    font-size: 2em; ";
    html += "    font-weight: bold; ";
    html += "    text-transform: uppercase; ";
    html += "    position: relative; ";
    html += "    text-shadow: 0.05em 0 0 #00fffc, -0.03em -0.04em 0 #fc00ff, 0.025em 0.04em 0 #fffc00; ";
    html += "    animation: glitch 725ms infinite; ";
    html += "}";
    html += ".glitch span { ";
    html += "    position: absolute; ";
    html += "    top: 0; ";
    html += "    left: 0; ";
    html += "}";
    html += ".glitch span:first-child { ";
    html += "    animation: glitch 500ms infinite; ";
    html += "    clip-path: polygon(0 0, 100% 0, 100% 35%, 0 35%); ";
    html += "    transform: translate(-0.04em, -0.03em); ";
    html += "    opacity: 0.75; ";
    html += "}";
    html += ".glitch span:last-child { ";
    html += "    animation: glitch 375ms infinite; ";
    html += "    clip-path: polygon(0 65%, 100% 65%, 100% 100%, 0 100%); ";
    html += "    transform: translate(0.04em, 0.03em); ";
    html += "    opacity: 0.75; ";
    html += "}";
    html += "@keyframes glow { ";
    html += "    from { filter: drop-shadow(0 0 5px #0f0); } ";
    html += "    to { filter: drop-shadow(0 0 10px #0f0); } ";
    html += "}";
    html += "@keyframes glitch { ";
    html += "    0% { text-shadow: 0.05em 0 0 #00fffc, -0.03em -0.04em 0 #fc00ff, 0.025em 0.04em 0 #fffc00; }";
    html += "    15% { text-shadow: 0.05em 0 0 #00fffc, -0.03em -0.04em 0 #fc00ff, 0.025em 0.04em 0 #fffc00; }";
    html += "    16% { text-shadow: -0.05em -0.025em 0 #00fffc, 0.025em 0.035em 0 #fc00ff, -0.05em -0.05em 0 #fffc00; }";
    html += "    49% { text-shadow: -0.05em -0.025em 0 #00fffc, 0.025em 0.035em 0 #fc00ff, -0.05em -0.05em 0 #fffc00; }";
    html += "    50% { text-shadow: 0.05em 0.035em 0 #00fffc, 0.03em 0 0 #fc00ff, 0 -0.04em 0 #fffc00; }";
    html += "    99% { text-shadow: 0.05em 0.035em 0 #00fffc, 0.03em 0 0 #fc00ff, 0 -0.04em 0 #fffc00; }";
    html += "    100% { text-shadow: -0.05em 0 0 #00fffc, -0.025em -0.04em 0 #fc00ff, -0.04em -0.025em 0 #fffc00; }";
    html += "}";
    html += "</style></head><body>";

    html += "<div class='glitch-wrapper'>";
    html += "<div class='glitch'>7H3 R04M1NG L1BR4RY";
    //html += "<span>7H3 R04M1NG L1BR4RY</span>";
    html += "</div>";
    html += "</div>";
  
    // Add the goat
    html += "<svg class='pixelart' viewBox='0 0 32 32'>";
    html += "<style>";
    html += ".pixel { fill: #0f0; filter: drop-shadow(0 0 2px #0f0); }";
    html += "</style>";
    html += "<path class='pixel' d='";
    html += "M15 6h2v1h-2zM14 7h4v1h-4zM13 8h6v1h-6z";  // Top of head
    html += "M12 9h2v1h-2zM18 9h2v1h-2z";              // Upper head sides
    html += "M11 10h3v1h-3zM18 10h3v1h-3z";            // Upper head details
    html += "M10 11h4v1h-4zM18 11h4v1h-4z";            // Middle head
    html += "M9 12h6v1h-6zM17 12h6v1h-6z";             // Lower head
    html += "M8 13h16v1h-16zM8 14h16v1h-16z";          // Main head
    html += "M9 15h14v1h-14zM10 16h12v1h-12z";         // Upper jaw
    html += "M11 17h10v1h-10zM12 18h8v1h-8z";          // Lower jaw
    html += "M13 19h6v1h-6zM14 20h4v1h-4z";            // Bottom of jaw
    html += "M15 21h2v1h-2zM15 22h2v1h-2z";            // Point
    html += "'/></svg>";

    html += "<div class='container'>";
    html += "<div class='status'>";
    html += "<p>[ Network: " + String(AP_SSID) + " ]<br>";
    //html += "[ IP: " + WiFi.softAPIP().toString() + " ]<br>";
    //html += "[ Connected Users: " + String(WiFi.softAPgetStationNum()) + " ]<br>";
    html += "[ System Status: " + String(ledState ? "ACTIVE" : "STANDBY") + " ]</p>";
    html += "</div>";

    html += "<div class='status' style='text-align: center;'>";
    html += "<h3>// File System Access //</h3>";
    html += "<p>[ Formats: PDF | EPUB | DOC | RTF | TXT ]</p>";
    html += "<div class='access-link'>";  // New container for the link
    html += "<a href='/list'>&gt; Access Files &lt;</a>";
    html += "</div>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

void handleFileList() {
    // Add headers for captive portal
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += ".container { border: 1px solid #0f0; padding: 20px; margin: 10px 0; box-shadow: 0 0 10px #0f0; }";
    html += ".file-list { list-style: none; padding: 0; }";
    html += ".file-item { border-left: 3px solid #0f0; padding: 10px; margin: 10px 0; }";
    html += ".file-item:hover { background-color: #001100; }";
    html += ".file-size { color: #0a0; font-size: 0.9em; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h2>//System Files//</h2>";

    html += "<ul class='file-list'>";
    
    File root = SD.open("/");
    if (!root) {
        html += "<p style='color: #f00;'>[ERROR] Failed to access storage system</p>";
    } else {
        bool filesFound = false;
        while (File file = root.openNextFile()) {
            String fileName = String(file.name());
            if (isAllowedFile(fileName)) {
                filesFound = true;
                html += "<li class='file-item'>";
                html += "<a href='/download?file=" + fileName + "'>&gt; " + fileName + " &lt;</a>";
                html += "<div class='file-size'>[Size: " + String(file.size() / 1024.0, 1) + " KB]</div>";
                html += "</li>";
            }
            file.close();
        }
        if (!filesFound) {
            html += "<p style='color: #f00;'>[WARNING] No documents found in system</p>";
        }
        root.close();
    }
    
    html += "</ul>";
    html += "<br><a href='/'>&lt;&lt; Return to Terminal &gt;&gt;</a>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
}

void handleFileDownload() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "File parameter missing");
        return;
    }
    
    String fileName = server.arg("file");
    
    if (!isAllowedFile(fileName)) {
        server.send(400, "text/plain", "Invalid file type. Only PDF, EPUB, DOC, RTF, and TXT files are allowed.");
        return;
    }
    
    if (!SD.exists(fileName)) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    File file = SD.open(fileName, FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Failed to open file");
        return;
    }
    
    String contentType = "application/octet-stream";
    if (fileName.endsWith(".pdf")) {
        contentType = "application/pdf";
    } else if (fileName.endsWith(".epub")) {
        contentType = "application/epub+zip";
    } else if (fileName.endsWith(".doc")) {
        contentType = "application/msword";
    } else if (fileName.endsWith(".rtf")) {
        contentType = "application/rtf";
    } else if (fileName.endsWith(".txt")) {
        contentType = "text/plain";
    }
    
    server.sendHeader("Content-Disposition", "attachment; filename=" + fileName);
    server.sendHeader("Connection", "close");
    server.streamFile(file, contentType);
    file.close();
}

void handleToggle() {
    ledState = !ledState;
    digitalWrite(ledPin, ledState);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    
    server.send(404, "text/plain", message);
}