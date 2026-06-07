/*
  ESP32 DUCO Miner - Complete Working Version
  Change the 4 lines below and upload
*/

// ==================== CHANGE THESE 4 LINES ====================
#define WIFI_SSID     "your_wifi"
#define WIFI_PASSWORD "your_pass"
#define DUCO_USERNAME "your_username"
#define MINING_KEY    ""
#define RIG_NAME      "ESP32-Miner"
// ===============================================================

#include <WiFi.h> // invluding the wifi library for MINING lol... 😅
#include <mbedtls/sha1.h>

WiFiClient client;
String lastHash = "", targetHash = "";
int diff = 10;
bool gotJob = false;
unsigned long totalHashes = 0, accepted = 0, rejected = 0;
float hashrate = 0;
unsigned long lastStat = 0;
unsigned long lastAttempt = 0;
const unsigned long COOLDOWN = 5000;

// ==================== CONNECTION ====================
void connectWiFi() {
  Serial.print("📡 WiFi: ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK!");
    Serial.print("   IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println(" Failed!");
    ESP.restart();
  }
}

// ==================== GET JOB FROM SERVER ====================
bool getJob() {
  if (millis() - lastAttempt < COOLDOWN) return false;
  lastAttempt = millis();
  
  if (!client.connect("server.duinocoin.com", 2812)) {
    Serial.println("❌ Connection failed"); //I hope this doesnt happen to you
    return false;
  }
  
  client.print("JOB," + String(DUCO_USERNAME) + "," + String(MINING_KEY) + "\n");
  
  unsigned long start = millis();
  while (!client.available() && millis() - start < 8000) {
    delay(10);
  }
  
  if (client.available()) {
    String resp = client.readStringUntil('\n');
    resp.trim();
    
    int first = resp.indexOf(',');
    int second = resp.indexOf(',', first + 1);
    
    if (first > 0 && second > 0) {
      lastHash = resp.substring(0, first);
      targetHash = resp.substring(first + 1, second);
      diff = resp.substring(second + 1).toInt();
      if (diff < 1) diff = 10;
      
      Serial.print("⛏️ Job: diff="); Serial.print(diff);
      Serial.print(" target="); Serial.println(targetHash.substring(0, 16));
      gotJob = true;
      client.stop();
      return true;
    }
  }
  client.stop();
  return false;
}

// ==================== SUBMIT SHARE ====================
bool submitShare(uint32_t nonce) {
  if (!client.connect("server.duinocoin.com", 2812)) return false;
  
  client.print(String(nonce) + ",0,ESP_Miner," + String(RIG_NAME) + "\n");
  
  unsigned long start = millis();
  while (!client.available() && millis() - start < 10000) delay(10);
  
  if (client.available()) {
    String resp = client.readStringUntil('\n');
    resp.trim();
    if (resp.startsWith("GOOD")) {
      accepted++;
      Serial.println("✅ Share ACCEPTED!");
      client.stop();
      return true;
    } else {
      rejected++;
      Serial.print("❌ Rejected: "); Serial.println(resp);
      client.stop();
      return false;
    }
  }
  rejected++;
  client.stop();
  return false;
}

// ==================== FAST NUMBER TO STRING ====================
void numToStr(uint32_t num, char* out) {
  if (num == 0) {
    out[0] = '0';
    out[1] = '\0';
    return;
  }
  char temp[12];
  int i = 0;
  while (num > 0) {
    temp[i++] = '0' + (num % 10);
    num /= 10;
  }
  for (int j = 0; j < i; j++) out[j] = temp[i - 1 - j];
  out[i] = '\0';
}

// ==================== SHA-1 HASHING ====================
void hashThis(const char* input, uint8_t* output) {
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, (const uint8_t*)input, strlen(input));
  mbedtls_sha1_finish(&ctx, output);
  mbedtls_sha1_free(&ctx);
}

// ==================== HEX CONVERSION ====================
uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

void hexToBytes(const char* hex, uint8_t* bytes, int len) {
  for (int i = 0; i < len; i++) {
    bytes[i] = (hexNibble(hex[i*2]) << 4) | hexNibble(hex[i*2+1]);
  }
}

// ==================== THE ACTUAL MINING LOOP ====================
void doMining() {
  if (!gotJob) return;
  
  // Convert target hash to bytes once
  uint8_t targetBytes[20];
  hexToBytes(targetHash.c_str(), targetBytes, 20);
  
  // Calculate how many nonces to try
  uint32_t maxNonce = diff * 100;
  if (maxNonce < 1000) maxNonce = 1000;
  if (maxNonce > 50000) maxNonce = 50000;
  
  uint32_t nonce = 0;
  uint8_t result[20];
  char inputStr[80];
  char nonceStr[12];
  
  unsigned long startTime = micros();
  unsigned long startHashes = totalHashes;
  
  Serial.println("🔨 Mining started...");
  
  while (nonce < maxNonce && gotJob) {
    // Build string: lastHash + nonce (as string)
    numToStr(nonce, nonceStr);
    strcpy(inputStr, lastHash.c_str());
    strcat(inputStr, nonceStr);
    
    // Hash it
    hashThis(inputStr, result);
    totalHashes++;
    
    // Check if we found a valid share
    if (memcmp(result, targetBytes, 20) == 0) {
      Serial.print("🔑 Found nonce: "); Serial.println(nonce);
      if (submitShare(nonce)) {
        gotJob = false;
      }
      return;
    }
    
    nonce++;
    
    // Update hashrate every ~8000 hashes
    if ((nonce & 0x1FFF) == 0) {
      unsigned long elapsed = micros() - startTime;
      if (elapsed > 0) {
        unsigned long done = totalHashes - startHashes;
        hashrate = (done * 1000000.0) / elapsed;
      }
    }
    
    // Keep the system responsive
    if ((nonce & 0xFFF) == 0) yield();
  }
  
  // No nonce found in this range
  gotJob = false;
  Serial.println("🔄 No nonce found, getting new job...");
}

// ==================== STATS DISPLAY ====================
void showStats() {
  unsigned long up = millis() / 1000;
  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.print  ("│ Hashrate: "); Serial.print(hashrate, 0); Serial.print(" H/s");
  for(int i=0; i<15-String(hashrate,0).length(); i++) Serial.print(" ");
  Serial.println("│");
  Serial.print  ("│ Shares: A/"); Serial.print(accepted); 
  Serial.print(" R/"); Serial.println(rejected);
  Serial.print  ("│ Uptime: "); Serial.print(up/3600); Serial.print("h ");
  Serial.print((up%3600)/60); Serial.print("m "); Serial.print(up%60); Serial.print("s");
  Serial.println("   │");
  Serial.println("└─────────────────────────────────────┘");
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║     ESP32 DUCO Miner v3.0      ║");
  Serial.println("╚════════════════════════════════╝");
  
  // Overclock to 240MHz
  setCpuFrequencyMhz(240);
  Serial.print("⚡ CPU: "); Serial.print(getCpuFrequencyMhz()); Serial.println(" MHz");
  
  // Connect to WiFi
  connectWiFi();
  
  // CRITICAL: Wait before first connection
  delay(5000);
  
  // Get first job
  Serial.println("📡 Requesting first job...");
  getJob();
  
  lastStat = millis();
  Serial.println("⛏️ Ready to mine!\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  unsigned long now = millis();
  
  // Show stats every 10 seconds
  if (now - lastStat >= 10000) {
    showStats();
    lastStat = now;
  }
  
  // If we have a job, MINE IT
  if (gotJob) {
    doMining();
  } 
  // If no job, get one
  else {
    getJob();
    delay(100);
  }
  
  // Keep WiFi alive
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi lost! Reconnecting...");
    connectWiFi();
    delay(2000);  // I hope this works, this just ensures stable wifi ig
  }
  
  delay(10);
} //I tested this, should work, it searched for a job from the server, i made a delay if it does not receive a job. 
