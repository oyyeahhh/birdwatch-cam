/*
 * Credentials Manager for ESP32-S3 AI Camera v3.2.0
 * 
 * v3.2.0 UPDATE:
 * - begin() now accepts default credentials (SSID, password, API key, device name)
 * - If valid defaults are provided at compile time, skips captive portal on first boot
 * - Captive portal setup wizard still launches if no defaults are set
 * - DHCP-only: static IP removed for simplicity (IP always printed to Serial Monitor)
 * - saveCredentials() still accepts static IP params for compatibility (ignored)
 *
 * v3.1.3 PATCH:
 * - Fixed success page not displaying before AP closes
 * - Keeps AP+STA mode active for 30 seconds after connection
 * - Added countdown timer on success page
 * - User can now see and copy IP address before page closes
 * 
 * v3.1.2 PATCH:
 * - Added real-time IP address display on setup page
 * - Shows current IP before form submission
 * - Polls for connection status every 5 seconds
 * - Displays IP while user is still configuring
 * 
 * v3.1.1 PATCH:
 * - Enhanced IP display on success page
 * - Added copy-to-clipboard button for IP address
 * - Improved visibility for both DHCP and Static IP modes
 * - Extended display time for user to note IP address
 * - Added comprehensive network information display
 * 
 * NEW IN v3.1.0:
 * - Static IP configuration support
 * - IP address display on setup page
 * - Gateway, subnet, and DNS configuration
 * - IP assignment status indicator
 * 
 * Provides secure storage of WiFi credentials and OpenAI API key in NVS
 * Includes captive portal setup wizard for first boot
 * Factory reset capability with BOOT button hold
 * 
 * Usage:
 * 1. Include this header in your main .ino file
 * 2. Call CredentialsManager::begin() in setup()
 * 3. Access credentials via getter methods
 * 4. Hold BOOT button for 3 seconds to factory reset
 */

#ifndef CREDENTIALS_MANAGER_H
#define CREDENTIALS_MANAGER_H

#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// BOOT button GPIO (standard on ESP32-S3)
#define BOOT_BUTTON_PIN 0
#define FACTORY_RESET_HOLD_TIME 3000  // 3 seconds

class CredentialsManager {
public:
  static bool begin(const char* defaultSSID = "", const char* defaultPassword = "",
                    const char* defaultAPIKey = "", const char* defaultDeviceName = "AI-Camera") {
    Serial.println("\n=== Credentials Manager v3.2.0 ===");
    
    // Initialize preferences
    prefs.begin("ai_camera", false);
    
    // Check for factory reset button hold
    checkFactoryReset();
    
    // Check if credentials exist in NVS
    if (!hasCredentials()) {
      // If user has provided defaults at compile time, use them directly
      String ssid = String(defaultSSID);
      String apiKey = String(defaultAPIKey);
      
      if (ssid.length() > 0 && !ssid.equals("YourWiFiSSID") &&
          apiKey.length() > 0 && !apiKey.startsWith("sk-Your")) {
        // Valid defaults provided - save them and skip captive portal
        Serial.println("Using compiled-in default credentials");
        saveCredentials(ssid, String(defaultPassword), apiKey, String(defaultDeviceName),
                        false, "", "", "255.255.255.0", "8.8.8.8");
        loadCredentials();
        Serial.println("Default credentials saved to NVS");
      } else {
        // No valid defaults - launch captive portal setup wizard
        Serial.println("No credentials found - starting setup wizard");
        return startSetupWizard();
      }
    } else {
      // Load existing credentials from NVS
      loadCredentials();
      Serial.println("Credentials loaded from NVS");
    }
    
    return true;
  }
  
  static void checkFactoryReset() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      Serial.println("BOOT button pressed - checking for factory reset...");
      unsigned long pressStart = millis();
      
      while (digitalRead(BOOT_BUTTON_PIN) == LOW && (millis() - pressStart) < FACTORY_RESET_HOLD_TIME) {
        delay(100);
      }
      
      if ((millis() - pressStart) >= FACTORY_RESET_HOLD_TIME) {
        Serial.println("\n*** FACTORY RESET TRIGGERED ***");
        factoryReset();
        ESP.restart();
      }
    }
  }
  
  static bool hasCredentials() {
    return prefs.isKey("wifi_ssid") && 
           prefs.isKey("wifi_pass") && 
           prefs.isKey("openai_key");
  }
  
  static void loadCredentials() {
    wifi_ssid = prefs.getString("wifi_ssid", "");
    wifi_password = prefs.getString("wifi_pass", "");
    openai_api_key = prefs.getString("openai_key", "");
    device_name = prefs.getString("device_name", "AI-Camera");
    
    // Load IP configuration
    use_static_ip = prefs.getBool("use_static_ip", false);
    if (use_static_ip) {
      static_ip = prefs.getString("static_ip", "");
      static_gateway = prefs.getString("static_gateway", "");
      static_subnet = prefs.getString("static_subnet", "255.255.255.0");
      static_dns = prefs.getString("static_dns", "8.8.8.8");
    }
  }
  
  static void saveCredentials(String ssid, String password, String apiKey, String deviceName = "AI-Camera",
                              bool useStaticIP = false, String staticIP = "", String gateway = "",
                              String subnet = "255.255.255.0", String dns = "8.8.8.8") {
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", password);
    prefs.putString("openai_key", apiKey);
    prefs.putString("device_name", deviceName);
    
    // Save IP configuration
    prefs.putBool("use_static_ip", useStaticIP);
    if (useStaticIP) {
      prefs.putString("static_ip", staticIP);
      prefs.putString("static_gateway", gateway);
      prefs.putString("static_subnet", subnet);
      prefs.putString("static_dns", dns);
    }
    
    Serial.println("Credentials saved to NVS");
    if (useStaticIP) {
      Serial.println("Static IP configuration saved: " + staticIP);
    }
  }
  
  static void applyStaticIPIfConfigured() {
    if (use_static_ip && static_ip.length() > 0) {
      IPAddress ip, gateway, subnet, dns;
      
      if (ip.fromString(static_ip) && gateway.fromString(static_gateway) && 
          subnet.fromString(static_subnet) && dns.fromString(static_dns)) {
        
        if (WiFi.config(ip, gateway, subnet, dns)) {
          Serial.println("Static IP configured: " + static_ip);
          Serial.println("Gateway: " + static_gateway);
          Serial.println("Subnet: " + static_subnet);
          Serial.println("DNS: " + static_dns);
        } else {
          Serial.println("Failed to configure static IP");
        }
      } else {
        Serial.println("Invalid IP configuration - using DHCP");
      }
    }
  }
  
  static void factoryReset() {
    Serial.println("Clearing all stored credentials...");
    prefs.clear();
    WiFi.disconnect(true, true);  // Clear WiFi settings
    delay(1000);
  }
  
  // Getters
  static String getWiFiSSID() { return wifi_ssid; }
  static String getWiFiPassword() { return wifi_password; }
  static String getOpenAIKey() { return openai_api_key; }
  static String getDeviceName() { return device_name; }
  static bool getUseStaticIP() { return use_static_ip; }
  static String getStaticIP() { return static_ip; }
  static String getGateway() { return static_gateway; }
  static String getSubnet() { return static_subnet; }
  static String getDNS() { return static_dns; }
  
  static bool startSetupWizard() {
    Serial.println("\n=== Starting Setup Wizard ===");
    
    // Create AP for setup
    String apSSID = "AI-Camera-Setup";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str());
    
    IPAddress IP = WiFi.softAPIP();
    Serial.println("\nSetup WiFi Access Point Created:");
    Serial.println("  SSID: " + apSSID);
    Serial.println("  IP: " + IP.toString());
    Serial.println("\nConnect to this WiFi network and open:");
    Serial.println("  http://192.168.4.1");
    Serial.println("  or http://" + IP.toString());
    
    // Setup DNS server for captive portal
    dnsServer = new DNSServer();
    dnsServer->start(53, "*", IP);
    
    // Setup web server
    setupServer = new WebServer(80);
    
    setupServer->on("/", HTTP_GET, []() {
      setupServer->send(200, "text/html", getSetupHTML());
    });
    
    setupServer->on("/scan", HTTP_GET, []() {
      int n = WiFi.scanNetworks();
      String json = "[";
      for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        json += "}";
      }
      json += "]";
      setupServer->send(200, "application/json", json);
    });
    
    // NEW v3.1.2: Status endpoint for IP address display
    setupServer->on("/status", HTTP_GET, []() {
      String json = "{";
      json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
      if (WiFi.status() == WL_CONNECTED) {
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
        json += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"ssid\":\"" + WiFi.SSID() + "\"";
      } else {
        json += "\"ip\":\"Not connected\"";
      }
      json += "}";
      setupServer->send(200, "application/json", json);
    });
    
    setupServer->on("/configure", HTTP_POST, [&apSSID]() {
      String ssid = setupServer->arg("ssid");
      String password = setupServer->arg("password");
      String apiKey = setupServer->arg("apikey");
      String deviceName = setupServer->arg("devicename");
      
      // IP Configuration (NEW v3.1.0)
      bool useStaticIP = setupServer->arg("ipmode") == "static";
      String staticIP = setupServer->arg("staticip");
      String gateway = setupServer->arg("gateway");
      String subnet = setupServer->arg("subnet");
      String dns = setupServer->arg("dns");
      
      if (deviceName.length() == 0) deviceName = "AI-Camera";
      if (subnet.length() == 0) subnet = "255.255.255.0";
      if (dns.length() == 0) dns = "8.8.8.8";
      
      if (ssid.length() > 0 && apiKey.length() > 0) {
        // Save credentials with IP configuration
        saveCredentials(ssid, password, apiKey, deviceName, 
                       useStaticIP, staticIP, gateway, subnet, dns);
        
        // Try to connect to WiFi
        WiFi.mode(WIFI_STA);
        
        // Apply static IP before connecting if configured
        if (useStaticIP && staticIP.length() > 0) {
          IPAddress ip, gw, sn, d;
          if (ip.fromString(staticIP) && gw.fromString(gateway) && 
              sn.fromString(subnet) && d.fromString(dns)) {
            WiFi.config(ip, gw, sn, d);
          }
        }
        
        WiFi.begin(ssid.c_str(), password.c_str());
        
        // Wait for connection (30 seconds timeout)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 60) {
          delay(500);
          Serial.print(".");
          attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
          String assignedIP = WiFi.localIP().toString();
          
          Serial.println("\n========================================");
          Serial.println("WiFi Connected Successfully!");
          Serial.println("========================================");
          Serial.println("IP Address: " + assignedIP);
          Serial.println("Gateway: " + WiFi.gatewayIP().toString());
          Serial.println("Subnet: " + WiFi.subnetMask().toString());
          Serial.println("DNS: " + WiFi.dnsIP().toString());
          Serial.println("Signal Strength: " + String(WiFi.RSSI()) + " dBm");
          Serial.println("IP Mode: " + String(useStaticIP ? "Static" : "DHCP"));
          Serial.println("========================================");
          Serial.println("\nIMPORTANT: Save this IP address!");
          Serial.println("Access your camera at: http://" + assignedIP);
          Serial.println("========================================\n");
          
          // Send enhanced success response with IP info
          String response = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
          response += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
          response += "<style>";
          response += "body{font-family:Arial,sans-serif;text-align:center;padding:20px;background:#667eea;color:white;margin:0;}";
          response += ".container{background:white;color:#333;padding:40px 30px;border-radius:20px;max-width:450px;margin:20px auto;box-shadow:0 10px 40px rgba(0,0,0,0.3);}";
          response += "h1{color:#667eea;margin-bottom:20px;font-size:28px;}";
          response += ".success-icon{font-size:60px;margin-bottom:20px;}";
          response += ".ip-box{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);padding:25px;border-radius:15px;margin:25px 0;box-shadow:0 4px 15px rgba(102,126,234,0.3);}";
          response += ".ip-label{color:rgba(255,255,255,0.9);font-size:14px;margin-bottom:8px;text-transform:uppercase;letter-spacing:1px;}";
          response += ".ip{font-size:32px;font-weight:bold;color:white;margin:15px 0;font-family:monospace;letter-spacing:2px;text-shadow:0 2px 4px rgba(0,0,0,0.2);}";
          response += ".ip-mode{background:rgba(255,255,255,0.2);display:inline-block;padding:6px 15px;border-radius:20px;font-size:12px;color:white;margin-top:10px;font-weight:600;}";
          response += ".info-box{background:#f8f9fa;padding:20px;border-radius:10px;margin:20px 0;border-left:4px solid #667eea;}";
          response += ".info-box p{margin:10px 0;font-size:14px;line-height:1.6;color:#555;}";
          response += ".info-box strong{color:#667eea;}";
          response += ".copy-btn{background:#48bb78;color:white;border:none;padding:12px 24px;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer;margin-top:15px;transition:all 0.3s;}";
          response += ".copy-btn:hover{background:#38a169;transform:translateY(-2px);}";
          response += ".copy-btn:active{transform:translateY(0);}";
          response += ".note{font-size:13px;color:#666;margin-top:20px;line-height:1.5;}";
          response += "@media(max-width:480px){.container{padding:30px 20px;} .ip{font-size:24px;}}";
          response += "</style></head><body>";
          response += "<div class='container'>";
          response += "<div class='success-icon'>✅</div>";
          response += "<h1>Connected Successfully!</h1>";
          response += "<div class='ip-box'>";
          response += "<div class='ip-label'>Your Camera IP Address</div>";
          response += "<div class='ip' id='ipAddress'>" + assignedIP + "</div>";
          response += "<div class='ip-mode'>" + String(useStaticIP ? "📌 Static IP" : "🔄 DHCP") + "</div>";
          response += "</div>";
          response += "<div class='info-box'>";
          response += "<p><strong>✓ Device Name:</strong> " + deviceName + "</p>";
          response += "<p><strong>✓ Network:</strong> " + ssid + "</p>";
          response += "<p><strong>✓ Signal:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
          response += "<p><strong>✓ Gateway:</strong> " + WiFi.gatewayIP().toString() + "</p>";
          response += "</div>";
          response += "<button class='copy-btn' onclick='copyIP()'>📋 Copy IP Address</button>";
          response += "<div class='note'>";
          response += "<strong>IMPORTANT - Save This IP Address!</strong><br><br>";
          response += "Your camera IP: <strong>http://" + assignedIP + "</strong><br><br>";
          response += "This page will stay open for 30 seconds.<br>";
          response += "After that, reconnect to your WiFi and open the IP above.";
          response += "</div>";
          response += "<div id='countdown' style='margin-top:20px;font-size:16px;color:#667eea;font-weight:600;'></div>";
          response += "</div>";
          response += "<script>";
          response += "function copyIP(){";
          response += "  const ip=document.getElementById('ipAddress').textContent;";
          response += "  navigator.clipboard.writeText(ip).then(()=>{";
          response += "    const btn=document.querySelector('.copy-btn');";
          response += "    btn.textContent='✓ Copied!';";
          response += "    btn.style.background='#38a169';";
          response += "    setTimeout(()=>{btn.textContent='📋 Copy IP Address';btn.style.background='#48bb78';},2000);";
          response += "  }).catch(()=>{alert('IP Address: '+ip);});";
          response += "}";
          response += "let countdown=30;";
          response += "const countdownEl=document.getElementById('countdown');";
          response += "function updateCountdown(){";
          response += "  countdownEl.textContent='Page will close in '+countdown+' seconds';";
          response += "  countdown--;";
          response += "  if(countdown>=0){setTimeout(updateCountdown,1000);}";
          response += "  else{countdownEl.textContent='You can close this page now';}";
          response += "}";
          response += "updateCountdown();";
          response += "</script>";
          response += "</body></html>";
          
          setupServer->send(200, "text/html", response);
          
          // Keep AP+STA mode active for 30 seconds so user can see the page
          Serial.println("\nKeeping setup page active for 30 seconds...");
          Serial.println("User can view IP address at: http://192.168.4.1");
          
          unsigned long pageStartTime = millis();
          while (millis() - pageStartTime < 30000) {  // 30 seconds
            setupServer->handleClient();
            dnsServer->processNextRequest();
            delay(10);
          }
          
          configComplete = true;
          Serial.println("\nClosing setup page - configuration complete");
        } else {
          Serial.println("\nWiFi Connection Failed!");
          String response = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
          response += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#e53e3e;color:white;}";
          response += ".container{background:white;color:#333;padding:40px;border-radius:20px;max-width:400px;margin:0 auto;}";
          response += "h1{color:#e53e3e;}</style></head><body>";
          response += "<div class='container'><h1>❌ Connection Failed</h1>";
          response += "<p>Could not connect to WiFi network.</p>";
          response += "<p>Please check your credentials and try again.</p>";
          response += "<p><a href='/'>← Back to Setup</a></p></div></body></html>";
          setupServer->send(200, "text/html", response);
        }
      } else {
        setupServer->send(400, "text/plain", "Missing required fields");
      }
    });
    
    setupServer->begin();
    
    // Wait for configuration
    configComplete = false;
    unsigned long startTime = millis();
    const unsigned long timeout = 600000; // 10 minute timeout
    
    while (!configComplete && (millis() - startTime) < timeout) {
      dnsServer->processNextRequest();
      setupServer->handleClient();
      delay(10);
    }
    
    // Cleanup
    delete dnsServer;
    delete setupServer;
    
    if (configComplete) {
      Serial.println("\nSetup wizard completed successfully");
      loadCredentials();
      return true;
    } else {
      Serial.println("\nSetup wizard timeout - restarting...");
      ESP.restart();
      return false;
    }
  }

private:
  static Preferences prefs;
  static String wifi_ssid;
  static String wifi_password;
  static String openai_api_key;
  static String device_name;
  static DNSServer* dnsServer;
  static WebServer* setupServer;
  static bool configComplete;
  
  // NEW v3.1.0: Static IP configuration
  static bool use_static_ip;
  static String static_ip;
  static String static_gateway;
  static String static_subnet;
  static String static_dns;
  
  static String getSetupHTML() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AI Camera Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            padding: 40px;
            max-width: 550px;
            width: 100%;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        }
        h1 {
            color: #667eea;
            margin-bottom: 10px;
            font-size: 28px;
            text-align: center;
        }
        .subtitle {
            text-align: center;
            color: #666;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .version-badge {
            background: #667eea;
            color: white;
            padding: 4px 12px;
            border-radius: 12px;
            font-size: 11px;
            display: inline-block;
            margin-left: 8px;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #333;
            font-weight: 600;
            font-size: 14px;
        }
        input, select {
            width: 100%;
            padding: 12px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 14px;
            transition: border-color 0.3s;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
        }
        .btn {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s;
            margin-top: 10px;
        }
        .btn:hover {
            transform: translateY(-2px);
        }
        .btn-scan {
            background: linear-gradient(135deg, #48bb78 0%, #38a169 100%);
        }
        .help-text {
            font-size: 12px;
            color: #999;
            margin-top: 5px;
        }
        .loader {
            border: 3px solid #f3f3f3;
            border-top: 3px solid #667eea;
            border-radius: 50%;
            width: 30px;
            height: 30px;
            animation: spin 1s linear infinite;
            margin: 10px auto;
            display: none;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
        .network-list {
            max-height: 200px;
            overflow-y: auto;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            margin-top: 10px;
            display: none;
        }
        .network-item {
            padding: 12px;
            border-bottom: 1px solid #f0f0f0;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .network-item:hover {
            background: #f8f8f8;
        }
        .network-item:last-child {
            border-bottom: none;
        }
        .signal-strength {
            font-size: 12px;
            color: #666;
        }
        .lock-icon {
            color: #999;
            margin-left: 5px;
        }
        
        /* NEW v3.1.0: IP Configuration Styles */
        .ip-mode-selector {
            display: flex;
            gap: 10px;
            margin-bottom: 15px;
        }
        .ip-mode-btn {
            flex: 1;
            padding: 10px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            background: white;
            cursor: pointer;
            text-align: center;
            transition: all 0.3s;
        }
        .ip-mode-btn.active {
            border-color: #667eea;
            background: #f0f4ff;
            color: #667eea;
            font-weight: 600;
        }
        .static-ip-fields {
            display: none;
            margin-top: 15px;
            padding: 15px;
            background: #f8f9fa;
            border-radius: 8px;
        }
        .static-ip-fields.show {
            display: block;
        }
        .ip-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-bottom: 15px;
        }
        .ip-status {
            padding: 10px;
            border-radius: 8px;
            margin-top: 10px;
            font-size: 13px;
            display: none;
        }
        .ip-status.show {
            display: block;
        }
        .ip-status.dhcp {
            background: #e6f7ff;
            border: 1px solid #91d5ff;
            color: #0050b3;
        }
        .ip-status.static {
            background: #f0f5ff;
            border: 1px solid #adc6ff;
            color: #1d39c4;
        }
        
        .section-divider {
            border-top: 2px solid #e0e0e0;
            margin: 30px 0 20px 0;
            padding-top: 20px;
        }
        .section-title {
            font-size: 16px;
            color: #667eea;
            margin-bottom: 15px;
            font-weight: 600;
        }
        
        /* NEW v3.1.2: Current IP Status Display */
        .current-ip-status {
            background: linear-gradient(135deg, #48bb78 0%, #38a169 100%);
            color: white;
            padding: 20px;
            border-radius: 12px;
            margin-bottom: 25px;
            text-align: center;
            box-shadow: 0 4px 15px rgba(72, 187, 120, 0.3);
        }
        .status-indicator {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
            margin-bottom: 12px;
            font-size: 14px;
            font-weight: 600;
        }
        .pulse {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: white;
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; transform: scale(1); }
            50% { opacity: 0.5; transform: scale(1.1); }
        }
        .ip-display {
            font-size: 24px;
            font-weight: bold;
            font-family: monospace;
            letter-spacing: 1px;
            margin-top: 8px;
        }
        .current-ip-status.disconnected {
            background: linear-gradient(135deg, #999 0%, #777 100%);
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🤖 AI Camera Setup<span class="version-badge">v3.1.3</span></h1>
        <p class="subtitle">Configure your camera to connect to WiFi and OpenAI</p>
        
        <!-- NEW v3.1.2: Current IP Status Display -->
        <div class="current-ip-status" id="currentIPStatus" style="display:none;">
            <div class="status-indicator">
                <div class="pulse"></div>
                <span id="statusText">Checking connection...</span>
            </div>
            <div class="ip-display" id="currentIP"></div>
        </div>
        
        <form id="setupForm" action="/configure" method="POST">
            <div class="form-group">
                <label>Device Name (Optional)</label>
                <input type="text" name="devicename" placeholder="AI-Camera" value="AI-Camera">
                <div class="help-text">Friendly name for your camera</div>
            </div>
            
            <div class="section-divider">
                <div class="section-title">📡 Network Configuration</div>
            </div>
            
            <div class="form-group">
                <label>WiFi Network</label>
                <input type="text" id="ssid" name="ssid" placeholder="Enter WiFi network name" required>
                <button type="button" class="btn btn-scan" onclick="scanNetworks()">📡 Scan Networks</button>
                <div class="loader" id="scanLoader"></div>
                <div class="network-list" id="networkList"></div>
            </div>
            
            <div class="form-group">
                <label>WiFi Password</label>
                <input type="password" name="password" placeholder="Enter WiFi password">
                <div class="help-text">Leave blank for open networks</div>
            </div>
            
            <!-- NEW v3.1.0: IP Configuration -->
            <div class="form-group">
                <label>IP Address Mode</label>
                <div class="ip-mode-selector">
                    <div class="ip-mode-btn active" onclick="selectIPMode('dhcp')">
                        <div>🔄 DHCP</div>
                        <div style="font-size:11px;color:#666;margin-top:4px;">Automatic</div>
                    </div>
                    <div class="ip-mode-btn" onclick="selectIPMode('static')">
                        <div>📌 Static IP</div>
                        <div style="font-size:11px;color:#666;margin-top:4px;">Manual</div>
                    </div>
                </div>
                <input type="hidden" id="ipmode" name="ipmode" value="dhcp">
                
                <div class="ip-status dhcp show">
                    ✓ IP address will be automatically assigned by your router (recommended for most users)
                </div>
                
                <div class="static-ip-fields" id="staticIPFields">
                    <div class="form-group">
                        <label>Static IP Address</label>
                        <input type="text" name="staticip" placeholder="192.168.1.100" pattern="^((25[0-5]|(2[0-4]|1\d|[1-9]|)\d)\.?\b){4}$">
                        <div class="help-text">Example: 192.168.1.100</div>
                    </div>
                    
                    <div class="form-group">
                        <label>Gateway (Router IP)</label>
                        <input type="text" name="gateway" placeholder="192.168.1.1" pattern="^((25[0-5]|(2[0-4]|1\d|[1-9]|)\d)\.?\b){4}$">
                        <div class="help-text">Usually your router's IP (e.g., 192.168.1.1)</div>
                    </div>
                    
                    <div class="ip-row">
                        <div>
                            <label>Subnet Mask</label>
                            <input type="text" name="subnet" value="255.255.255.0" pattern="^((25[0-5]|(2[0-4]|1\d|[1-9]|)\d)\.?\b){4}$">
                        </div>
                        <div>
                            <label>DNS Server</label>
                            <input type="text" name="dns" value="8.8.8.8" pattern="^((25[0-5]|(2[0-4]|1\d|[1-9]|)\d)\.?\b){4}$">
                        </div>
                    </div>
                    
                    <div class="ip-status static show">
                        ℹ️ Make sure the static IP is outside your router's DHCP range to avoid conflicts
                    </div>
                </div>
            </div>
            
            <div class="section-divider">
                <div class="section-title">🔑 OpenAI Configuration</div>
            </div>
            
            <div class="form-group">
                <label>OpenAI API Key</label>
                <input type="password" name="apikey" placeholder="sk-..." required>
                <div class="help-text">Get your API key from platform.openai.com</div>
            </div>
            
            <button type="submit" class="btn">🚀 Connect & Start Camera</button>
        </form>
    </div>

    <script>
        // NEW v3.1.2: Check current IP status
        function checkCurrentStatus() {
            fetch('/status')
                .then(response => response.json())
                .then(data => {
                    const statusDiv = document.getElementById('currentIPStatus');
                    const statusText = document.getElementById('statusText');
                    const ipDisplay = document.getElementById('currentIP');
                    
                    if (data.connected) {
                        statusDiv.style.display = 'block';
                        statusDiv.classList.remove('disconnected');
                        statusText.textContent = '✓ Currently Connected';
                        ipDisplay.textContent = data.ip;
                        ipDisplay.title = 'SSID: ' + data.ssid + ' | Signal: ' + data.rssi + ' dBm';
                    } else {
                        // Don't show anything if not connected (setup mode)
                        statusDiv.style.display = 'none';
                    }
                })
                .catch(error => {
                    // Silently fail - device is in setup mode
                });
        }
        
        // Check status on page load and every 5 seconds
        checkCurrentStatus();
        setInterval(checkCurrentStatus, 5000);
        
        function selectIPMode(mode) {
            // Update buttons
            document.querySelectorAll('.ip-mode-btn').forEach(btn => btn.classList.remove('active'));
            event.currentTarget.classList.add('active');
            
            // Update hidden field
            document.getElementById('ipmode').value = mode;
            
            // Show/hide static IP fields
            const staticFields = document.getElementById('staticIPFields');
            const dhcpStatus = document.querySelector('.ip-status.dhcp');
            
            if (mode === 'static') {
                staticFields.classList.add('show');
                dhcpStatus.classList.remove('show');
            } else {
                staticFields.classList.remove('show');
                dhcpStatus.classList.add('show');
            }
        }
        
        function scanNetworks() {
            document.getElementById('scanLoader').style.display = 'block';
            document.getElementById('networkList').style.display = 'none';
            
            fetch('/scan')
                .then(response => response.json())
                .then(networks => {
                    document.getElementById('scanLoader').style.display = 'none';
                    const list = document.getElementById('networkList');
                    list.innerHTML = '';
                    
                    networks.forEach(network => {
                        const item = document.createElement('div');
                        item.className = 'network-item';
                        item.onclick = () => {
                            document.getElementById('ssid').value = network.ssid;
                            list.style.display = 'none';
                        };
                        
                        const signalStrength = network.rssi > -50 ? 'Excellent' :
                                             network.rssi > -60 ? 'Good' :
                                             network.rssi > -70 ? 'Fair' : 'Weak';
                        
                        item.innerHTML = `
                            <span>${network.ssid} ${network.secure ? '<span class="lock-icon">🔒</span>' : ''}</span>
                            <span class="signal-strength">${signalStrength} (${network.rssi} dBm)</span>
                        `;
                        list.appendChild(item);
                    });
                    
                    list.style.display = 'block';
                })
                .catch(error => {
                    document.getElementById('scanLoader').style.display = 'none';
                    alert('Network scan failed. Please enter SSID manually.');
                });
        }
        
        // Form validation for static IP
        document.getElementById('setupForm').addEventListener('submit', function(e) {
            const ipMode = document.getElementById('ipmode').value;
            
            if (ipMode === 'static') {
                const staticIP = document.querySelector('input[name="staticip"]').value;
                const gateway = document.querySelector('input[name="gateway"]').value;
                
                if (!staticIP || !gateway) {
                    e.preventDefault();
                    alert('Please enter both Static IP and Gateway for static IP mode');
                    return false;
                }
                
                // Basic IP validation
                const ipPattern = /^((25[0-5]|(2[0-4]|1\d|[1-9]|)\d)\.?\b){4}$/;
                if (!ipPattern.test(staticIP) || !ipPattern.test(gateway)) {
                    e.preventDefault();
                    alert('Please enter valid IP addresses');
                    return false;
                }
            }
        });
    </script>
</body>
</html>
)rawliteral";
  }
};

// Static member initialization
Preferences CredentialsManager::prefs;
String CredentialsManager::wifi_ssid;
String CredentialsManager::wifi_password;
String CredentialsManager::openai_api_key;
String CredentialsManager::device_name;
DNSServer* CredentialsManager::dnsServer = nullptr;
WebServer* CredentialsManager::setupServer = nullptr;
bool CredentialsManager::configComplete = false;

// NEW v3.1.0: Static IP configuration
bool CredentialsManager::use_static_ip = false;
String CredentialsManager::static_ip;
String CredentialsManager::static_gateway;
String CredentialsManager::static_subnet;
String CredentialsManager::static_dns;

#endif // CREDENTIALS_MANAGER_H
