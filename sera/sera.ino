#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "DHT.h" // DHT kütüphanesini yüklemeyi unutmayın!

// --- AYARLAR ---
#define WIFI_SSID "Esptestwifi"
#define WIFI_PASSWORD "12345678"

#define API_KEY "AIzaSyAR1cyO7pFPM5RLvO308uAcSyf5HaqAizM"
#define FIREBASE_PROJECT_ID "akillisera-b71d4"
#define APP_ID "master-iot-final-v3"

// --- PİNLER ---
#define DHTPIN 4       // DHT11 Sensörünün Data pini
#define DHTTYPE DHT11
#define PUMP_PIN 2     // Su Pompası (TEST İÇİN ESP32 DAHİLİ MAVİ LED'E AYARLANDI)
#define FAN_PIN 18     // Fan Motoru (PWM uyumlu pin, parlaklığı değişen harici LED takabilirsiniz)

DHT dht(DHTPIN, DHTTYPE);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

String seraPath = "artifacts/" + String(APP_ID) + "/public/data/greenhouse/status";

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  // WiFi Bağlantısı
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi baglaniliyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi baglandi!");

  // Firebase Bağlantısı
  config.api_key = API_KEY;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (!Firebase.ready()) return;

  static unsigned long lastCheck = 0;

  // Her 3 saniyede bir işlem yap (Sera verileri yavaş güncellenir)
  if (millis() - lastCheck > 3000) {
    lastCheck = millis();

    // 1. SENSÖRLERİ OKU
    int t = dht.readTemperature();
    int h = dht.readHumidity();

    // Sensör bağlı değilse sistemi bozmamak için sahte veri (0) yollamasını engelleyelim
    if (isnan(t) || isnan(h)) {
      Serial.println("DHT11 Sensörü okunamadı! (Kabloları kontrol et)");
      t = 25; // Test için varsayılan değer
      h = 45; // Test için varsayılan değer
    }

    // 2. FİREBASE'E SICAKLIK VE NEMİ YAZ
    String seraUpdate = "{\"fields\":{\"temp\":{\"integerValue\":" + String(t) + "},\"hum\":{\"integerValue\":" + String(h) + "}}}";
    Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", seraPath.c_str(), seraUpdate.c_str(), "temp,hum");
    Serial.println("Sera verileri buluta gönderildi: " + String(t) + "C, %" + String(h));

    // 3. FİREBASE'DEN KOMUTLARI ÇEK
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", seraPath.c_str(), "")) {
      String payload = fbdo.payload();
      
      // Otonom Mod Durumu
      bool autoMode = false;
      int autoPos = payload.indexOf("\"auto\"");
      if (autoPos != -1 && payload.substring(autoPos, autoPos + 50).indexOf("true") > -1) autoMode = true;

      // Pompa Durumu
      bool pumpState = false;
      int pumpPos = payload.indexOf("\"pump\"");
      if (pumpPos != -1 && payload.substring(pumpPos, pumpPos + 50).indexOf("true") > -1) pumpState = true;

      // Fan Hızı Durumu (0 ile 255 arası değer çeker)
      int motorSpeed = 0;
      int speedPos = payload.indexOf("\"motorSpeed\"");
      if(speedPos > -1) {
         int valStart = payload.indexOf(":", speedPos);
         int valEnd = payload.indexOf("}", valStart);
         String valStr = payload.substring(valStart, valEnd);
         
         String numStr = "";
         for(int i=0; i<valStr.length(); i++) {
           if(isDigit(valStr[i])) numStr += valStr[i];
         }
         motorSpeed = numStr.toInt();
      }

      // 4. DONANIMLARI KONTROL ET
      if (autoMode) {
        Serial.println("SERA MODU: OTONOM (YAPAY ZEKA)");
        // Otonom moddaysa kararı sensörlere göre ESP32 kendisi verir
        if (t > 30) analogWrite(FAN_PIN, 255); else analogWrite(FAN_PIN, 0);
        
        // Nem %40'tan azsa test LED'ini (Pompayı) yak
        if (h < 40) digitalWrite(PUMP_PIN, HIGH); else digitalWrite(PUMP_PIN, LOW);
      } else {
        Serial.println("SERA MODU: MANUEL");
        // Manuel moddaysa kararı web sitesindeki butonlar belirler
        digitalWrite(PUMP_PIN, pumpState ? HIGH : LOW);
        analogWrite(FAN_PIN, motorSpeed); // Slider verisine göre fan (veya LED) hızı ayarlanır
      }
      
      Serial.println("Pompa (Mavi LED): " + String(pumpState ? "AÇIK" : "KAPALI") + " | Fan Hızı: " + String(motorSpeed));
      Serial.println("-------------------------------------------------");
    }
  }
}