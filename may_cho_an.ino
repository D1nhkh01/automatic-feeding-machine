#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Servo.h>
#include "HX711.h"

const char *ssid = "****";
const char *password = "********";
const char* mqtt_server = "192.168.****";
const char* topic = "v1/devices/me/telemetry";
const char* control_topic = "v1/devices/me/rpc/request/+";
const char* user_mqtt="***";
const char* pwd_mqtt="***";


WiFiClient espClient;
PubSubClient client(espClient);

#define DHTPIN D1          // Chân kết nối của cảm biến DHT11 trên ESP8266
#define DHTTYPE DHT11      // Loại cảm biến DHT11
DHT dht(DHTPIN, DHTTYPE); // Khởi tạo đối tượng cảm biến DHT11

#define TRIGGER_PIN D5     // Chân kết nối của cảm biến khoảng cách trên ESP8266
#define ECHO_PIN D6        // Chân kết nối của cảm biến khoảng cách trên ESP8266

Servo servo1;
Servo servo2;
bool State;

const int LOADCELL_DOUT_PIN = D7;
const int LOADCELL_SCK_PIN = D8;
HX711 scale;

// Lưu giá trị từ load cell
long net, residual, consume;

// Tạo một đối tượng JSON
StaticJsonDocument<200> doc;


void callback(char* topic, byte* payload, unsigned int length) {
  // Tạo một buffer để lưu chuỗi payload
  char buffer[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    buffer[i] = (char)payload[i];
  }

  // Tạo một đối tượng JSON để phân tích chuỗi payload
  DynamicJsonDocument doc(200);
  deserializeJson(doc, buffer);

  // Lấy giá trị của params từ đối tượng JSON
  bool params = doc["params"];
  String method = doc["method"];

  int pos;
  // Kiểm tra và xử lý giá trị params
  if (method == "setValue") {
    if (params == 1) {
      State = 1;
      net = scale.get_units(10); // Cân khối lượng thức ăn đưa ra
      Serial.println("Mở khay"); // Ghi log
      for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
        servo1.write(pos);                   // tell servo to go to position in variable 'pos'
        delay(70);                           // waits 50ms for the servo to reach the position
      }
      servo1.write(0);      
      Serial.println(net);
    } else {
      State = 0;
      Serial.println("Đóng khay"); // Ghi log
      for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
        servo1.write(pos);                  // tell servo to go to position in variable 'pos'
        delay(70); 
      }
      servo1.write(180);

      residual = scale.get_units(10); // Cân khối lượng thức ăn dư
      delay(2000);
      consume = net - residual; //Tính lượng thức ăn tiêu thụ
      Serial.println("dư " + String(residual));
      Serial.println("tiêu thụ " + String(consume));
    }
  }
  else  if (method == "setValue1" && params == 1 && State ==0) {
    if (params == 1) {
      servo2.write(180); // Góc Servo 2 là 180 độ
      delay(700);
      servo2.write(0); // Góc Servo 2 là 0 độ
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    if (client.connect("automatic_feeding_machine", user_mqtt, pwd_mqtt)) {
      Serial.println("connected");

      // Subscribe để nhận thông tin điều khiển từ dashboard
      client.subscribe(control_topic);
      Serial.print("Subscribed to: ");
      Serial.println(control_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.print(" wifi=");
      Serial.print(WiFi.status());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(10000);
    }
  }
}

void setup() {
  servo1.attach(D3); // Kết nối Servo 1 với chân D3 trên NodeMCU
  servo2.attach(D4); // Kết nối Servo 2 với chân D4 trên NodeMCU
  servo1.write(0);
  servo2.write(0);
  Serial.begin(115200);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  dht.begin();
  pinMode(TRIGGER_PIN, OUTPUT);  // Khai báo chân TRIGGER là đầu ra
  pinMode(ECHO_PIN, INPUT);      // Khai báo chân ECHO là đầu vào

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  // Tare the scale (reset it to 0)
  scale.set_scale(2131.3);
  scale.tare();

  delay(2000);
}

void loop() {
  // confirm still connected to mqtt server
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  float temperature = dht.readTemperature(); // Đọc nhiệt độ
  float humidity = dht.readHumidity();       // Đọc độ ẩm
  float reserved;

  // Đo khoảng cách bằng cảm biến khoảng cách
  long duration, distance;
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);
  duration = pulseIn(ECHO_PIN, HIGH);
  distance = duration * 0.034 / 2;
  reserved = distance*10; //Tính % thức ăn dự trữ bằng khoảng cách từ nắp tới bề mặt thức ăn
 
  // Thêm các trường dữ liệu vào đối tượng JSON
  doc["tem"] = temperature; //Nhiệt độ hộp chứa thức ăn dự trữ
  doc["hum"] = humidity; //Độ ẩm thức ăn dự trữ
  doc["reserved"] = reserved; //lượng thức ăn dự trữ      
  doc["consume"] = consume; // Khối lượng thức ăn đo được từ load cell
  doc["residual"] = residual;
  
  // Chuyển đổi đối tượng JSON thành chuỗi JSON
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(topic, jsonString.c_str(), true);
  delay(1000);
}
