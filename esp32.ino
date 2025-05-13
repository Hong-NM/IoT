#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// Cấu hình WiFi
const char* ssid = "TECNO POVA 2";
const char* password = "123456789";

// Cấu hình MQTT
const char* mqtt_server = "103.6.234.189";
const int mqtt_port = 1883;
const char* mqtt_user = "admin";
const char* mqtt_password = "admin";
const char* topic = "smartfarm/farm_001/esp32_001/data";

WiFiClient espClient;
PubSubClient client(espClient);

// Cảm biến DHT11
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Cảm biến MQ135 (Analog)
#define MQ135_PIN 34

// Cảm biến ánh sáng quang trở (Analog)
#define LIGHT_SENSOR_PIN 35

// Hàm kết nối WiFi
void setup_wifi() {
  delay(10);
  Serial.println("\nConnecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");
}

// Hàm kết nối MQTT
void reconnect() {
  while (!client.connected()) {
    Serial.print("🔁 Connecting to MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("✅ connected");
    } else {
      Serial.print("❌ failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Đọc dữ liệu cảm biến
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int co2 = analogRead(MQ135_PIN);
  int light = analogRead(LIGHT_SENSOR_PIN);

  // Mô phỏng pin
  int battery = random(70, 100);

  // Tạo JSON
  String json = "{";
  json += "\"device_id\":\"esp32_001\",";
  json += "\"location_id\":\"farm_001\",";
  json += "\"timestamp\":" + String(millis() / 1000) + ",";
  json += "\"data\":{";
  json += "\"temperature\":" + String(temperature) + ",";
  json += "\"humidity\":" + String(humidity) + ",";
  json += "\"light_intensity\":" + String(light) + ",";
  json += "\"co2_level\":" + String(co2);
  json += "},";
  json += "\"battery\":" + String(battery);
  json += "}";

  // Gửi dữ liệu
  client.publish(topic, json.c_str());
  Serial.println("Đã gửi MQTT:");
  Serial.println(json);

  delay(5000);
}