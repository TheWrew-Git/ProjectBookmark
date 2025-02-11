#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <SD.h>
#include <map>
#include <vector>
#include <algorithm>

// Global Variables

// LED pin
const int ledPin = LED_BUILTIN;
bool ledState = false;

// Access Point settings
const char* serverName = "r04m1ng.l1br4ry";  // DNS name for captive portal
const char* AP_SSID = "PR0J3K7_B00KM4RK";  // Name of the WiFi network
String AP_SSID;  // Full name with number
IPAddress apIP(192, 168, 4, 1);        // IP address of the NodeMCU in AP mode
const byte DNS_PORT = 53;              // DNS server port

// Create web server object
ESP8266WebServer server(80);
DNSServer dnsServer;

// SD card CS pin
const int SD_CS_PIN = D8;

// File Uploads
File uploadFile;

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
        
          // Clear and reinitialize forum on boot
        Serial.println("Clearing forum data...");
        if (SD.exists("/forum")) {
            removeDirectory("/forum");
        }
        
        // Create fresh forum structure
        SD.mkdir("/forum");
        SD.mkdir("/forum/posts");
        File threadsFile = SD.open("/forum/threads.json", FILE_WRITE);
        if (threadsFile) {
            threadsFile.println("[]");
            threadsFile.close();
            Serial.println("Forum reinitialized successfully");
        }
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
    server.on("/uploadpage", handleUploadPage);server.on("/", handleRoot);             // Main library page
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
        if (captivePortal()) { return; }
        handlePortal();
    });
    
    server.onNotFound([]() {
        if (captivePortal()) { return; }
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

void handleCaptivePortal() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='0;url=http://" + WiFi.softAPIP().toString() + "'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += ".container { text-align: center; margin-top: 50px; }";
    html += ".enter-btn { display: inline-block; background: #000; color: #0f0; border: 1px solid #0f0; padding: 15px 30px; font-size: 1.2em; text-decoration: none; margin-top: 20px; }";
    html += ".enter-btn:hover { background: #0f0; color: #000; }";
    html += ".glitch { font-size: 2em; font-weight: bold; text-transform: uppercase; position: relative; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1 class='glitch'>PR0J3KT B00KM4RK</h1>";
    html += "<a href='http://" + WiFi.softAPIP().toString() + "' class='enter-btn'>Enter Library</a>";
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

void handleNotFound() {
    // If request is not for our IP, redirect to captive portal
    if (!isIp(server.hostHeader())) {
        server.sendHeader("Location", String("http://") + toStringIp(apIP), true);
        server.send(302, "text/plain", ""); 
        return;
    }
    // Otherwise, send captive portal page
    handleCaptivePortal();
}

void handleRoot() {
    // Add headers for captive portal
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += ".container { border: 1px solid #0f0; padding: 20px; margin: 10px 0; box-shadow: 0 0 10px #0f0; }";
    html += ".status { border-left: 3px solid #0f0; padding-left: 10px; margin: 10px 0; text-align: center; }";
    html += ".pixelart { width: 150px; height: 150px; margin: 20px auto; display: block; animation: glow 2s ease-in-out infinite alternate; }";
    html += ".goat-png { width: 150px; height: 150px; margin: 20px auto; display: block; filter: hue-rotate(90deg) brightness(200%); }";
    html += "@keyframes glow { from { filter: drop-shadow(0 0 5px #0f0); } to { filter: drop-shadow(0 0 10px #0f0); } }";
    html += ".glitch-wrapper { padding: 20px; text-align: center; margin-bottom: 20px; }";
    html += ".glitch { font-size: 2em; font-weight: bold; text-transform: uppercase; position: relative; text-shadow: 0.05em 0 0 #00fffc, -0.03em -0.04em 0 #fc00ff, 0.025em 0.04em 0 #fffc00; animation: glitch 725ms infinite; }";
    html += ".glitch span { position: absolute; top: 0; left: 0; }";
    html += ".glitch span:first-child { animation: glitch 500ms infinite; clip-path: polygon(0 0, 100% 0, 100% 35%, 0 35%); transform: translate(-0.04em, -0.03em); opacity: 0.75; }";
    html += ".glitch span:last-child { animation: glitch 375ms infinite; clip-path: polygon(0 65%, 100% 65%, 100% 100%, 0 100%); transform: translate(0.04em, 0.03em); opacity: 0.75; }";
    html += "@keyframes glitch { 0% { text-shadow: 0.05em 0 0 #00fffc, -0.03em -0.04em 0 #fc00ff, 0.025em 0.04em 0 #fffc00; }";
    html += "15% { text-shadow: 0.05em 0 0 #00fffc, -0.03em -0.04em 0 #fc00ff, 0.025em 0.04em 0 #fffc00; }";
    html += "16% { text-shadow: -0.05em -0.025em 0 #00fffc, 0.025em 0.035em 0 #fc00ff, -0.05em -0.05em 0 #fffc00; }";
    html += "49% { text-shadow: -0.05em -0.025em 0 #00fffc, 0.025em 0.035em 0 #fc00ff, -0.05em -0.05em 0 #fffc00; }";
    html += "50% { text-shadow: 0.05em 0.035em 0 #00fffc, 0.03em 0 0 #fc00ff, 0 -0.04em 0 #fffc00; }";
    html += "99% { text-shadow: 0.05em 0.035em 0 #00fffc, 0.03em 0 0 #fc00ff, 0 -0.04em 0 #fffc00; }";
    html += "100% { text-shadow: -0.05em 0 0 #00fffc, -0.025em -0.04em 0 #fc00ff, -0.04em -0.025em 0 #fffc00; } }";
    html += "input[type='file']::-webkit-file-upload-button {";
    html += "input[type='file']::-webkit-file-upload-button {";
    html += "  background: #000;";
    html += "  color: #0f0;";
    html += "  border: 1px solid #0f0;";
    html += "  padding: 5px 10px;";
    html += "  cursor: pointer;";
    html += "}";
    html += "input[type='file']::-webkit-file-upload-button:hover {";
    html += "  background: #0f0;";
    html += "  color: #000;";
    html += "}";
    html += "</style></head><body>";

    html += "<div class='glitch-wrapper'>";
    html += "<div class='glitch'>7H3 R04M1NG L1BR4RY";
    html += "</div>";
    html += "</div>";

    // Add ASCII Owl
    html += "<pre style='color: #0f0; text-align: center; line-height: 1.2; margin: 20px auto; font-size: 14px;'>";
    html += "      ,___,\n";
    html += "     (O,O)\n";
    html += "     (  v  )\n";
    html += "    -==*^*==-\n";
    html += "</pre>";


    html += "<div class='container'>";
    html += "<div class='status'>";
    html += "<p>[ Network: " + String(AP_SSID) + " ]<br>";
   // html += "[ IP: " + WiFi.softAPIP().toString() + " ]<br>";
    html += "[ Connected Users: <span id='user-count'>" + String(WiFi.softAPgetStationNum()) + "</span> ]<br>";
    html += "[ System Status: " + String(ledState ? "ACTIVE" : "STANDBY") + " ]</p>";
    html += "</div>";
 
    html += "<div class='status'>";
    html += "<h3><a href='/forum' style='text-decoration: underline;'> // P057-2-F0RUM // </a></h3>";
    html += "</div>";

    html += "<div class='status'>";
    html += "<div style='text-align: center;'>";
    html += "<h3><a href='/uploadpage' style='text-decoration: underline;'> // L34V3-4-F1L3 // </a></h3>";
    html += "</div>";
    html += "</div>"; 

    html += "<div class='status'>";
    html += "<h3><a href='/list' style='text-decoration: underline;'> // 74k3-4-F1L3 // </a></h3>";
    html += "</div>";

    html += "</div></body></html>";

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
        String title = server.arg("title");
        String content = server.arg("content");
        String author = server.arg("author");
        
        if (title.length() > 0 && content.length() > 0) {
            // Create new thread
            String threadId = String(millis());
            
             // Read and parse existing threads
            File threadsFile = SD.open("/forum/threads.json", FILE_READ);
            String threads = "[]";
            if (threadsFile) {
                threads = threadsFile.readString();
                threadsFile.close();
                // Remove the last ]
                threads = threads.substring(0, threads.length() - 1);
                // Add comma if not empty array
                if (threads.length() > 1) {
                    threads += ",";
                }
            }
            
            // Add new thread
            threads += "{\"id\":\"" + threadId + "\",";
            threads += "\"title\":\"" + title + "\",";
            threads += "\"author\":\"" + author + "\",";
            threads += "\"timestamp\":\"" + String(millis()) + "\"}]";
            
            threadsFile = SD.open("/forum/threads.json", FILE_WRITE);
            if (threadsFile) {
                threadsFile.print(threads);
                threadsFile.close();
            }
            
            // Save initial post
            String postPath = "/forum/posts/" + threadId + ".json";
            File postFile = SD.open(postPath, FILE_WRITE);
            if (postFile) {
                postFile.print("[{\"id\":\"" + String(millis()) + "\",");
                postFile.print("\"author\":\"" + author + "\",");
                postFile.print("\"content\":\"" + content + "\",");
                postFile.print("\"timestamp\":\"" + String(millis()) + "\"}]");
                postFile.close();
            }
            
            server.sendHeader("Location", "/forum/thread?id=" + threadId);
            server.send(303);
            return;
        }
    }

    // Display new thread form
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += "form { border: 1px solid #0f0; padding: 20px; margin-top: 20px; }";
    html += "input, textarea { background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; width: 100%; margin: 5px 0; }";
    html += "</style></head><body>";
    
    html += "<h2>// NEW THREAD //</h2>";
    html += "<form method='post'>";
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
        
        if (threadId.length() > 0 && content.length() > 0) {
            String postsPath = "/forum/posts/" + threadId + ".json";
            
            // Create new post JSON
            String newPost = "{\"id\":\"" + String(millis()) + "\",";
            newPost += "\"author\":\"" + author + "\",";
            newPost += "\"content\":\"" + content + "\",";
            newPost += "\"timestamp\":\"" + String(millis()) + "\"}";

            // Open file and read existing content
            File postsFile = SD.open(postsPath, FILE_READ);
            String currentPosts;
            if (postsFile) {
                currentPosts = postsFile.readString();
                postsFile.close();
                // Delete the file so we can rewrite it
                SD.remove(postsPath);
            } else {
                currentPosts = "[]";
            }

            // Create updated JSON content
            String updatedPosts;
            if (currentPosts == "[]") {
                updatedPosts = "[" + newPost + "]";
            } else {
                // Remove the closing bracket, add new post, then close
                updatedPosts = currentPosts.substring(0, currentPosts.length() - 1) + "," + newPost + "]";
            }

            // Write the updated content to a new file
            postsFile = SD.open(postsPath, FILE_WRITE);
            if (postsFile) {
                postsFile.print(updatedPosts);
                postsFile.close();
            }
        }
        
        server.sendHeader("Location", "/forum/thread?id=" + threadId + "&scroll=true#bottom");
        server.send(303);
        return;
    }
    
    server.sendHeader("Location", "/forum");
    server.send(303);
}

String formatTimestamp(unsigned long timestamp) {
    unsigned long timeDiff = millis() - timestamp;
    if (timeDiff < 60000) return String(timeDiff / 1000) + "s ago";
    if (timeDiff < 3600000) return String(timeDiff / 60000) + "m ago";
    if (timeDiff < 86400000) return String(timeDiff / 3600000) + "h ago";
    return String(timeDiff / 86400000) + "d ago";
}

void handleUploadPage() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += ".container { border: 1px solid #0f0; padding: 20px; margin: 10px 0; box-shadow: 0 0 10px #0f0; }";
    html += ".upload-form { width: 75%; margin: 20px auto; text-align: center; }";
    html += "input[type='file'] { display: block; margin: 20px auto; color: #0f0; }";
    html += "input[type='submit'] { background: #000; color: #0f0; border: 1px solid #0f0; padding: 10px 20px; cursor: pointer; }";
    html += ".formats { color: #0a0; margin: 10px 0; }";
    html += ".progress { width: 100%; height: 20px; background: #000; border: 1px solid #0f0; margin: 10px 0; display: none; }";
    html += ".progress-bar { width: 0%; height: 100%; background: #0f0; transition: width 0.3s; }";
    html += ".progress-text { text-align: center; margin-top: 5px; }";
    html += "</style>";

    // Add JavaScript for progress handling
    html += "<script>";
    html += "function showProgress() {";
    html += "  document.getElementById('progress').style.display = 'block';";
    html += "  document.getElementById('uploadForm').addEventListener('submit', function() {";
    html += "    const formData = new FormData(this);";
    html += "    const xhr = new XMLHttpRequest();";
    html += "    xhr.open('POST', '/upload', true);";
    html += "    xhr.upload.onprogress = function(e) {";
    html += "      if (e.lengthComputable) {";
    html += "        const percent = (e.loaded / e.total) * 100;";
    html += "        document.getElementById('progressBar').style.width = percent + '%';";
    html += "        document.getElementById('progressText').textContent = Math.round(percent) + '%';";
    html += "      }";
    html += "    };";
    html += "    xhr.onload = function() {";
    html += "      if (xhr.status === 200 || xhr.status === 303) {";
    html += "        window.location.href = '/';";
    html += "      }";
    html += "    };";
    html += "    xhr.send(formData);";
    html += "    return false;";
    html += "  });";
    html += "}";
    html += "</script>";
    html += "</head><body>";

    html += "<h2>// D0N473 //</h2>";

    html += "<div class='upload-form'>";
    html += "<form id='uploadForm' method='post' action='/upload' enctype='multipart/form-data' onsubmit='return showProgress()'>";
    html += "<div class='formats'>[ PDF|EPUB|DOC|RTF|TXT ]</div>";
    html += "<input type='file' name='file' accept='.pdf,.txt,.rtf,.epub' required><br>";
    html += "<div id='progress' class='progress'>";
    html += "<div id='progressBar' class='progress-bar'></div>";
    html += "<div id='progressText' class='progress-text'>0%</div>";
    html += "</div>";
    html += "<input type='submit' value='UPLOAD'>";
    html += "</form>";
    html += "</div>";

    html += "<div style='text-align: center;'>";
    html += "<br><a href='/'>&lt;&lt; Return to Terminal &gt;&gt;</a>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}
void handleFileUpload() {
    if (server.uri() != "/upload") return;
    
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!isAllowedFile(filename)) {
            return;
        }
        // Create/Open file
        uploadFile = SD.open(filename, FILE_WRITE);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
        }
    }
}

void handleUpload() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleFileList() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: 'Courier New', monospace; background-color: #000; color: #0f0; margin: 20px; line-height: 1.6; }";
    html += "h1, h2 { color: #0f0; text-shadow: 0 0 5px #0f0; text-transform: uppercase; text-align: center; }";
    html += "a { color: #0f0; text-decoration: none; }";
    html += "a:hover { color: #fff; text-shadow: 0 0 10px #0f0; }";
    html += ".container { border: 1px solid #0f0; padding: 20px; margin: 10px 0; box-shadow: 0 0 10px #0f0; }";
    html += ".file-list { list-style: none; padding: 0; margin: 0; max-height: 0; overflow: hidden; transition: max-height 0.3s ease-out; }";
    html += ".file-item { border-left: 3px solid #0f0; padding: 10px; margin: 10px 0; }";
    html += ".file-item:hover { background-color: #001100; }";
    html += ".file-size { color: #0a0; font-size: 0.9em; }";
    html += ".section-header { border: 1px solid #0f0; margin: 10px 0; padding: 10px; cursor: pointer; user-select: none; display: flex; justify-content: space-between; }";
    html += ".section-header:hover { background-color: #001100; }";
    html += ".section-header::before { content: '[+]'; margin-right: 10px; }";
    html += ".section-header.active::before { content: '[-]'; }";
    html += ".section-header.active + .file-list { max-height: 1000px; }";
    html += ".file-count { color: #0a0; }";
    html += "</style>";
    
    // Add JavaScript for toggling sections
    html += "<script>";
    html += "function toggleSection(header) {";
    html += "  header.classList.toggle('active');";
    html += "}";
    html += "</script>";
    
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h3>// 404 DEWEY NOT FOUND //</h3>";

    // Create a map to store files by first letter
    std::map<char, std::vector<std::pair<String, size_t>>> fileGroups;
    
    File root = SD.open("/");
    if (!root) {
        html += "<p style='color: #f00;'>[ERROR] Failed to access storage system</p>";
    } else {
        while (File file = root.openNextFile()) {
            String fileName = String(file.name());
            if (isAllowedFile(fileName)) {
                char firstLetter = toupper(fileName.charAt(0));
                fileGroups[firstLetter].push_back({fileName, file.size()});
            }
            file.close();
        }
        root.close();

        if (fileGroups.empty()) {
            html += "<p style='color: #f00;'>[WARNING] No documents found in system</p>";
        } else {
            // Iterate through groups and display files
            for (const auto& group : fileGroups) {
                html += "<div class='section-header' onclick='toggleSection(this)'>";
                html += "<span>[" + String(group.first) + "]</span>";
                html += "<span class='file-count'>[Files: " + String(group.second.size()) + "]</span>";
                html += "</div>";
                html += "<ul class='file-list'>";
                
                // Sort files within each group
                std::vector<std::pair<String, size_t>> sortedFiles = group.second;
                std::sort(sortedFiles.begin(), sortedFiles.end());
                
                for (const auto& file : sortedFiles) {
                    html += "<li class='file-item'>";
                    html += "<a href='/download?file=" + file.first + "'>&gt; " + file.first + " &lt;</a>";
                    html += "<div class='file-size'>[Size: " + String(file.second / 1024.0, 1) + " KB]</div>";
                    html += "</li>";
                }
                html += "</ul>";
            }
        }
    }
    
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






