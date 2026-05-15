// Import libary yang dibutuhkan
#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// Konfigurasi WIFI
const char* ssid = "1am";
const char* password = "mimpi123";

// Konfigurasi MQTT
const char* mqtt_server = "broker.emqx.io";
WiFiClient espClient;
PubSubClient client(espClient);

// Konfigurasi Bot Telegram
const char* BOT_TOKEN = "8048484142:AAHdxK967uwALedOFJbIJwBK56sXHMRLqoI";
const char* CHAT_ID = "-5288176995";

// Koneksi secure HTTPS untuk telegram
WiFiClientSecure secured_client;

// Inisialisasi bot telegram
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// konfigurasi pin sensor ultrasonik
#define TRIG 5
#define ECHO 18

// Konfigurasi pin LED & buzzer
#define LED1 26
#define LED2 27
#define LED3 14
#define LED4 12
#define BUZZER 13

// Konfigurasi pin sensor suhu DS18B20
#define ONE_WIRE_BUS 25

// Inisialisasi komunikasi OneWire untuk sensor suhu DS18B20
OneWire oneWire(ONE_WIRE_BUS);

// Inisialisasi object sensor suhu DS18B20
DallasTemperature sensors(&oneWire);

// Tinggi dari tangki air (cm)
float tinggiTangki = 17.0;

// State mode auto/manual
bool modeAuto = true;

// data terakhir dari sensor ultrasonik dan sensor suhu DS18B20
float lastPersen = 0;
float lastSuhu = 0;

// state notifikasi telegram
bool notifHampirHabis = false;
bool notifHampirPenuh = false;
bool notifSuhuDingin = false;
bool notifSuhuPanas = false;


// Fungsi untuk menghubungkan ESP32 ke jaringan WIFI
void setup_wifi() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());
}

// Callback untuk menerima dan memproses pesan MQTT
void callback(char* topic, byte* payload, unsigned int length) {

  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  String topicStr = String(topic);

  if (topicStr == "iot7/iot/mode") {
    modeAuto = (message == "AUTO");
    return;
  }

  if (!modeAuto) {
    if (topicStr == "iot7/iot/led1") digitalWrite(LED1, message == "ON");
    if (topicStr == "iot7/iot/led2") digitalWrite(LED2, message == "ON");
    if (topicStr == "iot7/iot/led3") digitalWrite(LED3, message == "ON");
    if (topicStr == "iot7/iot/status_buzzer") digitalWrite(BUZZER, message == "ON");
  }
}

// Fungsi untuk Reconnect MQTT jika koneksi terputus
void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_IOT")) {

      client.subscribe("iot7/iot/mode");
      client.subscribe("iot7/iot/led1");
      client.subscribe("iot7/iot/led2");
      client.subscribe("iot7/iot/led3");
      client.subscribe("iot7/iot/status_buzzer");

    } else {
      delay(2000);
    }
  }
}

// Fungsi untuk membaca level air dan mengubah menjadi persen isi tangki
float bacaPersen() {

  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long durasi = pulseIn(ECHO, HIGH, 40000);

  if (durasi == 0) return -1;

  float jarak = durasi * 0.034 / 2;
  float tinggiAir = tinggiTangki - jarak;
  float persen = (tinggiAir / tinggiTangki) * 100;

  if (persen < 0) persen = 0;
  if (persen > 100) persen = 100;

  return persen;
}

// Fungsi untuk mengambil status tangki air
String getStatus(float p) {
  if (p <= 20) return "Hampir Habis";
  else if (p <= 75) return "Normal";
  return "Hampir Penuh";
}

// Fungsi untuk mengambil status suhu
String getStatusSuhu(float s) {

  if (s < 20) {
    return "Dingin";
  } else if (s <= 30) {
    return "Normal";
  }

  return "Panas";
}

// Fungsi untuk mengambil status LED
String getStatusLed() {

  String status = "";

  status += "LED1 : ";
  status += digitalRead(LED1) ? "ON\n" : "OFF\n";

  status += "LED2 : ";
  status += digitalRead(LED2) ? "ON\n" : "OFF\n";

  status += "LED3 : ";
  status += digitalRead(LED3) ? "ON\n" : "OFF\n";

  status += "Buzzer : ";
  status += digitalRead(BUZZER) ? "ON" : "OFF";

  return status;
}

// Fungsi untuk menangani command dan notifikasi telegram
void handleTelegram() {
  // Notifikasi otomatis saat air hampir habis
  if (lastPersen < 20 && !notifHampirHabis) {

    String msg = "⚠️ PERINGATAN\n";
    msg += "━━━━━━━━━━\n";
    msg += "Air tandon hampir habis!\n\n";
    msg += "💧 Level Air : " + String(lastPersen, 1) + "%";

    bot.sendMessage(CHAT_ID, msg, "");

    notifHampirHabis = true;
  }

  // Reset state notifikasi habis jika air naik lagi
  if (lastPersen >= 20) {
    notifHampirHabis = false;
  }

  // Notifikasi otomatis air hampir penuh
  if (lastPersen >= 75 && !notifHampirPenuh) {

    String msg = "✅ INFO TANDON\n";
    msg += "━━━━━━━━━━\n";
    msg += "Tandon hampir penuh!\n\n";
    msg += "💧 Level Air : " + String(lastPersen, 1) + "%";

    bot.sendMessage(CHAT_ID, msg, "");

    notifHampirPenuh = true;
  }

  // Reset state notifikasi penuh jika air turun lagi
  if (lastPersen < 70) {
    notifHampirPenuh = false;
  }

  if (lastSuhu < 20 && !notifSuhuDingin) {

    String msg = "❄️ PERINGATAN SUHU\n";
    msg += "━━━━━━━━━━\n";
    msg += "Suhu air terlalu dingin!\n\n";
    msg += "🌡 Suhu : " + String(lastSuhu, 1) + " C\n";
    msg += "📌 Status : " + getStatusSuhu(lastSuhu);

    bot.sendMessage(CHAT_ID, msg, "");

    notifSuhuDingin = true;
  }

  // reset notif dingin jika suhu naik
  if (lastSuhu >= 20) {
    notifSuhuDingin = false;
  }

  if (lastSuhu > 30 && !notifSuhuPanas) {

    String msg = "🔥 PERINGATAN SUHU\n";
    msg += "━━━━━━━━━━\n";
    msg += "Suhu air terlalu panas!\n\n";
    msg += "🌡 Suhu : " + String(lastSuhu, 1) + " C\n";
    msg += "📌 Status : " + getStatusSuhu(lastSuhu);

    bot.sendMessage(CHAT_ID, msg, "");

    notifSuhuPanas = true;
  }

  // reset notif panas jika suhu turun
  if (lastSuhu <= 30) {
    notifSuhuPanas = false;
  }
}

// Task FreeRTOS untuk monitoring sensor dan komunikasi MQTT
void taskSensorMQTT(void* pv) {
  while (1) {

    // Membaca persentase air
    float p = bacaPersen();
    if (p >= 0) lastPersen = p;

    // Membaca suhu DS18B20
    sensors.requestTemperatures();
    lastSuhu = sensors.getTempCByIndex(0);

    // AUTO MODE
    if (modeAuto) {

      // Reset semua output
      digitalWrite(LED1, LOW);
      digitalWrite(LED2, LOW);
      digitalWrite(LED3, LOW);
      digitalWrite(BUZZER, LOW);

      if (lastPersen < 20) {  // indikator persentase air hampir habis
        digitalWrite(LED1, HIGH);
      } else if (lastPersen < 75) {  // // indikator persentase air Normal
        digitalWrite(LED2, HIGH);

      } else {  // Indikator persentase air  hampir penuh
        digitalWrite(LED3, HIGH);
        digitalWrite(BUZZER, HIGH);
      }

      if (lastSuhu < 20) {
        digitalWrite(LED4, HIGH);
        vTaskDelay(300 / portTICK_PERIOD_MS);

        digitalWrite(LED4, LOW);
        vTaskDelay(300 / portTICK_PERIOD_MS);
      }

      else if (lastSuhu <= 30) {
        // suhu normal
        digitalWrite(LED4, LOW);
      }

      else {
        digitalWrite(LED4, HIGH);
      }
    }

    // MQTT PUBLISH
    if (client.connected()) {
      client.publish("iot7/iot/ketinggian", String(lastPersen).c_str());
      client.publish("iot7/iot/suhu", String(lastSuhu).c_str());
      client.publish("iot7/iot/status_tandon", getStatus(lastPersen).c_str());
      client.publish("iot7/iot/status_suhu", getStatusSuhu(lastSuhu).c_str());
    }

    // Delay task selama 2 detik
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// Task FreeRTOS untuk menangani bot Telegram
void taskTelegram(void* pv) {
  while (1) {

    // Menjalankan proses telegram
    handleTelegram();

    // Delay task selama 3 detik
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// Task FreeRTOS untuk menjaga koneksi dan komunikasi MQTT
void taskMQTT(void* pv) {

  while (1) {

    // cek koneksi WIFI
    if (WiFi.status() == WL_CONNECTED) {

      // Reconnect MQTT jika terputus
      if (!client.connected()) {
        reconnect();
      }

      // Menjalankan proses MQTT dan callback
      client.loop();
    }

    // delay task
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {

  // Inisialisasi koneksi WiFi
  setup_wifi();

  // Konfigurasi broker dan callback MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Inisialisasi sensor suhu DS18B20
  sensors.begin();

  // Mengaktifkan koneksi HTTPS Telegram
  secured_client.setInsecure();

  // Konfigurasi pin sensor ultrasonik
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  // Konfigurasi pin LED & buzzer
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // reset awal trigger ultrasonic
  digitalWrite(TRIG, LOW);

  delay(2000);

  // FREERTOS TASK
  xTaskCreatePinnedToCore(taskSensorMQTT, "Sensor", 10000, NULL, 1, NULL, 1);  //  Task monitoring sensor dan publish MQTT
  xTaskCreatePinnedToCore(taskTelegram, "Telegram", 10000, NULL, 1, NULL, 0);  // Task bot telegram
  xTaskCreatePinnedToCore(taskMQTT, "MQTT", 8000, NULL, 1, NULL, 0);           // Task Komunikasi MQTT
}

// Reconnect WiFi jika koneksi terputus
void reconnectWiFi() {

  // Cek status koneksi WiFi
  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi Disconnect...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    Serial.println("\nWiFi Reconnected");
  }
}

void loop() {

  // Reconnect WiFi jika terputus
  reconnectWiFi();

  // Delay loop utama
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}