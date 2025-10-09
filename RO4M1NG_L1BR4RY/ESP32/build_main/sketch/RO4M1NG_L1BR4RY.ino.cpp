#include <Arduino.h>
#line 1 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
#include <DNSServer.h>
#include <SPI.h>
#include <SD.h>

#include "src/includes/utility/Sd2Card.h"
#define HAS_SD2CARD_HELPER 1

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#else
#error "Unsupported architecture"
#endif
#include <map>
#include <vector>
#include <algorithm>

// Global Variables

// LED pin
const int ledPin = LED_BUILTIN;
bool ledState = false;

// Access Point settings
const char* serverName = "r04m1ng.l1br4ry";  // DNS name for captive portal
//const char* AP_SSID = "PR0J3K7_B00KM4RK";  // Name of the WiFi network
const char* AP_SSID_BASE = "PR0J3K7_B00KM4RK_";  // Base name for SSID
String AP_SSID;  // Full name with number
IPAddress apIP(192, 168, 4, 1);        // IP address of the NodeMCU in AP mode
const byte DNS_PORT = 53;              // DNS server port

#if defined(ARDUINO_ARCH_ESP32)
WebServer server(80);
#else
ESP8266WebServer server(80);
#endif
DNSServer dnsServer;

// SD card CS pin
#if defined(ARDUINO_ARCH_ESP32)
const int SD_CS_PIN = 10;
#else
const int SD_CS_PIN = D8;
#endif

// File Uploads
File uploadFile;

bool sdCardReady = false;

// Forum cleanup settings
unsigned long lastCleanupTime = 0;
const unsigned long CLEANUP_INTERVAL = 3600000; // 1 hour in milliseconds

// Forum structures
struct ForumPost {
  String id;
  String author;
  String content;
  String timestamp;
};

struct ForumThread {
  String id;
  String title;
  String author;
  String timestamp;
};

// Function declarations
void handleRoot();
void handleToggle();
void handleFileList();
void handleFileDownload();
void handleNotFound();
void handleCaptivePortal();
void handleForum();
void handleNewThread();
void handleThread();
void handleNewPost();
void checkAndCleanupForum();
void cleanupForum();
void removeDirectory(const char * path);
bool initializeSdCard();
bool ensureSdDirectory(const char* path, const char* description);
bool ensureSdFile(const char* path, const char* description, const char* defaultContent);
void finalizeChunkedResponse();

struct LibraryFileEntry {
  String path;
  size_t size;
};

void collectLibraryFiles(const String& dirPath, std::vector<LibraryFileEntry>& files);
String humanReadableSize(size_t bytes);
String urlEncodePath(const String& path);
String formatTimestamp(unsigned long timestamp);

// Function to check if file is allowed
#line 103 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
bool isAllowedFile(const String& filename);
#line 122 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
bool isIp(String str);
#line 132 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
String toStringIp(IPAddress ip);
#line 141 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
bool captivePortal();
#line 402 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void setup();
#line 502 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void loop();
#line 665 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handleRedirect();
#line 788 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handlePortal();
#line 958 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handleNodeFiles();
#line 1208 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handleUploadPage();
#line 1325 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handleDisclaimer();
#line 1390 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handleFileUpload();
#line 1471 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
bool verifyUploadedFile(String filePath, size_t expectedSize);
#line 1504 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handleUpload();
#line 1751 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
void handleThreadAjax();
#line 103 "/home/wrew/code/ProjectBookmark/RO4M1NG_L1BR4RY/RO4M1NG_L1BR4RY.ino"
bool isAllowedFile(const String& filename) {
  String lowerFilename = filename;
  lowerFilename.toLowerCase();
  return lowerFilename.endsWith(".pdf") ||
         lowerFilename.endsWith(".epub") ||
         lowerFilename.endsWith(".doc") ||
         lowerFilename.endsWith(".docx") ||
         lowerFilename.endsWith(".rtf") ||
         lowerFilename.endsWith(".txt") ||
         lowerFilename.endsWith(".azw") ||
         lowerFilename.endsWith(".mobi") ||
         lowerFilename.endsWith(".lib") ||
         lowerFilename.endsWith(".fb2") ||
         lowerFilename.endsWith(".prc") ||
         lowerFilename.endsWith(".pdb") ||
         lowerFilename.endsWith(".ibook");
}

// Function to check if string is IP address
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

bool captivePortal() {
  if (!isIp(server.hostHeader())) {
    server.sendHeader("Location", String("http://") + toStringIp(apIP), true);
    server.send(302, "text/plain", "");
    return true;
  }
  return false;
}

bool ensureSdDirectory(const char* path, const char* description) {
  Serial.printf("[SD] Checking directory '%s' (%s)\n", path, description);
  if (SD.exists(path)) {
    File entry = SD.open(path);
    if (!entry) {
      Serial.printf("[SD][ERROR] Failed to open existing entry: %s\n", path);
      return false;
    }
    bool isDir = entry.isDirectory();
    entry.close();
    if (isDir) {
      Serial.println("[SD] Directory present");
      return true;
    }
    Serial.println("[SD][WARN] Path exists but is not a directory; removing");
    if (!SD.remove(path)) {
      Serial.println("[SD][ERROR] Failed to remove conflicting entry");
      return false;
    }
  } else {
    Serial.println("[SD] Directory missing; creating");
  }

  if (SD.mkdir(path)) {
    Serial.println("[SD] Directory ready");
    return true;
  }

  Serial.printf("[SD][ERROR] Failed to create directory: %s\n", path);
  return false;
}

bool ensureSdFile(const char* path, const char* description, const char* defaultContent) {
  Serial.printf("[SD] Checking file '%s' (%s)\n", path, description);
  if (SD.exists(path)) {
    File file = SD.open(path, FILE_READ);
    if (!file) {
      Serial.printf("[SD][ERROR] Failed to open existing file: %s\n", path);
      return false;
    }
    size_t size = file.size();
    file.close();
    Serial.printf("[SD] File present (%lu bytes)\n", static_cast<unsigned long>(size));
    if (size > 0) {
      return true;
    }
    Serial.println("[SD][WARN] File is empty; writing default content");
  } else {
    Serial.println("[SD] File missing; creating");
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("[SD][ERROR] Failed to open file for writing: %s\n", path);
    return false;
  }
  if (defaultContent) {
    file.print(defaultContent);
  }
  file.close();
  Serial.println("[SD] File ready");
  return true;
}

void finalizeChunkedResponse() {
#if defined(ARDUINO_ARCH_ESP32)
  server.sendContent("");
#else
  server.chunkedResponseFinalize();
#endif
}

String humanReadableSize(size_t bytes) {
  const char* suffixes[] = {"B", "KB", "MB", "GB"};
  double value = static_cast<double>(bytes);
  size_t suffixIndex = 0;
  while (value >= 1024.0 && suffixIndex < 3) {
    value /= 1024.0;
    ++suffixIndex;
  }
  int decimals = value < 10.0 ? 2 : 1;
  return String(value, decimals) + " " + suffixes[suffixIndex];
}

String urlEncodePath(const String& path) {
  const char hex[] = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(path.length() * 3);
  for (size_t i = 0; i < path.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(path.charAt(i));
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String formatTimestamp(unsigned long timestamp) {
  unsigned long now = millis();
  unsigned long diff = now >= timestamp ? now - timestamp : 0;

  if (diff < 60000UL) {
    return String(diff / 1000UL) + "s ago";
  }
  if (diff < 3600000UL) {
    return String(diff / 60000UL) + "m ago";
  }
  if (diff < 86400000UL) {
    return String(diff / 3600000UL) + "h ago";
  }
  return String(diff / 86400000UL) + "d ago";
}

void collectLibraryFiles(const String& dirPath, std::vector<LibraryFileEntry>& files) {
  File dir = SD.open(dirPath.c_str());
  if (!dir) {
    return;
  }
  if (!dir.isDirectory()) {
    dir.close();
    return;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    String name = entry.name();
    bool isDir = entry.isDirectory();
    size_t entrySize = entry.size();
    String entryPath;
    if (dirPath == "/") {
      entryPath = "/" + name;
    } else if (dirPath.endsWith("/")) {
      entryPath = dirPath + name;
    } else {
      entryPath = dirPath + "/" + name;
    }
    entry.close();

    String lowerPath = entryPath;
    lowerPath.toLowerCase();

    if (isDir) {
      if (lowerPath == "/forum" || lowerPath.startsWith("/forum/")) {
        continue;
      }
      collectLibraryFiles(entryPath, files);
    } else if (isAllowedFile(name)) {
      files.push_back({entryPath, entrySize});
    }
  }

  dir.close();
}

bool initializeSdCard() {
  Serial.println("[SD] ----- Initialization Start -----");
  Serial.printf("[SD] Configured CS pin: %d\n", SD_CS_PIN);
  Serial.println("[SD] Attempting to mount card...");

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

#if defined(ARDUINO_ARCH_ESP32)
  SPI.begin(SCK, MISO, MOSI, SD_CS_PIN);
#else
  SPI.begin();
#endif

  //#if HAS_SD2CARD_HELPER
  //    Serial.println("[SD] Performing low-level card.init verification (SPI_HALF_SPEED)");
  //    Sd2Card card;
  //    if (!card.init(SPI_HALF_SPEED, SD_CS_PIN)) {
  //        Serial.printf("[SD][ERROR] card.init failed (code=%u, data=%u)\n", card.errorCode(), card.errorData());
  //        Serial.println("[SD][HINT] Check wiring, ensure the card is inserted, and verify CS pin wiring");
  //        Serial.println("[SD] ----- Initialization Aborted -----");
  //        return false;
  //   }

  // const uint8_t detectedType = card.type();
  //const char* typeLabel = "Unknown";
  //switch (detectedType) {
  //   case SD_CARD_TYPE_SD1:
  //      typeLabel = "SD1";
  //     break;
  //  case SD_CARD_TYPE_SD2:
  //     typeLabel = "SD2";
  //     break;
  // case SD_CARD_TYPE_SDHC:
  //     typeLabel = "SDHC";
  //     break;
  //  default:
  //      break;
  // }

  //   Serial.printf("[SD] card.init succeeded; detected type: %s\n", typeLabel);
  //#else
  //   Serial.println("[SD][WARN] card.init helper unavailable; skipping low-level verification");
  //#endif

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[SD][ERROR] SD.begin failed; card not available");
    Serial.println("[SD] ----- Initialization Aborted -----");
    return false;
  }

  Serial.println("[SD] Card mounted successfully");

  enum SdRequirementType { SD_REQ_DIRECTORY, SD_REQ_FILE };

  struct SdRequirement {
    const char* path;
    SdRequirementType type;
    const char* description;
    const char* defaultContent;
  };

  const SdRequirement requirements[] = {
    {"/forum", SD_REQ_DIRECTORY, "Forum root directory", nullptr},
    {"/forum/posts", SD_REQ_DIRECTORY, "Forum posts directory", nullptr},
    {"/forum/threads.json", SD_REQ_FILE, "Forum threads index", "[]\n"}
  };

  bool allOk = true;
  for (const auto& requirement : requirements) {
    Serial.printf("[SD] Verifying %s (%s)\n",
                  requirement.path,
                  requirement.type == SD_REQ_DIRECTORY ? "directory" : "file");

    bool result = requirement.type == SD_REQ_DIRECTORY
                  ? ensureSdDirectory(requirement.path, requirement.description)
                  : ensureSdFile(requirement.path, requirement.description, requirement.defaultContent);

    if (!result) {
      allOk = false;
      Serial.printf("[SD][ERROR] Requirement failed: %s\n", requirement.path);
    }
  }

  Serial.println(allOk ? "[SD] All requirements satisfied" : "[SD][WARN] Some requirements failed; review logs");
  Serial.println("[SD] ----- Initialization Complete -----");
  return allOk;
}

void setup() {
  // Start Serial for debugging
  Serial.begin(115200);
  delay(100);
#if defined(ARDUINO_ARCH_ESP32)
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 5000) {
    delay(10);
  }
  if (Serial) {
    Serial.println();
    Serial.println("[SYS] USB serial connected");
  }
#endif
  Serial.println("\nStarting setup...");

  // Initialize LED pin
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Initialize SD card and verify structure
  sdCardReady = initializeSdCard();
  if (!sdCardReady) {
    Serial.println("[SD][WARN] SD card initialization failed; storage features unavailable");
  }

  // Set up Access Point
  randomSeed(analogRead(0));  // Initialize random seed
  int randomNum = random(0, 100);  // Generate random number 0-99
  AP_SSID = String(AP_SSID_BASE) + String(randomNum < 10 ? "0" : "") + String(randomNum);
  Serial.println("Generated SSID: " + AP_SSID);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  // Start AP without password
  bool apStartSuccess = WiFi.softAP(AP_SSID.c_str(), NULL, 1);
  Serial.println(apStartSuccess ? "AP Start Success" : "AP Start Failed!");

  if (apStartSuccess) {
    Serial.println("Access Point Started Successfully");
    Serial.printf("SSID: %s\n", AP_SSID.c_str());
    Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
  }

  // Configure DNS server to redirect all requests
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  // Set up server routes
  server.on("/", handleRoot); //Main library page
  server.on("/toggle", handleToggle);
  server.on("/list", handleFileList);
  server.on("/download", handleFileDownload);
  server.on("/forum", handleForum);
  server.on("/forum/new", handleNewThread);
  server.on("/forum/thread", handleThread);
  server.on("/forum/post", handleNewPost);
  server.on("/thread", HTTP_GET, handleThreadAjax);
  server.on("/upload", HTTP_POST, handleUpload, handleFileUpload);
  server.on("/node-files", handleNodeFiles);
  server.on("/uploadpage", handleUploadPage); server.on("/", handleRoot);            // Main library page
  // server.on("/generate_204", handleCaptivePortal);  // Android
  //server.on("/gen_204", handleCaptivePortal);       // Android
  //server.on("/ncsi.txt", handleCaptivePortal);      // Windows
  //server.on("/check_network_status.txt", handleCaptivePortal);  // Windows
  //server.on("/hotspot-detect.html", handleCaptivePortal);    // iOS
  //server.on("/success.txt", handleCaptivePortal);           // iOS
  //server.on("/connecttest.txt", handleCaptivePortal);       // Windows
  //server.onNotFound(handleNotFound);                       // All other requests

  // Routes for Captive Portal detection
  server.on("/", handlePortal);
  server.on("/fwlink", handlePortal);
  server.on("/generate_204", handlePortal);
  server.on("/hotspot-detect.html", handlePortal);
  server.onNotFound(handlePortal);

  // Add handlers for different captive portal detection
  server.on("/", []() {
    if (captivePortal()) {
      return;
    }
    handlePortal();
  });

  server.onNotFound([]() {
    if (captivePortal()) {
      return;
    }
    handlePortal();
  });

  server.begin();
  Serial.println("HTTP server started");

  // Initialize cleanup timer
  lastCleanupTime = millis();
}

void loop() {
  dnsServer.processNextRequest();   // DNS
  server.handleClient();    //HTTP
  checkAndCleanupForum();
}

void checkAndCleanupForum() {
  unsigned long currentTime = millis();
  if ((currentTime - lastCleanupTime >= CLEANUP_INTERVAL) || (currentTime < lastCleanupTime)) {
    cleanupForum();
    lastCleanupTime = currentTime;
  }
}

void cleanupForum() {
  if (!sdCardReady) {
    Serial.println("[SD][WARN] Skipping forum cleanup; SD card unavailable");
    return;
  }
  if (SD.exists("/forum")) {
    removeDirectory("/forum");
  }

  SD.mkdir("/forum");
  SD.mkdir("/forum/posts");

  File threadsFile = SD.open("/forum/threads.json", FILE_WRITE);
  if (threadsFile) {
    threadsFile.println("[]");
    threadsFile.close();
  }

  File logFile = SD.open("/forum/cleanup.log", FILE_WRITE);
  if (logFile) {
    logFile.printf("Forum cleaned at: %lu\n", millis());
    logFile.close();
  }
}

void removeDirectory(const char * path) {
  File dir = SD.open(path);
  if (!dir.isDirectory()) {
    return;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    String entryPath = String(path) + "/" + entry.name();

    if (entry.isDirectory()) {
      entry.close();
      removeDirectory(entryPath.c_str());
    } else {
      entry.close();
      SD.remove(entryPath.c_str());
    }
  }
  dir.close();
  SD.rmdir(path);
}


//WEB HANDLERS
void handleRoot() {
    // Add headers for captive portal
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    
    // Create a simpler, more robust HTML string
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    // Simple but effective cyberpunk styling
    html += "body{background-color:#000;color:#0f0;font-family:'Courier New',monospace;margin:0;padding:0;display:flex;justify-content:center;align-items:center;min-height:100vh;overflow-x:hidden}";
    html += "h1,h2,h3{color:#0f0;text-shadow:0 0 5px #0f0;text-transform:uppercase;text-align:center}";
    html += "a{color:#0f0;text-decoration:none}";
    html += "a:hover{color:#fff;text-shadow:0 0 6px #0f0}";
    html += ".container{border:1px solid #0f0;width:90%;max-width:800px;margin:20px;padding:20px;box-shadow:0 0 15px #0f0;position:relative;z-index:1;background:rgba(0,5,0,0.7)}";
    html += ".status{border-left:3px solid #0f0;padding:10px;margin:15px auto;text-align:center;width:80%;max-width:500px;transition:all 0.3s ease}";
    html += ".status:hover{transform:translateY(-3px);box-shadow:0 0 8px rgba(0,255,0,0.7)}";
    
    // Cyberpunk grid background
    html += ".cyber-grid{position:fixed;top:0;left:0;right:0;bottom:0;background:linear-gradient(rgba(0,15,0,0.2) 1px, transparent 1px),linear-gradient(90deg, rgba(0,15,0,0.2) 1px, transparent 1px);background-size:20px 20px;z-index:-1}";
    
    // Scanning effect
    html += ".cyber-scan{position:fixed;top:0;left:0;right:0;height:3px;background:rgba(0,255,0,0.5);box-shadow:0 0 10px #0f0;animation:scan 20s linear infinite;z-index:0}";
    html += "@keyframes scan{0%{top:0}100%{top:100%}}";
    
    // Restored Glitch Text Effect
    html += ".glitch-wrapper{padding:20px;text-align:center;margin-bottom:20px;position:relative}";
    html += ".glitch{font-size:2.5em;font-weight:bold;text-transform:uppercase;position:relative;text-shadow:0.05em 0 0 #00fffc,-0.03em -0.04em 0 #fc00ff,0.025em 0.04em 0 #fffc00;animation:glitch 725ms infinite}";
    html += ".glitch span{position:absolute;top:0;left:0;width:100%}";
    html += ".glitch span:first-child{animation:glitch 500ms infinite;clip-path:polygon(0 0,100% 0,100% 35%,0 35%);transform:translate(-0.04em,-0.03em);opacity:0.75}";
    html += ".glitch span:last-child{animation:glitch 375ms infinite;clip-path:polygon(0 65%,100% 65%,100% 100%,0 100%);transform:translate(0.04em,0.03em);opacity:0.75}";
    
    // Glitch animation keyframes
    html += "@keyframes glitch{0%{text-shadow:0.05em 0 0 #00fffc,-0.03em -0.04em 0 #fc00ff,0.025em 0.04em 0 #fffc00}15%{text-shadow:0.05em 0 0 #00fffc,-0.03em -0.04em 0 #fc00ff,0.025em 0.04em 0 #fffc00}16%{text-shadow:-0.05em -0.025em 0 #00fffc,0.025em 0.035em 0 #fc00ff,-0.05em -0.05em 0 #fffc00}49%{text-shadow:-0.05em -0.025em 0 #00fffc,0.025em 0.035em 0 #fc00ff,-0.05em -0.05em 0 #fffc00}50%{text-shadow:0.05em 0.035em 0 #00fffc,0.03em 0 0 #fc00ff,0 -0.04em 0 #fffc00}99%{text-shadow:0.05em 0.035em 0 #00fffc,0.03em 0 0 #fc00ff,0 -0.04em 0 #fffc00}100%{text-shadow:-0.05em 0 0 #00fffc,-0.025em -0.04em 0 #fc00ff,-0.04em -0.025em 0 #fffc00}}";
    
    html += "</style></head><body>";

    // Background elements
    html += "<div class='cyber-grid'></div><div class='cyber-scan'></div>";

    // Main content
    html += "<div class='container'>";
    
    // Restored glitch title with spans for effect
    html += "<div class='glitch-wrapper'>";
    html += "<div class='glitch'>7H3 R04M1NG L1BR4RY";
    html += "<span>7H3 R04M1NG L1BR4RY</span>";
    html += "<span>7H3 R04M1NG L1BR4RY</span>";
    html += "</div>";
    html += "</div>";
    
    // ASCII Owl
    html += "<pre style='color:#0f0;text-align:center;line-height:1.2;margin:20px auto;font-size:18px'>";
    html += "      ,___,\n     (O,O)\n     (  v  )\n    -==*^*==-\n";
    html += "</pre>";
    
    // Menu options
   html += "<div class='status' style='position:relative'>";
    html += "<h3><a href='/node-files?node=" + AP_SSID + "' style='text-decoration:underline'> ** 74k3-4-F1L3 ** </a></h3>";
    html += "<div style='position:absolute;right:0;top:0;bottom:0;width:3px;background:#0f0;box-shadow:0 0 6px #0f0;'></div>";
    html += "</div>";

    html += "<div class='status' style='position:relative'>";
    html += "<h3><a href='/uploadpage' style='text-decoration:underline'> ** L34V3-4-F1L3 ** </a></h3>";
    html += "<div style='position:absolute;right:0;top:0;bottom:0;width:3px;background:#0f0;box-shadow:0 0 6px #0f0;'></div>";
    html += "</div>";

    html += "<div class='status' style='position:relative'>";
    html += "<h3><a href='/forum' style='text-decoration:underline'> ** P057-2-F0RUM ** </a></h3>";
    html += "<div style='position:absolute;right:0;top:0;bottom:0;width:3px;background:#0f0;box-shadow:0 0 6px #0f0;'></div>";
    html += "</div>";
    
    //buffer space
    html += "<div style='height:40px;'></div>"; 

    // System status
    html += "<div style='text-align:center;margin-top:20px'>";
    html += "[ System Status: " + String(ledState ? "ACTIVE" : "Rebel") + " ]";
    html += "</div>";

    //buffer space
    html += "<div style='height:80px;'></div>"; 

    // Disclaimer link in bottom right
    html += "<div style='position:absolute;bottom:10px;right:10px;font-size:0.9em;text-align:right'>";
    html += "<a href='/disclaimer' style='text-decoration:underline;color:#fff'>DISCLAIMER</a>";
    html += "</div>";
    
    html += "</div>"; // Close container
    html += "</body></html>";

    // Send the response
    server.send(200, "text/html", html);
}

void handleRedirect() {
  // Send a simple HTML page with JavaScript redirection
  String html = "<!DOCTYPE html><html><head>";
  html += "<script>window.location.replace('http://" + toStringIp(apIP) + "');</script>";
  html += "<meta http-equiv='refresh' content='0;url=http://" + toStringIp(apIP) + "'>";
  html += "</head><body>";
  html += "<p>Redirecting to portal...</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleNotFound() {
  // If this is a desktop browser request to an external domain
  if (!isIp(server.hostHeader()) && server.hostHeader() != WiFi.softAPIP().toString()) {
    // JavaScript redirect + meta refresh for desktop browsers
    String html = "<!DOCTYPE html><html><head>";
    html += "<script>window.location.replace('http://" + toStringIp(apIP) + "');</script>";
    html += "<meta http-equiv='refresh' content='0;url=http://" + toStringIp(apIP) + "'>";
    html += "</head><body>";
    html += "<p>Redirecting to portal...</p>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
    return;
  }
  
  // If it's a direct IP request or a specific path we don't handle
  handleRoot();
}

void handleCaptivePortal() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body {";
    html += "  font-family: 'Courier New', monospace;";
    html += "  background-color: #000;";
    html += "  color: #0f0;";
    html += "  margin: 0;";
    html += "  padding: 0;";
    html += "  height: 100vh;";
    html += "  display: flex;";
    html += "  flex-direction: column;";
    html += "  justify-content: center;";
    html += "  align-items: center;";
    html += "  overflow: hidden;";
    html += "}";
    html += ".container {";
    html += "  text-align: center;";
    html += "  animation: pulse 2s infinite;";
    html += "  z-index: 2;";
    html += "  position: relative;";
    html += "}";
    html += "@keyframes pulse {";
    html += "  0% {transform: scale(1);}";
    html += "  50% {transform: scale(1.05);}";
    html += "  100% {transform: scale(1);}";
    html += "}";
    html += "h1 {";
    html += "  color: #0f0;";
    html += "  text-shadow: 0 0 10px #0f0;";
    html += "  text-transform: uppercase;";
    html += "  font-size: 8vw;";
    html += "  margin: 0;";
    html += "}";
    html += ".enter-btn {";
    html += "  display: inline-block;";
    html += "  background: #000;";
    html += "  color: #0f0;";
    html += "  border: 3px solid #0f0;";
    html += "  padding: 15px 30px;";
    html += "  font-size: 6vw;";
    html += "  text-decoration: none;";
    html += "  margin-top: 30px;";
    html += "  animation: glow 1.5s infinite alternate;";
    html += "  transition: all 0.3s ease;";
    html += "}";
    html += ".enter-btn:hover {";
    html += "  background: #0f0;";
    html += "  color: #000;";
    html += "  transform: scale(1.05);";
    html += "}";
    html += "@keyframes glow {";
    html += "  from {box-shadow: 0 0 10px #0f0;}";
    html += "  to {box-shadow: 0 0 20px #0f0, 0 0 30px #0f0;}";
    html += "}";
    html += ".scan {";
    html += "  position: absolute;";
    html += "  height: 5px;";
    html += "  background: rgba(0,255,0,0.5);";
    html += "  width: 100%;";
    html += "  top: 0;";
    html += "  box-shadow: 0 0 20px #0f0;";
    html += "  animation: scan 2s linear infinite;";
    html += "}";
    html += "@keyframes scan {";
    html += "  0% {top: 0;}";
    html += "  100% {top: 100%;}";
    html += "}";
    html += ".grid {";
    html += "  position: fixed;";
    html += "  top: 0;";
    html += "  left: 0;";
    html += "  right: 0;";
    html += "  bottom: 0;";
    html += "  background: linear-gradient(rgba(0,15,0,0.3) 1px, transparent 1px),";
    html += "              linear-gradient(90deg, rgba(0,15,0,0.3) 1px, transparent 1px);";
    html += "  background-size: 30px 30px;";
    html += "  z-index: 1;";
    html += "}";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='grid'></div>";
    html += "<div class='scan'></div>";
    html += "<div class='container'>";
    html += "<h1>CONNECTING...</h1>";
    html += "<a href='http://" + WiFi.softAPIP().toString() + "' class='enter-btn'>ACCESS</a>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handlePortal() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += ".container { text-align: center; margin-top: 50px; }";
    html += ".enter-btn { display: inline-block; background: #000; color: #0f0; border: 1px solid #0f0; padding: 15px 30px; font-size: 1.2em; text-decoration: none; margin-top: 20px; }";
    html += ".enter-btn:hover { background: #0f0; color: #000; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>PR0J3KT B00KM4RK</h1>";
    html += "<a href='http://192.168.4.1/library' class='enter-btn'>TO ENTER LIBRARY</a>";
    html += "</div>";
    html += "<div style='text-align: center;'>";
    html += "<h4>Accept Sign-In (may vary by device). Open browser and navigate to 192.168.4.1</h4>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleFileList() {
   /*
   // Disconnect any existing peer connections when viewing the list
   for (const auto& peer : peers) {
       disconnectFromPeer(peer.mac);
   }
   */

   server.setContentLength(CONTENT_LENGTH_UNKNOWN);
   server.send(200, "text/html", "");

   // First part - HTML Header
   static const char HTML_HEAD[] PROGMEM = R"=====(
   <html><head>
   <meta name='viewport' content='width=device-width, initial-scale=1'>
   <style>
   body { 
       font-family: 'Courier New', monospace; 
       background-color: #000; 
       color: #0f0; 
       margin: 20px; 
       line-height: 1.6; 
   }
   h1, h2, h3 { 
       color: #0f0; 
       text-shadow: 0 0 5px #0f0; 
       text-transform: uppercase; 
       text-align: center; 
   }
   a { 
       color: #0f0; 
       text-decoration: none; 
   }
   a:hover { 
       color: #fff; 
       text-shadow: 0 0 10px #0f0; 
   }
   .container { 
       border: 1px solid #0f0; 
       padding: 20px; 
       margin: 10px 0; 
       box-shadow: 0 0 10px #0f0; 
   }
   .node-item { 
       border-left: 3px solid #0f0; 
       padding: 10px; 
       margin: 10px 0;
       display: flex;
       justify-content: space-between;
       align-items: center;
       transition: all 0.3s ease;
   }
   .node-item:hover { 
       background-color: #001100; 
       transform: translateX(5px);
   }
   .node-info {
       flex-grow: 1;
   }
   .connect-btn {
       padding: 5px 15px;
       border: 1px solid #0f0;
       margin-left: 20px;
       transition: all 0.3s ease;
   }
   .connect-btn:hover {
       background: #0f0;
       color: #000;
   }
   .node-status {
       color: #0a0;
       font-size: 0.9em;
       margin-top: 5px;
   }
   .loader {
       display: none;
       width: 20px;
       height: 20px;
       border: 2px solid #0f0;
       border-radius: 50%;
       border-top-color: transparent;
       animation: spin 1s linear infinite;
       margin-left: 10px;
   }
   @keyframes spin {
       to {transform: rotate(360deg);}
   }
   </style>
   <script>
   function connect(nodeId) {
       const loader = document.getElementById('loader-' + nodeId);
       const btn = document.getElementById('btn-' + nodeId);
       const status = document.getElementById('status-' + nodeId);
       
       loader.style.display = 'inline-block';
       btn.style.display = 'none';
       status.textContent = '[CONNECTING...]';
       
       // Redirect to node files page
       window.location.href = '/node-files?node=' + nodeId;
   }
   </script>
   </head><body>
   <div class='container'>
   <h3>//404 D3W3Y N07 F0UND//</h3>
   )=====";

   server.sendContent_P(HTML_HEAD);

   // Local node
   String localNode = "<div class='node-item'>";
   localNode += "<div class='node-info'>&gt; NODE: " + String(AP_SSID) + " [LOCAL] &lt;</div>";
   localNode += "<a href='/node-files?node=" + String(AP_SSID) + "' class='connect-btn'>CONNECT</a>";
   localNode += "</div>";
   server.sendContent(localNode);
   yield();

   // Remote nodes
   /*
   int nodeCount = 0;
   for (const auto& peer : peers) {
       String nodeHtml = "<div class='node-item'>";
       nodeHtml += "<div class='node-info'>&gt; NODE: " + peer.ssid + " &lt;";
       nodeHtml += "<div class='node-status' id='status-" + peer.ssid + "'>[AVAILABLE]</div></div>";
       nodeHtml += "<div class='loader' id='loader-" + peer.ssid + "'></div>";
       nodeHtml += "<a href='javascript:void(0)' onclick='connect(\"" + peer.ssid + "\")' ";
       nodeHtml += "class='connect-btn' id='btn-" + peer.ssid + "'>CONNECT</a>";
       nodeHtml += "</div>";
       server.sendContent(nodeHtml);
       
       if (++nodeCount % 5 == 0) {
           yield();
       }
   }
   */

   // Last part - HTML Footer
   static const char HTML_FOOTER[] PROGMEM = R"=====(
   <div style='text-align: center;'>
   <br><a href='/'>&lt;&lt; Return to Terminal &gt;&gt;</a>
   </div>
   </div></body></html>
   )=====";

   server.sendContent_P(HTML_FOOTER);
   finalizeChunkedResponse();
}

void handleNodeFiles() {
    String nodeSSID = server.arg("node");
    String section = server.arg("section");
    bool isLocal = true; // local files only
    
    // Start sending headers immediately to improve responsiveness
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    // Send HTML header with enhanced cyberpunk styles while keeping CSS compact
    server.sendContent(F(
      "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<style>"
      "body{background:#000;color:#0f0;font-family:monospace;margin:0;padding:0;min-height:100vh;overflow-x:hidden}"
      "h1,h2,h3{color:#0f0;text-shadow:0 0 5px #0f0;text-transform:uppercase;text-align:center}"
      "a{color:#0f0;text-decoration:none;transition:all .2s}"
      "a:hover{color:#fff;text-shadow:0 0 10px #0f0}"
      ".container{border:1px solid #0f0;padding:15px;margin:10px auto;box-shadow:0 0 15px #0f0;max-width:800px;width:90%;position:relative;z-index:1;background:rgba(0,10,0,0.7)}"
      ".file-list{max-height:70vh;overflow-y:auto;border:1px solid #0f0;margin:10px 0;padding:5px;background:rgba(0,5,0,0.5)}"
      ".file-list::-webkit-scrollbar{width:5px;background:#000}"
      ".file-list::-webkit-scrollbar-thumb{background:#0f0}"
      ".file-item{border-left:3px solid #0f0;padding:8px;margin:5px 0;transition:all .2s;background:rgba(0,10,0,0.4)}"
      ".file-item:hover{background:#001500;transform:translateX(5px);box-shadow:0 0 10px #0f0}"
      ".nav-bar{display:flex;justify-content:space-between;align-items:center;margin:10px 0;padding:8px;border:1px solid #0f0;background:rgba(0,10,0,0.5)}"
      ".nav-button{padding:5px 15px;border:1px solid #0f0;transition:all .2s;background:rgba(0,20,0,0.6)}"
      ".nav-button:hover{background:#0f0;color:#000;box-shadow:0 0 10px #0f0}"
      ".section-list{display:grid;grid-template-columns:repeat(auto-fill,minmax(60px,1fr));gap:10px;padding:15px;border:1px solid #0f0;margin:15px 0;background:rgba(0,10,0,0.5)}"
      ".section-button{text-align:center;padding:5px;border:1px solid #0f0;transition:all .2s;background:rgba(0,15,0,0.6)}"
      ".section-button:hover{background:#0f0;color:#000;transform:scale(1.05);box-shadow:0 0 10px #0f0}"
      // Cyberpunk grid background
      ".cyber-grid{position:fixed;top:0;left:0;right:0;bottom:0;background:linear-gradient(rgba(0,15,0,0.2) 1px, transparent 1px),linear-gradient(90deg, rgba(0,15,0,0.2) 1px, transparent 1px);background-size:20px 20px;z-index:-1}"
      // Scanning effect  
      ".cyber-scan{position:fixed;top:0;left:0;right:0;height:3px;background:rgba(0,255,0,0.5);box-shadow:0 0 10px #0f0;animation:scan 20s linear infinite;z-index:0}"
      "@keyframes scan{0%{top:0}100%{top:100%}}"
      ".title{text-shadow:0 0 10px #0f0;letter-spacing:2px;margin:10px 0;font-weight:bold}"
      // Loading indicator that doesn't block rendering
      ".loading{position:fixed;bottom:10px;right:10px;padding:5px 10px;background:rgba(0,10,0,0.8);border:1px solid #0f0;display:none;animation:pulse 1.5s infinite;z-index:100}"
      "@keyframes pulse{0%{opacity:.7}50%{opacity:1}100%{opacity:.7}}"
      "</style>"
      "<script>"
      "function showLoading(show){document.querySelector('.loading').style.display=show?'block':'none'}"
      "window.addEventListener('beforeunload',function(){showLoading(true)})"
      "</script>"
      "</head><body>"
      "<div class='cyber-grid'></div><div class='cyber-scan'></div>"
      "<div class='loading'>PR0C3551NG...</div>"
    ));

    // Send container opening
    server.sendContent(F("<div class='container'>"));

    // Send the title with cyberpunk styling
    server.sendContent("<h3 class='title'>//N0D3: " + nodeSSID + "//</h3>");

    // Cached directory path calculation outside conditions
    String dirPath = "/Alexandria/";
    
    if (section == "") {
        // Send section buttons (alphabetical navigation)
        server.sendContent(F("<div class='section-list'>"));
        server.sendContent(F("<a href='/node-files?node="));
        server.sendContent(nodeSSID + F("&section=num' class='section-button' onclick='showLoading(true)'>[0-9]</a>"));
        server.sendContent(F("<a href='/node-files?node="));
        server.sendContent(nodeSSID + F("&section=sym' class='section-button' onclick='showLoading(true)'>[#@]</a>"));
        
        // Send A-Z section buttons in chunks to avoid long operations
        for (char c = 'A'; c <= 'M'; c++) { // First half of alphabet
            server.sendContent(F("<a href='/node-files?node="));
            server.sendContent(nodeSSID + F("&section="));
            server.sendContent(String(c) + F("' class='section-button' onclick='showLoading(true)'>["));
            server.sendContent(String(c) + F("]</a>"));
            yield(); // Allow system to process
        }
        
        for (char c = 'N'; c <= 'Z'; c++) { // Second half of alphabet
            server.sendContent(F("<a href='/node-files?node="));
            server.sendContent(nodeSSID + F("&section="));
            server.sendContent(String(c) + F("' class='section-button' onclick='showLoading(true)'>["));
            server.sendContent(String(c) + F("]</a>"));
            yield(); // Allow system to process
        }
        
        server.sendContent(F("</div>"));

        // Navigation bar
        server.sendContent(F("<div class='nav-bar' style='text-align:center;'><div style='display:inline-block;'>"));
        server.sendContent(F("<a href='/' class='nav-button' onclick='showLoading(true)'>&lt;&lt; R37URN 70 73RM1N4L &gt;&gt;</a>"));
        server.sendContent(F("</div></div>"));
    } else {
        // Show specific section files
        std::vector<String> sectionFiles;
        String sectionTitle;

        // Determine directory path and section title - do this calculation once
        if (section == "num") {
            dirPath += "0-9";
            sectionTitle = "[0-9]";
        } else if (section == "sym") {
            dirPath += "#@";
            sectionTitle = "[5YM80L5]";
        } else if (section.length() == 1) {
            dirPath += section;
            sectionTitle = "[" + section + "]";
        }

        // Navigation bar at top - send immediately
        server.sendContent(String("<div class='nav-bar'><span class='section-title'>") +
                           sectionTitle +
                           " F1L35</span></div>");
        
        // Start file list container
        server.sendContent(F("<div class='file-list'>"));

        // More efficient file reading with limited yields
        if (isLocal) {
            File dir = SD.open(dirPath);
            int fileCount = 0;
            
            if (dir) {
                // First count how many files we have (faster than using vectors for large directories)
                int totalFiles = 0;
                while (File entry = dir.openNextFile()) {
                    if (!entry.isDirectory() && isAllowedFile(String(entry.name()))) {
                        totalFiles++;
                    }
                    entry.close();
                    
                    // Only yield occasionally during counting to maintain responsiveness
                    if (totalFiles % 10 == 0) {
                        yield();
                    }
                }
                
                // Reset directory pointer
                dir.close();
                dir = SD.open(dirPath);
                
                if (totalFiles == 0) {
                    // No files - send this information immediately
                    server.sendContent(F("<div class='file-item'>[N0 F1L35 F0UND]</div>"));
                } else {
                    // Read and send files in batches for better performance
                    const int BATCH_SIZE = 10; // Process 10 files before yielding
                    int batchCount = 0;
                    
                    while (File entry = dir.openNextFile()) {
                        if (!entry.isDirectory()) {
                            String fileName = String(entry.name());
                            if (isAllowedFile(fileName)) {
                                String fullPath = section + "/" + fileName;
                                String encodedPath = urlEncodePath(fullPath);

                                String html = "<div class='file-item'><a href='/download?file=";
                                html += encodedPath;
                                html += "' onclick='showLoading(true)'>&gt; " + fileName + " &lt;</a></div>";

                                server.sendContent(html);
                                fileCount++;

                                batchCount++;
                                if (batchCount >= BATCH_SIZE) {
                                    yield();
                                    batchCount = 0;
                                }
                            }
                        }
                        entry.close();
                    }
                    
                    // Update section title with file count
                    server.sendContent("<script>document.querySelector('.section-title').innerHTML += ' [" +
                                       String(fileCount) +
                                       " F1L35]';</script>");
                }
                
                dir.close();
            } else {
                server.sendContent(F("<div class='file-item'>[D1R3C70RY N07 F0UND]</div>"));
            }
        }
        
        server.sendContent(F("</div>"));

        // Navigation bar at bottom
        server.sendContent("<div class='nav-bar'>");
        server.sendContent("<a href='/node-files?node=" + nodeSSID +
                           "' class='nav-button' onclick='showLoading(true)'>&lt;&lt; 53C710N5</a>");
        server.sendContent("</div>");
    }

    // Close container and HTML
    server.sendContent(F("</div>"));
    
    // Add a script to hide loading indicator when page is loaded
    server.sendContent(F("<script>window.onload = function(){showLoading(false)}</script>"));
    server.sendContent(F("</body></html>"));
    finalizeChunkedResponse();
}

void handleFileDownload() {
    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "File parameter missing");
        return;
    }

    String fileName = server.arg("file");

    // ✅ Strip path to get only the actual filename
    String baseName = fileName.substring(fileName.lastIndexOf('/') + 1);

    // ✅ Run extension check on the base filename only
    if (!isAllowedFile(baseName)) {
        server.send(400, "text/plain", "Invalid file type. Only PDF, EPUB, DOC, DOCX, RTF, and TXT files are allowed.");
        return;
    }

    String filePath = "/Alexandria/" + fileName;
    if (!SD.exists(filePath)) {
        server.send(404, "text/plain", "File not found");
        return;
    }

    File file = SD.open(filePath, FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Failed to open file");
        return;
    }

    String contentType = "application/octet-stream";
    if (baseName.endsWith(".pdf")) {
        contentType = "application/pdf";
    } else if (baseName.endsWith(".epub")) {
        contentType = "application/epub+zip";
    } else if (baseName.endsWith(".doc")) {
        contentType = "application/msword";
    } else if (baseName.endsWith(".docx")) {
        contentType = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    } else if (baseName.endsWith(".rtf")) {
        contentType = "application/rtf";
    } else if (baseName.endsWith(".txt")) {
        contentType = "text/plain";
    }

    server.sendHeader("Content-Disposition", "attachment; filename=" + baseName);
    server.sendHeader("Connection", "close");
    server.streamFile(file, contentType);
    file.close();
}


void handleUploadPage() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 3px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 6px #0f0; }";
    html += ".container { border: 1px solid #0f0; padding: 20px; margin: 10px 0; box-shadow: 0 0 8px rgba(0,255,0,0.6); }";
    html += ".upload-form { width: 75%; margin: 20px auto; text-align: center; }";
    html += "input[type='file'] { display: block; margin: 20px auto; color: #0f0; }";
    html += "input[type='submit'] { background: #000; color: #0f0; border: 1px solid #0f0; padding: 10px 20px; cursor: pointer; }";
    html += ".formats { color: #0a0; margin: 10px 0; }";
    html += ".progress-container { width: 100%; margin: 10px 0; display: none; }";
    html += ".progress { width: 100%; height: 20px; background: #001000; border: 1px solid #0f0; overflow: hidden; }";
    html += ".progress-bar { width: 0%; height: 100%; background: #0f0; transition: width 0.2s; }";
    html += ".progress-text { text-align: center; margin-top: 5px; }";
    html += ".status-area { border: 1px solid #0f0; padding: 10px; margin-top: 20px; display: none; background: rgba(0,10,0,0.5); }";
    html += ".verification-success { color: #0f0; animation: pulse 1.5s infinite; }";
    html += ".verification-failed { color: #f00; }";
    html += "@keyframes pulse { 0% {opacity: 0.7;} 50% {opacity: 1;} 100% {opacity: 0.7;} }";
    html += "</style>";

    // JavaScript for progress tracking and status messages
    html += "<script>";
    html += "function showProgress() {";
    html += "  document.getElementById('progressContainer').style.display = 'block';";
    html += "  document.getElementById('statusArea').style.display = 'block';";
    html += "  document.getElementById('statusMessage').innerHTML = 'UPL04D1NG...';";
    html += "  document.getElementById('submitBtn').disabled = true;";
    html += "  ";
    html += "  const form = document.getElementById('uploadForm');";
    html += "  const formData = new FormData(form);";
    html += "  const fileName = document.getElementById('fileInput').files[0].name;";
    html += "  const xhr = new XMLHttpRequest();";
    html += "  ";
    html += "  xhr.open('POST', '/upload', true);";
    html += "  ";
    html += "  // Track upload progress";
    html += "  xhr.upload.onprogress = function(e) {";
    html += "    if (e.lengthComputable) {";
    html += "      const percent = Math.round((e.loaded / e.total) * 100);";
    html += "      document.getElementById('progressBar').style.width = percent + '%';";
    html += "      document.getElementById('progressText').textContent = percent + '%';";
    html += "      console.log('Upload progress: ' + percent + '%');"; // Debug logging
    html += "    }";
    html += "  };";
    html += "  ";
    html += "  // Handle completion";
    html += "  xhr.onload = function() {";
    html += "    if (xhr.status === 200) {";
    html += "      document.getElementById('statusMessage').innerHTML = 'V3R1FY1NG F1L3...';";
    html += "      setTimeout(function() {";
    html += "        document.getElementById('statusMessage').className = 'verification-success';";
    html += "        document.getElementById('statusMessage').innerHTML = 'F1L3 V3R1F13D SUC3SSFULLY!';";
    html += "        // Wait longer before redirecting - 5 seconds";
    html += "        setTimeout(function() {";
    html += "          window.location.href = '/?filename=' + encodeURIComponent(fileName);";
    html += "        }, 5000);";
    html += "      }, 1500);";
    html += "    } else {";
    html += "      document.getElementById('statusMessage').className = 'verification-failed';";
    html += "      document.getElementById('statusMessage').innerHTML = 'V3R1F1C4710N F41L3D!';";
    html += "      document.getElementById('submitBtn').disabled = false;";
    html += "    }";
    html += "  };";
    html += "  ";
    html += "  // Handle errors";
    html += "  xhr.onerror = function() {";
    html += "    document.getElementById('statusMessage').className = 'verification-failed';";
    html += "    document.getElementById('statusMessage').innerHTML = 'UPL04D F41L3D! CH3CK C0NN3C710N.';";
    html += "    document.getElementById('submitBtn').disabled = false;";
    html += "  };";
    html += "  ";
    html += "  xhr.send(formData);";
    html += "  return false;"; // Prevent regular form submission
    html += "}";
    html += "</script>";
    html += "</head><body>";

    html += "<h2>// D0N473 //</h2>";

    html += "<div class='upload-form'>";
    html += "<form id='uploadForm' method='post' action='/upload' enctype='multipart/form-data' onsubmit='return showProgress()'>";
    html += "<div class='formats'>[AZW|DOC|DOCX|EPUB|FB2]</div>";
    html += "<div class='formats'>[iBOOK|LIB|MOBI|PDB]</div>";
    html += "<div class='formats'>[PDF|PRC|RTF|TXT]</div>";
    html += "<input id='fileInput' type='file' name='file' accept='.pdf,.txt,.rtf,.epub,.azw,.mobi,.lib,.fb2,.prc,.pdb,.ibook,.doc,.docx' required><br>";
    
    html += "<p style='color:#0ff; font-size:0.9em; padding: 10px; border: 1px dashed #0ff; border-radius: 10px;'>"
        "Trouble uploading? Open your browser of choice and navigate to 192.168.4.1"
        "</p>";

    // Progress display
    html += "<div id='progressContainer' class='progress-container'>";
    html += "<div class='progress'>";
    html += "<div id='progressBar' class='progress-bar'></div>";
    html += "</div>";
    html += "<div id='progressText' class='progress-text'>0%</div>";
    html += "</div>";
    
    // Status area with more visibility
    html += "<div id='statusArea' class='status-area'>";
    html += "<div id='statusMessage'>R34DY</div>";
    html += "</div>";
    
    html += "<input id='submitBtn' type='submit' value='UPLOAD'>";
    html += "</form>";
    html += "</div>";

    html += "<div style='text-align: center;'>";
    html += "<br><a href='/'>&lt;&lt; Return to Terminal &gt;&gt;</a>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleDisclaimer() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{background-color:#000;color:#0f0;font-family:'Courier New',monospace;margin:0;padding:0;display:flex;justify-content:center;align-items:center;min-height:100vh;overflow-x:hidden}";
    html += "h1,h2,h3{color:#0f0;text-shadow:0 0 5px #0f0;text-transform:uppercase;text-align:center}";
    html += "a{color:#0f0;text-decoration:none}";
    html += "a:hover{color:#fff;text-shadow:0 0 10px #0f0}";
    html += ".container{border:1px solid #0f0;width:90%;max-width:800px;margin:20px;padding:20px;box-shadow:0 0 15px #0f0;position:relative;z-index:1;background:rgba(0,5,0,0.7)}";
    html += ".disclaimer-section{border-left:3px solid #0f0;padding:15px;margin:15px 0;text-align:left;background:rgba(0,10,0,0.4)}";
    html += ".cyber-grid{position:fixed;top:0;left:0;right:0;bottom:0;background:linear-gradient(rgba(0,15,0,0.2) 1px, transparent 1px),linear-gradient(90deg, rgba(0,15,0,0.2) 1px, transparent 1px);background-size:20px 20px;z-index:-1}";
    html += ".cyber-scan{position:fixed;top:0;left:0;right:0;height:3px;background:rgba(0,255,0,0.5);box-shadow:0 0 10px #0f0;animation:scan 3s linear infinite;z-index:0}";
    html += "@keyframes scan{0%{top:0}100%{top:100%}}";
    html += ".back-button{display:inline-block;padding:10px 15px;border:1px solid #0f0;margin-top:20px;transition:all 0.3s ease;background:rgba(0,10,0,0.6);text-align:center}";
    html += ".back-button:hover{background:#0f0;color:#000;box-shadow:0 0 10px #0f0}";
    html += "</style></head><body>";

    // Background elements
    html += "<div class='cyber-grid'></div><div class='cyber-scan'></div>";

    // Main content
    html += "<div class='container'>";
    
    // Title
    html += "<h2>// 5Y573M D15CL41M3R //</h2>";
    
    // Usage terms section
    html += "<div class='disclaimer-section'>";
    html += "<h3>Usage Terms</h3>";
    html += "<p>7H3 R04M1NG L1BR4RY is designed for the sharing of non-copyright protected works and documents that users have the legal right to distribute.</p>";
    html += "<p>By uploading files to this system, you affirm that you have the legal right to distribute them and that they do not violate any applicable copyright laws.</p>";
    html += "</div>";
    
    // Format disclaimer section
    html += "<div class='disclaimer-section'>";
    html += "<h3>Supported Formats</h3>";
    html += "<p>This system supports multiple document formats including: PDF, EPUB, DOC, RTF, TXT, AZW, MOBI, LIB, FB2, PRC, PDB, and iBOOK.</p>";
    html += "<p>Note that some formats (AZW, iBook) are proprietary and may be tied to specific reader ecosystems. We do not guarantee that all devices will be able to read all formats.</p>";
    html += "</div>";
    
    // Security disclaimer section
    html += "<div class='disclaimer-section'>";
    html += "<h3>Security Notice</h3>";
    html += "<p>Files are stored and transferred as-is. The system does not scan for malicious content. Exercise caution when downloading files from unknown sources.</p>";
    html += "<p>Some document formats may contain scripts or external links. We recommend using readers with security features enabled.</p>";
    html += "</div>";
    
    // No liability section
    html += "<div class='disclaimer-section'>";
    html += "<h3>Limitation of Liability</h3>";
    html += "<p>The operators of this system are not responsible for the content of uploaded files or any damages that may result from their use.</p>";
    html += "<p>This system is provided as-is with no warranty. Use at your own risk.</p>";
    html += "</div>";
    
    // Return button
    html += "<div style='text-align:center'>";
    html += "<a href='/' class='back-button'>&lt;&lt; R37URN 70 73RM1N4L &gt;&gt;</a>";
    html += "</div>";
    
    html += "</div>"; // Close container
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleFileUpload() {
    if (server.uri() != "/upload") return;
   
    HTTPUpload& upload = server.upload();
     Serial.printf("Status: %d | Filename: %s | Bytes: %d\n", 
                upload.status, 
                upload.filename.c_str(), 
                upload.currentSize);
    static String currentFilePath;
    static size_t totalBytes = 0;
   
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!isAllowedFile(filename)) {
            return;
        }

        // Determine correct directory based on first character
        String dirPath = "/Alexandria/";
        char firstChar = toupper(filename.charAt(0));
        
        if (isdigit(firstChar)) {
            dirPath += "0-9";
        } else if (isalpha(firstChar)) {
            dirPath += String(firstChar);
        } else {
            dirPath += "#@";
        }

        // Ensure directory exists
        if (!SD.exists(dirPath)) {
            Serial.println("Creating directory: " + dirPath);
            if (!SD.mkdir(dirPath)) {
                Serial.println("Failed to create directory: " + dirPath);
                return;
            }
        }

        // Open file in correct directory
        currentFilePath = dirPath + "/" + filename;
        Serial.println("Creating file: " + currentFilePath);
        
        // Remove any existing file with same name
        if (SD.exists(currentFilePath)) {
            SD.remove(currentFilePath);
        }
        
        uploadFile = SD.open(currentFilePath, FILE_WRITE);
        totalBytes = 0;
        
        if (!uploadFile) {
            Serial.println("Failed to open file for writing: " + currentFilePath);
            return;
        }
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            size_t bytesWritten = uploadFile.write(upload.buf, upload.currentSize);
            if (bytesWritten != upload.currentSize) {
                Serial.println("Warning: Bytes written mismatch! Expected: " + 
                               String(upload.currentSize) + ", Actual: " + String(bytesWritten));
            }
            totalBytes += bytesWritten;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            Serial.println("Upload complete, file size: " + String(totalBytes) + " bytes");
            
            // Verify file is readable and has correct size
            verifyUploadedFile(currentFilePath, totalBytes);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) {
            uploadFile.close();
            SD.remove(currentFilePath);
            Serial.println("Upload aborted, file deleted: " + currentFilePath);
        }
    }
}

bool verifyUploadedFile(String filePath, size_t expectedSize) {
    File file = SD.open(filePath, FILE_READ);
    if (!file) {
        Serial.println("VERIFICATION FAILED: Cannot open file: " + filePath);
        return false;
    }
    
    size_t fileSize = file.size();
    
    // Check file size
    if (fileSize != expectedSize) {
        Serial.println("VERIFICATION FAILED: Size mismatch for " + filePath + 
                      ". Expected: " + String(expectedSize) + ", Actual: " + String(fileSize));
        file.close();
        return false;
    }
    
    // Read a few bytes to ensure file is readable
    byte testBuf[16];
    size_t bytesRead = file.read(testBuf, min(16, (int)fileSize));
    
    if (bytesRead <= 0 && fileSize > 0) {
        Serial.println("VERIFICATION FAILED: Cannot read from file: " + filePath);
        file.close();
        return false;
    }
    
    file.close();
    Serial.println("VERIFICATION SUCCESSFUL: " + filePath + 
                  " (Size: " + String(fileSize) + " bytes, readable: yes)");
    return true;
}

void handleUpload() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
   
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{background:#000;color:#0f0;font-family:monospace;text-align:center;margin-top:50px;}</style>";
    html += "</head><body>";
    
    if (server.hasArg("filename")) {
        String filename = server.arg("filename");
        html += "<h2>F1L3 UPL04D 5UCC355FUL!</h2>";
        html += "<p>File '" + filename + "' was successfully uploaded and verified.</p>";
    } else {
        html += "<h2>F1L3 PR0C3553D</h2>";
        html += "<p>Upload complete!</p>";
    }
    
    html += "<p><a href='/'>Return to Terminal</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleForum() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += ".container { border: 1px solid #0f0; padding: 20px; margin: 10px 0; box-shadow: 0 0 10px #0f0; }";
    html += ".thread { border: 1px solid #0f0; margin: 10px 0; padding: 10px; }";
    html += ".thread:hover { box-shadow: 0 0 10px #0f0; }";
    html += ".new-thread { text-align: center; margin: 20px; }";
    html += "form { border: 1px solid #0f0; padding: 20px; }";
    html += "input, textarea { background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; width: 100%; margin: 5px 0; }";
    html += ".cleanup-timer { text-align: center; margin: 20px; padding: 10px; border: 1px solid #0f0; }";

    html += "</style></head><body>";

    html += "<h2>// TERMINAL FORUM //</h2>";

    // Display cleanup timer
    html += "<div class='cleanup-timer'>";
    html += "[NEXT RESET IN: ";
    unsigned long timeLeft = CLEANUP_INTERVAL - (millis() - lastCleanupTime);
    int hoursLeft = timeLeft / 3600000;
    int minutesLeft = (timeLeft % 3600000) / 60000;
    int secondsLeft = (timeLeft % 3600000) / 600000;
    html += String(minutesLeft) + "m " + String(secondsLeft) + "s ]";
    html += "</div>";

    html += "<div class='new-thread'>";
    html += "<div style='text-align: center;'>";
    html += "<a href='/forum/new' style='text-decoration: underline;'>&gt; CREATE NEW THREAD &lt;</a>";
    html += "</div>";
    html += "</div>";

    // Read and display threads
   
    File threadsFile = SD.open("/forum/threads.json", FILE_READ);
    if (threadsFile) {
        String threads = threadsFile.readString();
        threadsFile.close();

        int start = threads.indexOf('[');
        int end = threads.lastIndexOf(']');
        if (start >= 0 && end >= 0) {
            threads = threads.substring(start + 1, end);
            while (threads.length() > 0) {
                int threadEnd = threads.indexOf('}');
                if (threadEnd < 0) break;
               
                String threadData = threads.substring(0, threadEnd + 1);
                String threadId = threadData.substring(threadData.indexOf("\"id\":\"") + 6);
                threadId = threadId.substring(0, threadId.indexOf("\""));
                String threadTitle = threadData.substring(threadData.indexOf("\"title\":\"") + 9);
                threadTitle = threadTitle.substring(0, threadTitle.indexOf("\""));


                html += "<div id='thread-list'>";
                html += "<div class='thread'>";
                html += "<div style='text-align: center;'>";
                html += "<a href='/forum/thread?id=" + threadId + "'>";
                html += "&gt; " + threadTitle + " &lt;";
                html += "</a>";
                html += "</div>";
                html += "</div>";
                html += "</div>";


                threads = threads.substring(threadEnd + 2);
            }
        }
    }

    html += "<div style='text-align: center;'>";
    html += "<br><a href='/' style='text-decoration: underline;'>&lt;&lt; Return to Terminal &gt;&gt;</a>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleThread() {
    String threadId = server.arg("id");
    if (threadId.length() == 0) {
        server.sendHeader("Location", "/forum");
        server.send(303);
        return;
    }

    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += ".container { border: 1px solid #0f0; padding: 20px; margin: 10px 0; box-shadow: 0 0 10px #0f0; }";
    html += ".post { border: 1px solid #0f0; margin: 10px 0; padding: 10px; }";
    html += ".post-header { border-bottom: 1px solid #0f0; padding-bottom: 5px; margin-bottom: 10px; }";
    html += ".post-content { white-space: pre-wrap; }";
    html += "form { border: 1px solid #0f0; padding: 20px; margin-top: 20px; }";
    html += "input, textarea { background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; width: 100%; margin: 5px 0; }";
    html += ".posts-container { width: 95%; margin: 0 auto; max-height: 40vh; overflow-y: auto; border: 1px solid #0f0; padding: 10px; display: flex; flex-direction: column; }";
    html += ".posts-wrapper { display: flex; flex-direction: column; }";
    html += ".posts-container::-webkit-scrollbar { width: 10px; }";
    html += ".posts-container::-webkit-scrollbar-track { background: #000; }";
    html += ".posts-container::-webkit-scrollbar-thumb { background: #0f0; }";
    html += ".posts-container::-webkit-scrollbar-thumb:hover { background: #0a0; }";
    html += ".reply-section { width: 90%; margin: 10px auto; }";
    html += "</style>";

    // Add JavaScript for auto-scrolling and post updates
        html += "<script>";
    html += "function scrollToBottom() {";
    html += "  const container = document.querySelector('.posts-container');";
    html += "  container.scrollTop = container.scrollHeight;";
    html += "}";

    html += "function updatePosts() {";
    html += "  const threadId = '" + threadId + "';";
    html += "  fetch('/thread?id=' + threadId + '&ajax=true')";
    html += "    .then(response => response.text())";
    html += "    .then(html => {";
    html += "      document.querySelector('.posts-wrapper').innerHTML = html;";
    html += "      scrollToBottom();";
    html += "    });";
    html += "}";

    html += "window.onload = function() {";
    html += "  scrollToBottom();";
    html += "  const urlParams = new URLSearchParams(window.location.search);";
    html += "  if(urlParams.get('scroll') === 'true') {";
    html += "    scrollToBottom();";
    html += "    document.getElementById('reply').scrollIntoView();";
    html += "  }";
    html += "};";

    html += "setInterval(updatePosts, 2000);"; // Refresh posts every 2 seconds
    html += "</script>";
    html += "</head><body>";

    // Find thread title
    File threadsFile = SD.open("/forum/threads.json", FILE_READ);
    String threadTitle = "Unknown Thread";
    if (threadsFile) {
        String threads = threadsFile.readString();
        threadsFile.close();
       
        int threadStart = threads.indexOf("\"id\":\"" + threadId + "\"");
        if (threadStart >= 0) {
            int titleStart = threads.indexOf("\"title\":\"", threadStart) + 9;
            int titleEnd = threads.indexOf("\"", titleStart);
            threadTitle = threads.substring(titleStart, titleEnd);
        }
    }

    html += "<h1>// " + threadTitle + " //</h1>";

    // Posts container (this part gets refreshed)
    html += "<div class='posts-container'>";
    html += "<div class='posts-wrapper'>";

    // Store posts in an array first
    std::vector<String> postsList;
    String postsPath = "/forum/posts/" + threadId + ".json";
    File postsFile = SD.open(postsPath, FILE_READ);
    if (postsFile) {
        String posts = postsFile.readString();
        postsFile.close();

        int start = posts.indexOf('[');
        int end = posts.lastIndexOf(']');
        if (start >= 0 && end >= 0) {
            posts = posts.substring(start + 1, end);
            while (posts.length() > 0) {
                int postEnd = posts.indexOf('}');
                if (postEnd < 0) break;
               
                String postData = posts.substring(0, postEnd + 1);
                postsList.push_back(postData);
                posts = posts.substring(postEnd + 2);
            }
        }
    }

    // Display posts in chronological order
    for (const String& postData : postsList) {
        String author = postData.substring(postData.indexOf("\"author\":\"") + 10);
        author = author.substring(0, author.indexOf("\""));
        String content = postData.substring(postData.indexOf("\"content\":\"") + 11);
        content = content.substring(0, content.indexOf("\""));
        String timestamp = postData.substring(postData.indexOf("\"timestamp\":\"") + 13);
        timestamp = timestamp.substring(0, timestamp.indexOf("\""));

        html += "<div class='post'>";
        html += "<div class='post-header'>";
        html += "[ USER: " + author + " " + formatTimestamp(timestamp.toInt()) + " ]";
        html += "</div>";
        html += "<div class='post-content'>" + content + "</div>";
        html += "</div>";
    }

    html += "</div>"; // Close posts-wrapper
    html += "</div>"; // Close posts-container

    // Reply section (outside the refreshing container)
    html += "<div class='reply-section'>";    
    html += "<form id='reply' method='post' action='/forum/post'>";
    html += "<input type='hidden' name='threadId' value='" + threadId + "'>";
    html += "<input type='text' name='author' placeholder='Your Handle' required><br>";
    html += "<textarea name='content' placeholder='Your Reply' rows='5' required></textarea><br>";
    html += "<input type='submit' value='POST REPLY'>";
    html += "</form>";
    html += "</div>";

    html += "<div style='text-align: center;'>";
    html += "<br><a href='/forum' >&lt;&lt; Back to Forum &gt;&gt;</a>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleThreadAjax() {
    if (!server.hasArg("ajax")) {
        handleThread();
        return;
    }

    String threadId = server.arg("id");
    String html = "";
    std::vector<String> postsList;
    String postsPath = "/forum/posts/" + threadId + ".json";
   
    File postsFile = SD.open(postsPath, FILE_READ);
    if (postsFile) {
        String posts = postsFile.readString();
        postsFile.close();

        int start = posts.indexOf('[');
        int end = posts.lastIndexOf(']');
        if (start >= 0 && end >= 0) {
            posts = posts.substring(start + 1, end);
            while (posts.length() > 0) {
                int postEnd = posts.indexOf('}');
                if (postEnd < 0) break;
               
                String postData = posts.substring(0, postEnd + 1);
                postsList.push_back(postData);
                posts = posts.substring(postEnd + 2);
            }
        }
    }

    // Display posts in chronological order
    for (const String& postData : postsList) {
        String author = postData.substring(postData.indexOf("\"author\":\"") + 10);
        author = author.substring(0, author.indexOf("\""));
        String content = postData.substring(postData.indexOf("\"content\":\"") + 11);
        content = content.substring(0, content.indexOf("\""));
        String timestamp = postData.substring(postData.indexOf("\"timestamp\":\"") + 13);
        timestamp = timestamp.substring(0, timestamp.indexOf("\""));

        html += "<div class='post'>";
        html += "<div class='post-header'>";
        html += "[ USER: " + author + " " + formatTimestamp(timestamp.toInt()) + " ]";
        html += "</div>";
        html += "<div class='post-content'>" + content + "</div>";
        html += "</div>";
    }
   
    server.send(200, "text/html", html);
}

void handleNewThread() {
    if (server.method() == HTTP_POST) {
        Serial.println("\n--- Starting New Thread Creation ---");
       
        // Validate form data exists
        if (!server.hasArg("title") || !server.hasArg("content") || !server.hasArg("author")) {
            Serial.println("Error: Missing form data");
            String errorHtml = "<html><head><meta http-equiv='refresh' content='3;url=/forum'></head>";
            errorHtml += "<body>Missing form data. Redirecting...</body></html>";
            server.send(400, "text/html", errorHtml);
            return;
        }

        String title = server.arg("title");
        String content = server.arg("content");
        String author = server.arg("author");
       
        Serial.println("Title: " + title);
        Serial.println("Author: " + author);
        Serial.println("Content length: " + String(content.length()));

        // Generate thread ID first
        String threadId = String(millis());
       
        // Prepare error response HTML
        String errorHtml = "<html><head>";
        errorHtml += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        errorHtml += "<meta http-equiv='refresh' content='3;url=/forum'>";
        errorHtml += "<style>";
        errorHtml += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; text-align: center; }";
        errorHtml += "</style></head><body>";
       
        // Ensure directories exist
        if (!SD.exists("/forum")) {
            if (!SD.mkdir("/forum")) {
                Serial.println("Error: Failed to create /forum directory");
                errorHtml += "<p>Failed to create forum directory. Redirecting...</p></body></html>";
                server.send(500, "text/html", errorHtml);
                return;
            }
        }
       
        if (!SD.exists("/forum/posts")) {
            if (!SD.mkdir("/forum/posts")) {
                Serial.println("Error: Failed to create /forum/posts directory");
                errorHtml += "<p>Failed to create posts directory. Redirecting...</p></body></html>";
                server.send(500, "text/html", errorHtml);
                return;
            }
        }

        // Create threads.json if it doesn't exist
        if (!SD.exists("/forum/threads.json")) {
            File threadsFile = SD.open("/forum/threads.json", FILE_WRITE);
            if (threadsFile) {
                threadsFile.println("[]");
                threadsFile.close();
            }
        }

        // Read existing threads
        String threads = "[]";
        File threadsFile = SD.open("/forum/threads.json", FILE_READ);
        if (threadsFile) {
            threads = threadsFile.readString();
            threadsFile.close();
            threads.trim();
            if (!threads.startsWith("[")) {
                threads = "[]";
            }
        }

        // Create new thread JSON
        String newThreadData;
        if (threads == "[]") {
            newThreadData = "[{\"id\":\"" + threadId + "\",";
        } else {
            newThreadData = threads.substring(0, threads.length() - 1) + ",{\"id\":\"" + threadId + "\",";
        }
        newThreadData += "\"title\":\"" + title + "\",";
        newThreadData += "\"author\":\"" + author + "\",";
        newThreadData += "\"timestamp\":\"" + String(millis()) + "\"}]";

        // Write new thread data
        SD.remove("/forum/threads.json");
        threadsFile = SD.open("/forum/threads.json", FILE_WRITE);
        if (!threadsFile) {
            Serial.println("Error: Failed to open threads.json for writing");
            errorHtml += "<p>Failed to create thread file. Redirecting...</p></body></html>";
            server.send(500, "text/html", errorHtml);
            return;
        }
       
        threadsFile.print(newThreadData);
        threadsFile.close();

        // Create initial post file
        String postPath = "/forum/posts/" + threadId + ".json";
        File postFile = SD.open(postPath, FILE_WRITE);
        if (!postFile) {
            Serial.println("Error: Failed to create post file");
            errorHtml += "<p>Failed to create post file. Redirecting...</p></body></html>";
            server.send(500, "text/html", errorHtml);
            return;
        }

        String postData = "[{\"id\":\"" + String(millis()) + "\",";
        postData += "\"author\":\"" + author + "\",";
        postData += "\"content\":\"" + content + "\",";
        postData += "\"timestamp\":\"" + String(millis()) + "\"}]";
       
        postFile.print(postData);
        postFile.close();

        // Success - redirect to new thread
        server.sendHeader("Location", "/forum/thread?id=" + threadId);
        server.send(303);
        return;
    }

    // Show the new thread form
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += "form { border: 1px solid #0f0; padding: 20px; margin-top: 20px; }";
    html += "input, textarea { background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; width: 100%; margin: 5px 0; }";
    html += "input[type='submit'] { cursor: pointer; }";
    html += "input[type='submit']:hover { background: #0f0; color: #000; }";
    html += "</style></head><body>";
   
    html += "<h2>// NEW THREAD //</h2>";
    html += "<form method='post' action='/forum/new'>";  // Added explicit action
    html += "<input type='text' name='author' placeholder='Your Handle' required><br>";
    html += "<input type='text' name='title' placeholder='Thread Title' required><br>";
    html += "<textarea name='content' placeholder='Content' rows='5' required></textarea><br>";
    html += "<input type='submit' value='CREATE THREAD'>";
    html += "</form>";
   
    html += "<div style='text-align: center;'>";
    html += "<br><a href='/forum'>&lt;&lt; Back to Forum &gt;&gt;</a>";
    html += "</div>";
    html += "</body></html>";
   
    server.send(200, "text/html", html);
}

void handleNewPost() {
    if (server.method() == HTTP_POST) {
        String threadId = server.arg("threadId");
        String content = server.arg("content");
        String author = server.arg("author");
       
        // Input validation
        if (threadId.length() == 0 || content.length() == 0 || author.length() == 0) {
            server.send(400, "text/plain", "Missing required fields");
            return;
        }
       
        String postsPath = "/forum/posts/" + threadId + ".json";
       
        // Check if thread exists
        if (!SD.exists(postsPath)) {
            server.send(404, "text/plain", "Thread not found");
            return;
        }
       
        // Read existing posts
        File postsFile = SD.open(postsPath, FILE_READ);
        String currentPosts = "[]";
        if (postsFile) {
            currentPosts = postsFile.readString();
            postsFile.close();
           
            // Validate JSON format
            currentPosts.trim();
            if (!currentPosts.startsWith("[")) {
                currentPosts = "[]";
            }
        }
       
        // Create new post JSON
        String newPost = "{\"id\":\"" + String(millis()) + "\",";
        newPost += "\"author\":\"" + author + "\",";
        newPost += "\"content\":\"" + content + "\",";
        newPost += "\"timestamp\":\"" + String(millis()) + "\"}";
       
        // Prepare updated JSON
        String updatedPosts;
        if (currentPosts == "[]") {
            updatedPosts = "[" + newPost + "]";
        } else {
            updatedPosts = currentPosts.substring(0, currentPosts.length() - 1) + "," + newPost + "]";
        }
       
        // Write updated posts
        SD.remove(postsPath); // Remove old file
        postsFile = SD.open(postsPath, FILE_WRITE);
        if (postsFile) {
            postsFile.print(updatedPosts);
            postsFile.close();
            Serial.println("Post added successfully");
           
            server.sendHeader("Location", "/forum/thread?id=" + threadId + "&scroll=true#bottom");
            server.send(303);
            return;
        } else {
            Serial.println("Failed to write post");
            server.send(500, "text/plain", "Failed to save post");
            return;
        }
    }
   
    server.sendHeader("Location", "/forum");
    server.send(303);
}

void handleToggle() {
    ledState = !ledState;
    digitalWrite(ledPin, ledState);
    server.sendHeader("Location", "/");
    server.send(303);
}
