// Include libraries
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <SD.h>
#include <map>
#include <vector>
#include <algorithm>
#include <list>
#include <set>
#include <espnow.h>

// Hardware settings
SPISettings spiSettings(40000000, MSBFIRST, SPI_MODE0);  // Faster SPI settings for SD
const int SD_CS_PIN = D8;
const int ledPin = LED_BUILTIN;


// Constants - grouped by type
// Network constants
const int MAX_AP_CLIENTS = 8; // Max AP Clients
const byte DNS_PORT = 53;     // DNS server port
const int MAX_PEERS = 20;     // Max Peers
// Timing constants
const unsigned long BROADCAST_INTERVAL = 7500;     // Reduced frequency
const unsigned long NODE_INFO_INTERVAL = 5000;     // Adjusted timing for better performance
const unsigned long PEER_TIMEOUT = 15000;          // Increased to account for packet loss
const unsigned long CHUNK_TIMEOUT = 30000;         // 30 seconds timeout for incomplete transfers
const unsigned long CLEANUP_INTERVAL = 3600000;    // 1 hour
// Performance constants  
const int SD_BUFFER_SIZE = 4096;     //Larger buffer for SD operations
const uint16_t MAX_CHUNK_SIZE = 240;
const int MAX_RETRY = 3;
// Access Point settings
const char* serverName = "r04m1ng.l1br4ry";  // DNS name for captive portal
const char* AP_SSID_BASE = "B00KM4RK_";  // Base name for SSID
IPAddress apIP(192, 168, 4, 1);        // IP address of the NodeMCU in AP mode

//Structures
// Data Structures - packed 
struct __attribute__((packed)) SharedFile {
    char filepath[64];  // Increased size to hold full path + filename
    size_t filesize;
    uint8_t sourceNode[6];
    uint8_t padding[2];  // Add padding to ensure 4-byte alignment
    char nodeSSID[32];
};

struct __attribute__((packed)) NodeInfo {
    char ssid[32];
    uint8_t mac[6];
    uint8_t padding[2];  // Add padding to ensure 4-byte alignment
};

struct __attribute__((packed)) FileChunk {
    char filename[32];
    uint32_t chunkIndex;
    uint32_t totalChunks;
    uint16_t chunkSize;
    uint8_t padding[2];    // Add padding for 4-byte alignment
    uint8_t data[240];     // ESP-NOW has max packet size of ~250 bytes
};

//Regular Structures
struct PeerInfo {
    uint8_t mac[6];
    unsigned long lastSeen;
    String ssid;
};

struct SendStatus {
    bool success;
    int retryCount;
    unsigned long lastTry;
};

struct FileTransfer {
    String filename;
    std::vector<bool> receivedChunks;
    uint32_t totalChunks;
    unsigned long lastReceived;
};

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


// Global Variables
String AP_SSID;  // Full name with number
// LED state
bool ledState = false;
//WiFi check
unsigned long lastWiFiCheck = 0; 
// Forum cleanup
unsigned long lastCleanupTime = 0;
// File Uploads
File uploadFile;

//URLEncode
String urlencode(const String& str) {
  String encoded = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      char hex[4];
      sprintf(hex, "%%%02X", c);
      encoded += hex;
    }
  }
  return encoded;
}

//Collections
std::list<PeerInfo> peers;
std::list<SharedFile> remoteFiles;
std::map<String, SendStatus> sendStatuses;
std::map<String, FileTransfer> activeTransfers;
uint8_t peerMac[6];
   
// Web server object
ESP8266WebServer server(80);
DNSServer dnsServer;


// Function declarations
// Utility/Helper Functions
bool addPeer(uint8_t *mac);
bool connectToPeer(const String& peerSSID);
String formatTimestamp(unsigned long timestamp);
// Core Infrastructure
void checkWiFiMode();
void setupWiFiAP();
void setupDnsRedirect();
void disconnectFromPeer(const uint8_t* mac);
void updatePeerInfo(const uint8_t* mac, const char* ssid);
void checkPeerTimeouts();
void broadcastNodeInfo();
void broadcastFiles();
void sendFileInChunks(const char* filename, File& file, uint8_t* targetMac);
void cleanupStaleTransfers();
// Directory & Forum Management
void initializeDirectories();
void initializeForum();
void checkAndCleanupForum();
void cleanupForum();
void cleanupPosts();
void removeDirectory(const char * path);
// Web Handlers
void handleRoot();
void handleRedirect();
void handleNotFound();
void handleCaptivePortal();
void handlePortal();
void handleFileList();
void handleNodeFiles();
void handleFileDownload();
void handleUploadPage();
void handleFileUpload();
bool verifyUploadedFile(String filePath, size_t expectedSize);
void handleUpload();
void handleForum();
void handleThread();
void handleThreadAjax();
void handleNewThread();
void handleNewPost();
void handleToggle();


//FUNCTIONS
//UTILITY/HELPER FUNCTIONS
bool isAllowedFile(const String& filename) {
    String lowerFilename = filename;
    lowerFilename.toLowerCase();
    lowerFilename.trim();  // Remove whitespace just in case
    
    Serial.println("Checking file type: " + lowerFilename);

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

bool addPeer(uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }

    if (esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, 1, NULL, 0) != 0) {
        Serial.println("Failed to add peer");
        return false;
    }
    return true;
}

bool captivePortal() {
    if (!isIp(server.hostHeader())) {
        server.sendHeader("Location", String("http://") + toStringIp(apIP), true);
        server.send(302, "text/plain", "");
        return true;
    }
    return false;
}

bool connectToPeer(const String& peerSSID) {
    Serial.println("Attempting to connect to peer: " + peerSSID);
    
    // Save current WiFi state
    uint8_t currentMode = WiFi.getMode();
    
    for (const auto& peer : peers) {
        if (peer.ssid == peerSSID) {
            uint8_t tempMac[6];
            memcpy(tempMac, peer.mac, 6);
            
            if (!esp_now_is_peer_exist(tempMac)) {
                // Ensure we maintain AP functionality
                if (currentMode != WIFI_AP_STA) {
                    WiFi.mode(WIFI_AP_STA);
                }
                
                if (esp_now_add_peer(tempMac, ESP_NOW_ROLE_COMBO, 1, NULL, 0) != 0) {
                    Serial.println("Failed to add peer");
                    return false;
                }
                Serial.println("Connected to peer: " + peerSSID);
            }
            return true;
        }
    }
    
    Serial.println("Peer not found: " + peerSSID);
    return false;
}

bool createForumDirectories() {
    Serial.println("\n--- Creating Forum Directories ---");
   
    // First check if SD card is working
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("Error: SD card not initialized!");
        return false;
    }
   
    // Try to create parent directory first
    if (!SD.exists("/forum")) {
        Serial.println("Creating /forum directory...");
        if (!SD.mkdir("/forum")) {
            Serial.println("Failed to create /forum directory!");
            Serial.println("Checking if card is writable...");
           
            // Test write access
            File testFile = SD.open("/test.txt", FILE_WRITE);
            if (!testFile) {
                Serial.println("Cannot write to SD card!");
                return false;
            }
            testFile.close();
            SD.remove("/test.txt");
            return false;
        }
        Serial.println("/forum directory created successfully");
    } else {
        Serial.println("/forum directory already exists");
    }
   
    // Small delay between operations
    delay(100);
   
    // Now create posts directory
    if (!SD.exists("/forum/posts")) {
        Serial.println("Creating /forum/posts directory...");
        if (!SD.mkdir("/forum/posts")) {
            Serial.println("Failed to create /forum/posts directory!");
            return false;
        }
        Serial.println("/forum/posts directory created successfully");
    } else {
        Serial.println("/forum/posts directory already exists");
    }
   
    // Verify directories exist
    bool forumExists = SD.exists("/forum");
    bool postsExists = SD.exists("/forum/posts");
   
    Serial.println("\nDirectory Status:");
    Serial.println("/forum exists: " + String(forumExists ? "Yes" : "No"));
    Serial.println("/forum/posts exists: " + String(postsExists ? "Yes" : "No"));
   
    return forumExists && postsExists;
}

// Get MAC string
String getMacString(uint8_t* mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

String formatTimestamp(unsigned long timestamp) {
    unsigned long timeDiff = millis() - timestamp;
    if (timeDiff < 60000) return String(timeDiff / 1000) + "s ago";
    if (timeDiff < 3600000) return String(timeDiff / 60000) + "m ago";
    if (timeDiff < 86400000) return String(timeDiff / 3600000) + "h ago";
    return String(timeDiff / 86400000) + "d ago";
}

String toStringIp(IPAddress ip) {
    String res = "";
    for (int i = 0; i < 3; i++) {
        res += String((ip >> (8 * i)) & 0xFF) + ".";
    }
    res += String(((ip >> 8 * 3)) & 0xFF);
    return res;
}


//CORE INFRASTRUCTURE
// Monitor AP status
/*
//For remote nodes
void checkAPStatus() {
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastCheck >= 10000) {  // Check every 10 seconds
        // Check if AP is still running and configured correctly
        if (!WiFi.softAPIP() || WiFi.softAPgetStationNum() > MAX_AP_CLIENTS) {
            Serial.println("AP Status Check - Restoring AP...");
            recoverAP();
        }
        
        lastCheck = currentTime;
    }
}
*/
//For local node only
void checkAPStatus() {
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();
    
    // Check every 10 seconds
    if (currentTime - lastCheck >= 10000) {
        int clientCount = WiFi.softAPgetStationNum();
        Serial.printf("AP Status - Connected Clients: %d/%d\n", clientCount, MAX_AP_CLIENTS);
        
        // If AP seems to be having issues, try to restart it
        if (!WiFi.softAPIP()) {
            Serial.println("AP IP not valid, attempting to restart AP...");
            setupWiFiAP();
        }
        
        lastCheck = currentTime;
    }
}
void checkWiFiMode() {
    if (WiFi.getMode() != WIFI_AP_STA) {
        Serial.println("WiFi mode changed, restoring AP+STA");
        WiFi.mode(WIFI_AP_STA);
        delay(100);  // Give some time for mode change
    }
}
/*
// ESP-NOW callback function for received data
void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
    digitalWrite(ledPin, HIGH);
    delay(50);
    digitalWrite(ledPin, LOW);
    
    if (len == sizeof(NodeInfo)) {
        NodeInfo nodeInfo;
        memcpy(&nodeInfo, data, sizeof(NodeInfo));
        updatePeerInfo(mac, nodeInfo.ssid);
    }
    else if (len == sizeof(FileChunk)) {
        FileChunk chunk;
        memcpy(&chunk, data, len);
        
        String filename = String(chunk.filename);
        
        // Initialize transfer if new
        if (activeTransfers.find(filename) == activeTransfers.end()) {
            FileTransfer transfer;
            transfer.filename = filename;
            transfer.totalChunks = chunk.totalChunks;
            transfer.receivedChunks.resize(chunk.totalChunks, false);
            transfer.lastReceived = millis();
            activeTransfers[filename] = transfer;
        }
        
        // Update transfer status
        FileTransfer& transfer = activeTransfers[filename];
        transfer.lastReceived = millis();
        transfer.receivedChunks[chunk.chunkIndex] = true;
        
        // Write chunk to SD
        String tempFilename = "/Alexandria/." + filename + ".tmp";
        File file = SD.open(tempFilename.c_str(), FILE_WRITE);
        if (file) {
            file.seek(chunk.chunkIndex * MAX_CHUNK_SIZE);
            file.write(chunk.data, chunk.chunkSize);
            file.close();
            
            // Check if transfer is complete
            bool complete = true;
            for (bool received : transfer.receivedChunks) {
                if (!received) {
                    complete = false;
                    break;
                }
            }
            
            if (complete) {
                // Rename temp file to final name
                String finalPath = "/Alexandria/" + filename;
                SD.remove(finalPath);
                SD.rename(tempFilename.c_str(), finalPath.c_str());
                activeTransfers.erase(filename);
                Serial.println("File transfer complete: " + filename);
            }
        }
    }
    else if (len == sizeof(SharedFile)) {
        SharedFile receivedFile;
        memcpy(&receivedFile, data, sizeof(SharedFile));
        
        Serial.println("\n--- Received File Info ---");
        Serial.printf("File: %s\n", receivedFile.filepath);
        Serial.printf("From Node: %s\n", receivedFile.nodeSSID);
        Serial.printf("Size: %u bytes\n", receivedFile.filesize);
        Serial.print("Source MAC: ");
        for (int i = 0; i < 6; i++) {
            Serial.printf("%02X", receivedFile.sourceNode[i]);
            if (i < 5) Serial.print(":");
        }
        Serial.println();
        
        // Update peer info from file message as backup
        updatePeerInfo(mac, receivedFile.nodeSSID);
        
        // Handle the received file info
        bool fileFound = false;
        for (auto &file : remoteFiles) {
            if (strcmp(file.filepath, receivedFile.filepath) == 0 &&
                memcmp(file.sourceNode, receivedFile.sourceNode, 6) == 0) {
                file = receivedFile;
                fileFound = true;
                Serial.println("Updated existing file info");
                break;
            }
        }
        
        if (!fileFound) {
            remoteFiles.push_back(receivedFile);
            Serial.println("Added new file info");
            Serial.println("Total remote files: " + String(remoteFiles.size()));
        }
    }
}

// Initialize ESP-NOW in setup()
void initializeESPNow() {
    // Initialize ESP-NOW with retry
    int initRetry = 0;
    while (esp_now_init() != 0) {
        Serial.println("Error initializing ESP-NOW, retrying...");
        delay(100);
        if (++initRetry >= 5) {
            Serial.println("ESP-NOW init failed after 5 attempts");
            return;
        }
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    
    // Use lambda with correct signature
    esp_now_register_recv_cb([](uint8_t *mac, uint8_t *data, uint8_t len) {
        onDataReceived(mac, data, len);
    });
    
    esp_now_register_send_cb([](uint8_t *mac_addr, uint8_t sendStatus) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        
        if (sendStatus != 0) {
            String macString = String(macStr);
            if (sendStatuses.find(macString) != sendStatuses.end()) {
                sendStatuses[macString].success = false;
                sendStatuses[macString].retryCount++;
            }
        }
    });
}
*/
void setupWiFiAP() {
    Serial.println("\n--- Setting up WiFi Access Point ---");
    
    // Clear existing WiFi configuration
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(100);
    
    // Generate SSID
    randomSeed(analogRead(0));
    int randomNum = random(0, 1000);
    AP_SSID = String(AP_SSID_BASE) + String(randomNum < 100 ? "0" : "") + String(randomNum);
    Serial.println("Generated SSID: " + AP_SSID);
    
    // Set mode to AP+STA
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    
    // Configure soft-AP
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    delay(100);
    
    // Start AP with specific settings
    bool apStartSuccess = false;
    int retryCount = 0;
    while (!apStartSuccess && retryCount < 3) {
        apStartSuccess = WiFi.softAP(AP_SSID.c_str(), NULL, 1, 0, MAX_AP_CLIENTS);
        if (!apStartSuccess) {
            Serial.printf("AP Start attempt %d failed, retrying...\n", retryCount + 1);
            WiFi.mode(WIFI_OFF);
            delay(1000);
            WiFi.mode(WIFI_AP_STA);
            delay(100);
            retryCount++;
        }
    }
    
    if (apStartSuccess) {
        Serial.println("Access Point Started Successfully");
        Serial.printf("SSID: %s\n", AP_SSID.c_str());
        Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("Max Clients: %d\n", MAX_AP_CLIENTS);
        
        // Add event handlers for client connections
        WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected& evt) {
            Serial.printf("Client Connected - MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                evt.mac[0], evt.mac[1], evt.mac[2], evt.mac[3], evt.mac[4], evt.mac[5]);
            Serial.printf("Total Clients: %d\n", WiFi.softAPgetStationNum());
        });
        
        WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected& evt) {
            Serial.printf("Client Disconnected - MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                evt.mac[0], evt.mac[1], evt.mac[2], evt.mac[3], evt.mac[4], evt.mac[5]);
            Serial.printf("Total Clients: %d\n", WiFi.softAPgetStationNum());
        });
    } else {
        Serial.println("Access Point Start Failed after multiple attempts!");
    }
}

// Enhanced DNS server handlers for desktop browsers
void setupDnsRedirect() {
  // Configure DNS server to redirect all domains to the ESP's IP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
  
  // Set up handlers for desktop browsers
  server.on("/hotspot-detect.html", handleRedirect);  // Safari/Mac
  server.on("/ncsi.txt", handleRedirect);            // Edge/Windows
  server.on("/generate_204", handleRedirect);        // Chrome
  server.on("/redirect", handleRedirect);            // Firefox
  server.on("/success.txt", handleRedirect);         // Some macOS/iOS
  
  // Set up the general "not found" handler
  server.onNotFound(handleNotFound);
}

/*
void recoverAP() {
    Serial.println("Attempting AP recovery...");
    
    // Save ESP-NOW peers
    std::vector<std::array<uint8_t, 6>> savedPeers;
    for (const auto& peer : peers) {
        std::array<uint8_t, 6> tempMac;
        memcpy(tempMac.data(), peer.mac, 6);
        uint8_t *macPtr = tempMac.data();
        if (esp_now_is_peer_exist(macPtr)) {
            savedPeers.push_back(tempMac);
            esp_now_del_peer(macPtr);
        }
    }
    
    // Temporarily disable ESP-NOW
    esp_now_deinit();
    
    // Reset WiFi
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(500);
    
    // Restore AP mode
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    
    // Reconfigure AP
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    delay(100);
    
    // Restart AP
    int retryCount = 0;
    bool apStarted = false;
    while (!apStarted && retryCount < 3) {
        apStarted = WiFi.softAP(AP_SSID.c_str(), NULL, 1, 0, MAX_AP_CLIENTS);
        if (!apStarted) {
            Serial.printf("AP Start attempt %d failed, retrying...\n", retryCount + 1);
            delay(1000);
            retryCount++;
        }
    }
    
    if (apStarted) {
        Serial.println("AP recovered successfully");
        Serial.printf("SSID: %s\n", AP_SSID.c_str());
        Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
        
        // Reinitialize ESP-NOW
        if (esp_now_init() == 0) {
            esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
            
            // Restore ESP-NOW callbacks
            esp_now_register_recv_cb([](uint8_t *mac, uint8_t *data, uint8_t len) {
                onDataReceived(mac, data, len);
            });
            
            esp_now_register_send_cb([](uint8_t *mac_addr, uint8_t sendStatus) {
                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                Serial.printf("Send Status for %s: %s\n", macStr, sendStatus == 0 ? "Success" : "Fail");
            });
            
            // Restore peers
            for (auto& peerMac : savedPeers) {
              uint8_t *macPtr = peerMac.data();
              addPeer(macPtr);
            }
            
            Serial.println("ESP-NOW recovered");
        }
    } else {
        Serial.println("AP recovery failed!");
    }
}
*/
void updateStatusLED() {
    static unsigned long lastBlink = 0;
    unsigned long currentTime = millis();
    
    // Blink pattern based on status
    if (WiFi.softAPgetStationNum() > 0) {
        // Connected clients - solid LED
        digitalWrite(ledPin, HIGH);
    } else if (WiFi.softAPIP()) {
        // AP running but no clients - slow blink
        if (currentTime - lastBlink >= 1000) {
            digitalWrite(ledPin, !digitalRead(ledPin));
            lastBlink = currentTime;
        }
    } else {
        // AP issues - fast blink
        if (currentTime - lastBlink >= 200) {
            digitalWrite(ledPin, !digitalRead(ledPin));
            lastBlink = currentTime;
        }
    }
}


//NODE MANAGEMENT
/*
void updateNodeLinking() {
    static unsigned long lastBroadcast = 0;
    static unsigned long lastNodeInfo = 0;
    unsigned long currentTime = millis();
    
    // Check for peer timeouts
    checkPeerTimeouts();
    
    // Broadcast node info
    if (currentTime - lastNodeInfo >= NODE_INFO_INTERVAL) {
        Serial.println("Broadcasting node info...");
        broadcastNodeInfo();
        lastNodeInfo = currentTime;
    }
    
    // Broadcast files
     if (currentTime - lastBroadcast >= BROADCAST_INTERVAL) {
        Serial.printf("\nTime since last broadcast: %lums\n", currentTime - lastBroadcast);
        Serial.println("Broadcasting files...");
        broadcastFiles();
        lastBroadcast = currentTime;
        
        // Log current peers
        Serial.println("\nCurrent peers:");
        for (const auto& peer : peers) {
            Serial.printf("SSID: %s, Last seen: %lus ago\n",
                         peer.ssid.c_str(),
                         (currentTime - peer.lastSeen) / 1000);
        }
    }
}

void updatePeerInfo(const uint8_t* mac, const char* ssid) {
    bool peerFound = false;
    unsigned long currentTime = millis();
    
    for (auto &peer : peers) {
        if (memcmp(peer.mac, mac, 6) == 0) {
            peer.lastSeen = currentTime;
            peer.ssid = String(ssid);
            peerFound = true;
            Serial.printf("Updated peer: %s\n", ssid);
            break;
        }
    }
    
    if (!peerFound && peers.size() < MAX_PEERS) {
        PeerInfo newPeer;
        memcpy(newPeer.mac, mac, 6);
        newPeer.lastSeen = currentTime;
        newPeer.ssid = String(ssid);
        peers.push_back(newPeer);
        Serial.printf("Added new peer: %s\n", ssid);
    }
}

void checkPeerTimeouts() {
    unsigned long currentTime = millis();
    peers.remove_if([currentTime](const PeerInfo &peer) {
        bool removing = (currentTime - peer.lastSeen) > PEER_TIMEOUT;
        if (removing) {
            Serial.printf("Removing inactive peer: %s\n", peer.ssid.c_str());
        }
        return removing;
    });
}

void disconnectFromPeer(const uint8_t* mac) {
    uint8_t tempMac[6];
    memcpy(tempMac, mac, 6);
    
    if (esp_now_is_peer_exist(tempMac)) {
        esp_now_del_peer(tempMac);
        Serial.println("Disconnected from peer");
    }
}

void broadcastNodeInfo() {
    NodeInfo nodeInfo = {};  // Zero-initialize the structure
    strncpy(nodeInfo.ssid, AP_SSID.c_str(), sizeof(nodeInfo.ssid) - 1);
    WiFi.macAddress(nodeInfo.mac);
    memset(nodeInfo.padding, 0, sizeof(nodeInfo.padding));  // Explicitly zero padding
    
    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcastMac, (uint8_t*)&nodeInfo, sizeof(nodeInfo));
}

void broadcastFiles() {
    static std::vector<SharedFile> failedTransmissions;
   
    Serial.println("\n--- Starting File Broadcast ---");
    Serial.printf("Number of remote files: %d\n", remoteFiles.size());

    // First, retry any failed transmissions
    auto it = failedTransmissions.begin();
    while (it != failedTransmissions.end()) {
        String macStr = getMacString(it->sourceNode);
        if (sendStatuses[macStr].retryCount < MAX_RETRY) {
            if (millis() - sendStatuses[macStr].lastTry >= 100) {  // Wait 100ms between retries
                uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                if (esp_now_send(broadcastMac, (uint8_t*)&(*it), sizeof(SharedFile)) == 0) {
                    sendStatuses[macStr].lastTry = millis();
                    it = failedTransmissions.erase(it);
                    continue;
                }
            }
        } else {
            it = failedTransmissions.erase(it);
            continue;
        }
        ++it;
    }

    // Then broadcast files from subdirectories
    for (char c = 'A'; c <= 'Z'; c++) {
        String dirPath = "/Alexandria/" + String(c);
        File dir = SD.open(dirPath);
        if (dir) {
            while (File file = dir.openNextFile()) {
                if (!file.isDirectory()) {
                    String fileName = String(file.name());
                    if (isAllowedFile(fileName)) {
                        SharedFile sharedFile;
                        String fullPath = dirPath + "/" + fileName;
                        strncpy(sharedFile.filepath, fullPath.c_str(), sizeof(sharedFile.filepath) - 1);
                        sharedFile.filesize = file.size();
                        WiFi.macAddress(sharedFile.sourceNode);
                        strncpy(sharedFile.nodeSSID, AP_SSID.c_str(), sizeof(sharedFile.nodeSSID) - 1);
                        
                        uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                        if (esp_now_send(broadcastMac, (uint8_t*)&sharedFile, sizeof(SharedFile)) != 0) {
                            failedTransmissions.push_back(sharedFile);
                            String macStr = getMacString(sharedFile.sourceNode);
                            sendStatuses[macStr] = {false, 0, millis()};
                        }
                    }
                }
                file.close();
                yield();
            }
            dir.close();
        }
    }

    // Check numeric directory (0-9)
    File numDir = SD.open("/Alexandria/0-9");
    if (numDir) {
        while (File file = numDir.openNextFile()) {
            if (!file.isDirectory()) {
                String fileName = String(file.name());
                if (isAllowedFile(fileName)) {
                    SharedFile sharedFile;
                    String fullPath = "/Alexandria/0-9/" + fileName;
                    strncpy(sharedFile.filepath, fullPath.c_str(), sizeof(sharedFile.filepath) - 1);
                    sharedFile.filesize = file.size();
                    WiFi.macAddress(sharedFile.sourceNode);
                    strncpy(sharedFile.nodeSSID, AP_SSID.c_str(), sizeof(sharedFile.nodeSSID) - 1);
                    
                    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                    if (esp_now_send(broadcastMac, (uint8_t*)&sharedFile, sizeof(SharedFile)) != 0) {
                        failedTransmissions.push_back(sharedFile);
                        String macStr = getMacString(sharedFile.sourceNode);
                        sendStatuses[macStr] = {false, 0, millis()};
                    }
                }
            }
            file.close();
            yield();
        }
        numDir.close();
    }

    // Check symbols directory (#@)
    File symDir = SD.open("/Alexandria/#@");
    if (symDir) {
        while (File file = symDir.openNextFile()) {
            if (!file.isDirectory()) {
                String fileName = String(file.name());
                if (isAllowedFile(fileName)) {
                    SharedFile sharedFile;
                    String fullPath = "/Alexandria/#@/" + fileName;
                    strncpy(sharedFile.filepath, fullPath.c_str(), sizeof(sharedFile.filepath) - 1);
                    sharedFile.filesize = file.size();
                    WiFi.macAddress(sharedFile.sourceNode);
                    strncpy(sharedFile.nodeSSID, AP_SSID.c_str(), sizeof(sharedFile.nodeSSID) - 1);
                    
                    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                    if (esp_now_send(broadcastMac, (uint8_t*)&sharedFile, sizeof(SharedFile)) != 0) {
                        failedTransmissions.push_back(sharedFile);
                        String macStr = getMacString(sharedFile.sourceNode);
                        sendStatuses[macStr] = {false, 0, millis()};
                    }
                }
            }
            file.close();
            yield();
        }
        symDir.close();
    }
}

void sendFileInChunks(const char* filename, File& file, uint8_t* targetMac) {
    uint32_t fileSize = file.size();
    uint32_t totalChunks = (fileSize + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;
    
    FileChunk chunk = {};  // Zero-initialize the structure
    strncpy(chunk.filename, filename, sizeof(chunk.filename) - 1);
    chunk.totalChunks = totalChunks;
    memset(chunk.padding, 0, sizeof(chunk.padding));  // Explicitly zero padding
    
    for (uint32_t i = 0; i < totalChunks; i++) {
        chunk.chunkIndex = i;
        uint32_t offset = i * MAX_CHUNK_SIZE;
        file.seek(offset);
        
        // Calculate size of this chunk
        uint16_t chunkSize = (fileSize - offset) > MAX_CHUNK_SIZE ? 
                            MAX_CHUNK_SIZE : (fileSize - offset);
        chunk.chunkSize = chunkSize;
        
        // Read chunk data
        file.read(chunk.data, chunkSize);
        
        // Send chunk
        if (esp_now_send(targetMac, (uint8_t*)&chunk, sizeof(FileChunk)) != 0) {
            Serial.printf("Failed to send chunk %d/%d\n", i + 1, totalChunks);
        }
        
        // Small delay between chunks to prevent flooding
        delay(50);
    }
}
*/
void cleanupStaleTransfers() {
    unsigned long currentTime = millis();
    auto it = activeTransfers.begin();
    while (it != activeTransfers.end()) {
        if (currentTime - it->second.lastReceived > CHUNK_TIMEOUT) {
            String tempFilename = "/Alexandria/." + it->first + ".tmp";
            SD.remove(tempFilename.c_str());
            it = activeTransfers.erase(it);
        } else {
            ++it;
        }
    }
}


//DIRECTORY & FORUM MANAGEMENT
void initializeDirectories() {
    Serial.println("\n--- Initializing Directory Structure ---");

    // Create Alexandria directory if it doesn't exist
    if (!SD.exists("/Alexandria")) {
        Serial.println("Creating Alexandria directory...");
        if (!SD.mkdir("/Alexandria")) {
            Serial.println("Failed to create Alexandria directory!");
            return;
        }
    }

    // Create numeric directory
    String numPath = "/Alexandria/0-9";
    if (!SD.exists(numPath)) {
        Serial.println("Creating numeric directory...");
        if (!SD.mkdir(numPath)) {
            Serial.println("Failed to create numeric directory!");
        }
    }

    // Create symbols directory
    String symPath = "/Alexandria/#@";
    if (!SD.exists(symPath)) {
        Serial.println("Creating symbols directory...");
        if (!SD.mkdir(symPath)) {
            Serial.println("Failed to create symbols directory!");
        }
    }

    // Create A-Z directories
    for (char c = 'A'; c <= 'Z'; c++) {
        String dirPath = "/Alexandria/" + String(c);
        if (!SD.exists(dirPath)) {
            Serial.println("Creating directory: " + dirPath);
            if (!SD.mkdir(dirPath)) {
                Serial.println("Failed to create directory: " + dirPath);
            }
        }
        yield(); // Allow the ESP8266 to handle other tasks
    }

    Serial.println("Directory initialization complete\n");
}

void initializeForum() {
    Serial.println("Initializing forum structure...");
   
    // First, create the main forum directory if it doesn't exist
    if (!SD.exists("/forum")) {
        Serial.println("Creating /forum directory");
        if (SD.mkdir("/forum")) {
            Serial.println("/forum directory created successfully");
        } else {
            Serial.println("Failed to create /forum directory");
            return;
        }
    }
   
    // Create the posts directory if it doesn't exist
    if (!SD.exists("/forum/posts")) {
        Serial.println("Creating /forum/posts directory");
        if (SD.mkdir("/forum/posts")) {
            Serial.println("/forum/posts directory created successfully");
        } else {
            Serial.println("Failed to create /forum/posts directory");
            return;
        }
    }
   
    // Initialize or reset the threads.json file
    File threadsFile = SD.open("/forum/threads.json", FILE_WRITE);
    if (threadsFile) {
        threadsFile.println("[]");
        threadsFile.close();
        Serial.println("threads.json initialized successfully");
    } else {
        Serial.println("Failed to initialize threads.json");
    }
   
    Serial.println("Forum initialization complete");
}

void checkAndCleanupForum() {
    unsigned long currentTime = millis();
    if ((currentTime - lastCleanupTime >= CLEANUP_INTERVAL) || (currentTime < lastCleanupTime)) {
        cleanupForum();
        lastCleanupTime = currentTime;
    }
}

void cleanupForum() {
    Serial.println("\n--- Starting Forum Cleanup ---");
    
    // First, close any open files in the forum directory
    File root = SD.open("/forum");
    if (root) {
        if (root.isDirectory()) {
            File entry;
            while (entry = root.openNextFile()) {
                entry.close();
            }
        }
        root.close();
    }
    
    // Remove existing forum directory and its contents
    if (SD.exists("/forum")) {
        Serial.println("Removing old forum directory and contents...");
        // First try to remove posts directory
        if (SD.exists("/forum/posts")) {
            removeDirectory("/forum/posts");
        }
        
        // Remove any remaining files in forum directory
        root = SD.open("/forum");
        if (root && root.isDirectory()) {
            File entry;
            while (entry = root.openNextFile()) {
                String entryPath = String("/forum/") + entry.name();
                entry.close();
                if (!SD.remove(entryPath.c_str())) {
                    Serial.printf("Failed to remove: %s\n", entryPath.c_str());
                } else {
                    Serial.printf("Removed: %s\n", entryPath.c_str());
                }
            }
            root.close();
        }
        
        // Now remove the forum directory itself
        removeDirectory("/forum");
    }
    
    // Add a small delay before creating new directories
    delay(100);
    
    Serial.println("Creating new forum structure...");
    
    // Create fresh forum directory
    if (!SD.mkdir("/forum")) {
        Serial.println("Failed to create /forum directory!");
        return;
    }
    
    delay(50); // Small delay between operations
    
    // Create posts subdirectory
    if (!SD.mkdir("/forum/posts")) {
        Serial.println("Failed to create /forum/posts directory!");
        return;
    }
    
    delay(50);
    
    // Initialize empty threads file
    File threadsFile = SD.open("/forum/threads.json", FILE_WRITE);
    if (threadsFile) {
        threadsFile.println("[]");
        threadsFile.close();
        Serial.println("Initialized empty threads.json");
    } else {
        Serial.println("Failed to create threads.json");
    }
    
    // Create cleanup log
    File logFile = SD.open("/forum/cleanup.log", FILE_WRITE);
    if (logFile) {
        logFile.printf("Forum cleaned at: %lu\n", millis());
        logFile.close();
        Serial.println("Created cleanup log");
    }
    
    Serial.println("Forum cleanup complete\n");
}

void cleanupPosts() {
    if (SD.exists("/forum/posts")) {
        removeDirectory("/forum/posts");
    }
}

void removeDirectory(const char * path) {
    Serial.printf("Attempting to remove directory: %s\n", path);
    
    File dir = SD.open(path);
    if (!dir) {
        Serial.printf("Failed to open directory: %s\n", path);
        return;
    }
    
    if (!dir.isDirectory()) {
        Serial.println("Not a directory");
        dir.close();
        return;
    }
    
    File entry;
    while (entry = dir.openNextFile()) {
        String entryPath = String(path) + "/" + entry.name();
        
        if (entry.isDirectory()) {
            Serial.printf("Found subdirectory: %s\n", entryPath.c_str());
            entry.close();
            removeDirectory(entryPath.c_str());
        } else {
            Serial.printf("Removing file: %s\n", entryPath.c_str());
            entry.close();
            if (!SD.remove(entryPath.c_str())) {
                Serial.printf("Failed to remove file: %s\n", entryPath.c_str());
            }
        }
    }
    
    dir.close();
    
    // Attempt to remove the directory itself
    if (!SD.rmdir(path)) {
        Serial.printf("Failed to remove directory: %s\n", path);
        // Try force removing if normal removal fails
        if (!SD.remove(path)) {
            Serial.printf("Force remove also failed for: %s\n", path);
        } else {
            Serial.printf("Force removed: %s\n", path);
        }
    } else {
        Serial.printf("Successfully removed directory: %s\n", path);
    }
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
   server.chunkedResponseFinalize();
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
        server.sendContent(F("<div class='nav-bar'><span class='section-title'>"));
        server.sendContent(sectionTitle + F(" F1L35</span></div>"));
        
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
                                String encodedPath = urlencode(fullPath);

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
                    server.sendContent(F("<script>document.querySelector('.section-title').innerHTML += ' [") + 
                                     String(fileCount) + F(" F1L35]';</script>"));
                }
                
                dir.close();
            } else {
                server.sendContent(F("<div class='file-item'>[D1R3C70RY N07 F0UND]</div>"));
            }
        }
        
        server.sendContent(F("</div>"));

        // Navigation bar at bottom
        server.sendContent(F("<div class='nav-bar'>"));
        server.sendContent(F("<a href='/node-files?node=") + nodeSSID + 
                         F("' class='nav-button' onclick='showLoading(true)'>&lt;&lt; 53C710N5</a>"));
        server.sendContent(F("</div>"));
    }

    // Close container and HTML
    server.sendContent(F("</div>"));
    
    // Add a script to hide loading indicator when page is loaded
    server.sendContent(F("<script>window.onload = function(){showLoading(false)}</script>"));
    server.sendContent(F("</body></html>"));
    server.chunkedResponseFinalize();
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


//CORE LOOP FUNCTIONS
void setup() {
    // Start Serial for debugging
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== Starting Setup ===");
    
    // Initialize LED pin
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
    Serial.println("LED initialized");
    
    // Initialize SD card with proper error handling
    Serial.print("Initializing SD card...");
    SPI.begin();
    SPI.beginTransaction(spiSettings);
    if (!SD.begin(SD_CS_PIN)) {
        SPI.endTransaction();
        Serial.println("FAILED!");
        Serial.println("SD card initialization failed. Check wiring and card.");
        // Flash LED to indicate SD card error
        for(int i = 0; i < 5; i++) {
            digitalWrite(ledPin, HIGH);
            delay(100);
            digitalWrite(ledPin, LOW);
            delay(100);
        }
        return;
    }
    SPI.endTransaction();
    Serial.println("SUCCESS");

    // Initialize all required directories
    initializeDirectories();
  
    // First, explicitly disconnect and set WiFi mode
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    // Then set mode to AP+STA
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    
    // Set up Access Point with specific settings
    Serial.println("\nInitializing Access Point...");
    setupWiFiAP();
    setupDnsRedirect(); //DNS Redirect
    delay(500);  // Give more time for AP to stabilize
    
    // Verify AP is running
    if (WiFi.softAP(AP_SSID.c_str(), NULL, 1, 0, MAX_AP_CLIENTS)) {
        Serial.println("Access Point Started Successfully");
        Serial.printf("SSID: %s\n", AP_SSID.c_str());
        Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("Access Point Start Failed!");
        // Try to recover
        WiFi.mode(WIFI_OFF);
        delay(1000);
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        if (!WiFi.softAP(AP_SSID.c_str(), NULL, 1, 0, MAX_AP_CLIENTS)) {
            Serial.println("Recovery attempt failed!");
            return;
        }
    }
    
    /*
    // Initialize ESP-NOW only after AP is confirmed working
    Serial.println("\nInitializing ESP-NOW...");
    if (esp_now_init() != 0) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    
    // Register callbacks
    esp_now_register_recv_cb([](uint8_t *mac, uint8_t *data, uint8_t len) {
        onDataReceived(mac, data, len);
    });
    
    esp_now_register_send_cb([](uint8_t *mac_addr, uint8_t sendStatus) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        Serial.printf("Send Status for %s: %s\n", macStr, sendStatus == 0 ? "Success" : "Fail");
    });
    
    // Add broadcast peer
    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (addPeer(broadcastMac)) {
        Serial.println("Broadcast peer added successfully");
    } else {
        Serial.println("Failed to add broadcast peer");
    }
    */

    // Configure DNS server
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("\nDNS Server started");
    
    // Set up server routes
    Serial.println("\nSetting up web server routes...");
    server.on("/", handleRoot);
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
    server.on("/uploadpage", handleUploadPage);
    server.on("/disclaimer", handleDisclaimer);
    server.on("/hotspot-detect.html", handleCaptivePortal);
    server.on("/generate_204", handleCaptivePortal);
    server.on("/mobile/status.php", handleCaptivePortal);  
    server.on("/success.txt", handleCaptivePortal);
    server.on("/ncsi.txt", handleCaptivePortal);
    
    // Start web server
    server.begin();
    Serial.println("HTTP server started");
    
    // Initialize cleanup timer
    lastCleanupTime = millis();
    
    // Print memory stats
    Serial.printf("\nSetup complete. Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println("=== Setup Finished ===\n");
    
    // Indicate successful setup with LED
    for(int i = 0; i < 3; i++) {
        digitalWrite(ledPin, HIGH);
        delay(200);
        digitalWrite(ledPin, LOW);
        delay(200);
    }
}

void loop() {
    static unsigned long lastDNS = 0;
    unsigned long currentMillis = millis();
    
    // Process DNS less frequently
    if (currentMillis - lastDNS >= 50) {  // Check DNS every 50ms instead of every loop
        dnsServer.processNextRequest();
        lastDNS = currentMillis;
    }
    
    // Add WiFi mode check
    if (currentMillis - lastWiFiCheck >= 5000) {  // Check every 5 seconds
        checkWiFiMode();
        lastWiFiCheck = currentMillis;
    }
    
    server.handleClient();    //HTTP
    checkAndCleanupForum();
    //updateNodeLinking();
    cleanupStaleTransfers();
    
    yield();  // Give system time to process WiFi
}




















