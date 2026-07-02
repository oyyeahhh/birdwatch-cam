/*
 * Error Handling & Retry Logic for ESP32-S3 AI Camera
 * 
 * Provides:
 * 1. WiFi connection monitoring and automatic reconnection
 * 2. OpenAI API retry with exponential backoff
 * 3. Network diagnostics and error reporting
 * 4. Graceful degradation strategies
 * 
 * Usage:
 * - Replace WiFi.begin() with WiFiManager::connect()
 * - Replace direct API calls with RetryableAPIClient methods
 * - Call WiFiManager::monitor() in loop() for auto-reconnection
 */

#ifndef ERROR_HANDLING_H
#define ERROR_HANDLING_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <functional>

// ============================================================================
// WiFi RECONNECTION MANAGER
// ============================================================================

class WiFiManager {
public:
  // Configuration
  static const unsigned long RECONNECT_INTERVAL = 30000;  // 30 seconds between attempts
  static const unsigned long MONITOR_INTERVAL = 5000;     // Check every 5 seconds
  static const int MAX_CONNECT_ATTEMPTS = 10;
  
  static bool connect(const char* ssid, const char* password) {
    Serial.println("\n=== WiFi Connection Manager ===");
    Serial.printf("SSID: %s\n", ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    // Configure WiFi for better reliability
    WiFi.setSleep(false);  // Disable WiFi sleep for stable connection
    
    int attempt = 0;
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED && attempt < MAX_CONNECT_ATTEMPTS) {
      delay(500);
      Serial.print(".");
      attempt++;
      
      if (attempt % 10 == 0) {
        Serial.printf("\nAttempt %d/%d\n", attempt, MAX_CONNECT_ATTEMPTS);
      }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✓ WiFi connected");
      Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
      Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
      
      lastConnectTime = millis();
      connectionAttempts = 0;
      isConnected = true;
      return true;
    } else {
      Serial.println("\n✗ WiFi connection failed");
      printConnectionError();
      isConnected = false;
      return false;
    }
  }
  
  // Call this in loop() for automatic reconnection
  static void monitor() {
    unsigned long now = millis();
    
    // Check connection status periodically
    if (now - lastCheckTime >= MONITOR_INTERVAL) {
      lastCheckTime = now;
      
      if (WiFi.status() != WL_CONNECTED) {
        if (isConnected) {
          // Just lost connection
          Serial.println("\n⚠ WiFi connection lost!");
          isConnected = false;
          connectionLostTime = now;
        }
        
        // Attempt reconnection if enough time has passed
        if (now - lastReconnectAttempt >= RECONNECT_INTERVAL) {
          attemptReconnection();
        }
      } else {
        if (!isConnected) {
          // Just reconnected
          Serial.println("✓ WiFi reconnected!");
          isConnected = true;
          connectionAttempts = 0;
        }
      }
    }
  }
  
  static bool isCurrentlyConnected() {
    return WiFi.status() == WL_CONNECTED;
  }
  
  static String getConnectionStatus() {
    if (!isCurrentlyConnected()) {
      return "Disconnected (attempt " + String(connectionAttempts) + ")";
    }
    
    int rssi = WiFi.RSSI();
    String quality;
    if (rssi > -50) quality = "Excellent";
    else if (rssi > -60) quality = "Good";
    else if (rssi > -70) quality = "Fair";
    else quality = "Weak";
    
    return "Connected (" + quality + " " + String(rssi) + " dBm)";
  }
  
  static void getDiagnostics() {
    Serial.println("\n=== WiFi Diagnostics ===");
    Serial.printf("Status: %s\n", getStatusString(WiFi.status()).c_str());
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
      Serial.printf("DNS: %s\n", WiFi.dnsIP().toString().c_str());
      Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
      Serial.printf("Channel: %d\n", WiFi.channel());
      Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
    }
    
    Serial.printf("Auto Reconnect: %s\n", WiFi.getAutoReconnect() ? "Enabled" : "Disabled");
    Serial.printf("Connection Attempts: %d\n", connectionAttempts);
    
    if (connectionLostTime > 0) {
      unsigned long downtime = (millis() - connectionLostTime) / 1000;
      Serial.printf("Downtime: %lu seconds\n", downtime);
    }
    
    Serial.println("========================\n");
  }

private:
  static unsigned long lastCheckTime;
  static unsigned long lastReconnectAttempt;
  static unsigned long lastConnectTime;
  static unsigned long connectionLostTime;
  static int connectionAttempts;
  static bool isConnected;
  
  static void attemptReconnection() {
    connectionAttempts++;
    lastReconnectAttempt = millis();
    
    Serial.printf("\n⟳ Reconnection attempt %d...\n", connectionAttempts);
    
    // Try to reconnect
    WiFi.disconnect();
    delay(100);
    WiFi.reconnect();
    
    // Wait a bit to see if it works
    int wait = 0;
    while (WiFi.status() != WL_CONNECTED && wait < 10) {
      delay(500);
      Serial.print(".");
      wait++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✓ Reconnection successful!");
      isConnected = true;
      connectionAttempts = 0;
    } else {
      Serial.println("\n✗ Reconnection failed");
      printConnectionError();
    }
  }
  
  static void printConnectionError() {
    wl_status_t status = WiFi.status();
    Serial.printf("Error: %s\n", getStatusString(status).c_str());
    
    switch (status) {
      case WL_NO_SHIELD:
        Serial.println("Suggestion: Check WiFi hardware");
        break;
      case WL_NO_SSID_AVAIL:
        Serial.println("Suggestion: Check SSID name, move closer to router");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("Suggestion: Check password, check router settings");
        break;
      case WL_CONNECTION_LOST:
        Serial.println("Suggestion: Check signal strength, check router stability");
        break;
      case WL_DISCONNECTED:
        Serial.println("Suggestion: Check router, check interference");
        break;
      default:
        Serial.println("Suggestion: Check network settings");
    }
  }
  
  static String getStatusString(wl_status_t status) {
    switch (status) {
      case WL_IDLE_STATUS: return "Idle";
      case WL_NO_SSID_AVAIL: return "SSID not available";
      case WL_SCAN_COMPLETED: return "Scan completed";
      case WL_CONNECTED: return "Connected";
      case WL_CONNECT_FAILED: return "Connection failed";
      case WL_CONNECTION_LOST: return "Connection lost";
      case WL_DISCONNECTED: return "Disconnected";
      case WL_NO_SHIELD: return "No WiFi hardware";
      default: return "Unknown (" + String(status) + ")";
    }
  }
};

// Static member initialization
unsigned long WiFiManager::lastCheckTime = 0;
unsigned long WiFiManager::lastReconnectAttempt = 0;
unsigned long WiFiManager::lastConnectTime = 0;
unsigned long WiFiManager::connectionLostTime = 0;
int WiFiManager::connectionAttempts = 0;
bool WiFiManager::isConnected = false;

// ============================================================================
// API RETRY WITH EXPONENTIAL BACKOFF
// ============================================================================

struct RetryConfig {
  int maxRetries = 3;
  unsigned long initialDelayMs = 1000;  // Start with 1 second
  float backoffMultiplier = 2.0;        // Double delay each retry
  unsigned long maxDelayMs = 30000;     // Cap at 30 seconds
  bool retryOn5xx = true;               // Retry on server errors
  bool retryOnTimeout = true;           // Retry on timeouts
  bool retryOn429 = true;               // Retry on rate limit
};

class RetryableAPIClient {
public:
  // Generic retry wrapper for any HTTP operation
  template<typename Func>
  static int retryWithBackoff(Func apiCall, RetryConfig config = RetryConfig()) {
    int attempt = 0;
    unsigned long delayMs = config.initialDelayMs;
    
    while (attempt <= config.maxRetries) {
      attempt++;
      
      if (attempt > 1) {
        Serial.printf("\n⟳ Retry attempt %d/%d (waiting %lums)...\n", 
                      attempt - 1, config.maxRetries, delayMs);
        delay(delayMs);
        
        // Exponential backoff with jitter
        delayMs = min((unsigned long)(delayMs * config.backoffMultiplier), config.maxDelayMs);
        // Add random jitter (±20%) to prevent thundering herd
        delayMs += random(-delayMs/5, delayMs/5);
      }
      
      Serial.printf("API call attempt %d...\n", attempt);
      int httpCode = apiCall();
      
      // Success
      if (httpCode == 200 || httpCode == 201) {
        if (attempt > 1) {
          Serial.printf("✓ Success after %d attempts\n", attempt);
        }
        return httpCode;
      }
      
      // Determine if we should retry
      bool shouldRetry = false;
      
      if (httpCode < 0) {
        // Connection error
        if (config.retryOnTimeout) {
          Serial.printf("✗ Connection error: %d\n", httpCode);
          shouldRetry = true;
        }
      } else if (httpCode == 429) {
        // Rate limit
        if (config.retryOn429) {
          Serial.println("✗ Rate limited (429)");
          shouldRetry = true;
          // Rate limits need longer backoff
          delayMs = max(delayMs, 5000UL);
        }
      } else if (httpCode >= 500 && httpCode < 600) {
        // Server error
        if (config.retryOn5xx) {
          Serial.printf("✗ Server error: %d\n", httpCode);
          shouldRetry = true;
        }
      } else {
        // Client error (4xx except 429) - don't retry
        Serial.printf("✗ Client error: %d (not retrying)\n", httpCode);
        return httpCode;
      }
      
      // Check if we've exhausted retries
      if (!shouldRetry || attempt > config.maxRetries) {
        Serial.printf("✗ Failed after %d attempts (code: %d)\n", attempt, httpCode);
        return httpCode;
      }
    }
    
    return -1; // Should never reach here
  }
  
  // Whisper API with retry
  static String analyzeWithRetry(String imageBase64, String prompt, const char* apiKey, RetryConfig config = RetryConfig()) {
    String result = "";
    bool success = false;
    
    Serial.println("\n=== Vision API (with retry) ===");
    
    // Build request body once (outside retry loop)
    String requestBody = "{\"model\":\"gpt-4o\",\"max_tokens\":1000,\"messages\":[{\"role\":\"user\",\"content\":[";
    requestBody += "{\"type\":\"text\",\"text\":\"" + prompt + "\"},";
    requestBody += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64," + imageBase64 + "\"}}";
    requestBody += "]}]}";
    
    int httpCode = retryWithBackoff([&]() -> int {
      WiFiClientSecure client;
      client.setInsecure();
      client.setTimeout(60);
      
      HTTPClient https;
      if (!https.begin(client, "https://api.openai.com/v1/chat/completions")) {
        return HTTPC_ERROR_CONNECTION_REFUSED;
      }
      
      https.addHeader("Content-Type", "application/json");
      https.addHeader("Authorization", "Bearer " + String(apiKey));
      https.setTimeout(90000);
      
      int code = https.POST(requestBody);
      
      if (code == 200) {
        String response = https.getString();
        DynamicJsonDocument responseDoc(6144);
        DeserializationError error = deserializeJson(responseDoc, response);
        
        if (error == DeserializationError::Ok) {
          result = responseDoc["choices"][0]["message"]["content"].as<String>();
          success = true;
          Serial.println("✓ Analysis received");
        } else {
          code = -999; // JSON parse error - retry
        }
      }
      
      https.end();
      return code;
    }, config);
    
    // Clear request body to free memory
    requestBody = "";
    
    if (success) {
      return result;
    } else {
      return "Error: Vision API failed after " + String(config.maxRetries + 1) + " attempts (code: " + String(httpCode) + ")";
    }
  }
  
  // TTS API with retry
};

// ============================================================================
// CIRCUIT BREAKER PATTERN (Optional - Advanced)
// ============================================================================
// 
// Prevents repeated calls to a failing service
// Opens circuit after threshold failures
// Allows occasional retry attempts
// 

class CircuitBreaker {
public:
  enum State { CLOSED, OPEN, HALF_OPEN };
  
  CircuitBreaker(int threshold = 5, unsigned long timeoutMs = 60000) 
    : failureThreshold(threshold), resetTimeout(timeoutMs) {
    reset();
  }
  
  bool canAttempt() {
    unsigned long now = millis();
    
    switch (state) {
      case CLOSED:
        return true;
        
      case OPEN:
        if (now - lastFailureTime >= resetTimeout) {
          Serial.println("Circuit breaker: Attempting recovery (HALF_OPEN)");
          state = HALF_OPEN;
          return true;
        }
        Serial.println("Circuit breaker: OPEN - blocking call");
        return false;
        
      case HALF_OPEN:
        return true;
    }
    
    return false;
  }
  
  void recordSuccess() {
    if (state == HALF_OPEN) {
      Serial.println("Circuit breaker: Recovery successful (CLOSED)");
    }
    reset();
  }
  
  void recordFailure() {
    failureCount++;
    lastFailureTime = millis();
    
    if (state == HALF_OPEN) {
      Serial.println("Circuit breaker: Recovery failed (OPEN)");
      state = OPEN;
    } else if (failureCount >= failureThreshold) {
      Serial.printf("Circuit breaker: Threshold reached (%d failures) (OPEN)\n", failureCount);
      state = OPEN;
    }
  }
  
  State getState() { return state; }
  int getFailureCount() { return failureCount; }
  
private:
  State state;
  int failureCount;
  int failureThreshold;
  unsigned long resetTimeout;
  unsigned long lastFailureTime;
  
  void reset() {
    state = CLOSED;
    failureCount = 0;
    lastFailureTime = 0;
  }
};

#endif // ERROR_HANDLING_H
