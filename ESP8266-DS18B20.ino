/*  ESP8266 with DS18B20 Temp Sensor and MQTT Client v1.0

  Original Code from
  - https://github.com/KmanOz/Sonoff-HomeAssistant/blob/master/arduino/ESPsonoff-v1.01t/ESPsonoff-v1.01t.ino
  - https://github.com/iot-playground/Arduino/blob/master/ESP8266ArduinoIDE/DS18B20_temperature_sensor/DS18B20_temperature_sensor.ino

  External libraries:
  - https://github.com/milesburton/Arduino-Temperature-Control-Library
  - https://github.com/PaulStoffregen/OneWire

  Home Assistant:
  - add this to your configuration.yaml to show up

sensor:
  - platform: mqtt
    name: "Temperature"
    state_topic: "home/esp8266/tempsensor1/1/temp"
    qos: 1
    unit_of_measurement: "°C"
    value_template: "{{ value_json.Temp }}
*/


#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include "OneWire.h"
#include "DallasTemperature.h"

#define BUTTON          0  //Sonoff 0                                   
#define RELAY           12 //Sonoff 12                                  
#define LED             14 //Sonoff 13
#define ONE_WIRE_BUS 2     //Sonoff 14  DS18B20 pin  (4.7k resistor between +ve and data)

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

#define MQTT_CLIENT     "TempSensor1"                        // mqtt client_id (Must be unique for each Sonoff)
#define MQTT_SERVER     "192.168.0.100"                      // mqtt server
#define MQTT_PORT       1883                                 // mqtt port
#define MQTT_TOPIC      "home/sonoff/living_room/1"          // mqtt topic (Must be unique for each Sonoff)
#define MQTT_USER       "user"                               // mqtt user
#define MQTT_PASS       "pass"                               // mqtt password

#define WIFI_SSID       "homewifi"                           // wifi ssid
#define WIFI_PASS       "homepass"                           // wifi password

#define VERSION    "\n\n----------------  ESP8266 with DS18B20 Temp Sensor v1.0  -----------------"

bool rememberRelayState = true;                              // If 'true' remembers the state of the relay before power loss.
bool requestRestart = false;                                 // (Do not Change)
bool sendStatus = false;                                     // (Do not Change)
bool tempReport = false;                                     // (Do not Change)

int kUpdFreq = 1;                                            // Update frequency in Mintes to check for mqtt connection
int kRetries = 10;                                           // WiFi retry count. Increase if not connecting to router.
int lastRelayState;                                          // (Do not Change)

unsigned long TTasks;                                        // (Do not Change)
unsigned long count = 0;                                     // (Do not Change)

extern "C" { 
  #include "user_interface.h" 
}

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient, MQTT_SERVER, MQTT_PORT);
Ticker btn_timer;

void callback(const MQTT::Publish& pub) {
  if (pub.payload_string() == "stat") {
  }
  else if (pub.payload_string() == "on") {
    digitalWrite(RELAY, HIGH);
  }
  else if (pub.payload_string() == "off") {
    digitalWrite(RELAY, LOW);
  }
  else if (pub.payload_string() == "reset") {
    requestRestart = true;
  }
  else if (pub.payload_string() == "temp") {
    tempReport = true;
  }
  sendStatus = true;
}

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(BUTTON, INPUT);
  digitalWrite(LED, HIGH);
  digitalWrite(RELAY, LOW);
  Serial.begin(115200);
  EEPROM.begin(8);
  lastRelayState = EEPROM.read(0);
  if (rememberRelayState && lastRelayState == 1) {
     digitalWrite(RELAY, HIGH);
  }
  btn_timer.attach(0.05, button);
  mqttClient.set_callback(callback);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println(VERSION);
  Serial.print("\nUnit ID: ");
  Serial.print("esp8266-");
  Serial.print(ESP.getChipId(), HEX);
  Serial.print("\nConnecting to "); Serial.print(WIFI_SSID); Serial.print(" Wifi"); 
  while ((WiFi.status() != WL_CONNECTED) && kRetries --) {
    delay(500);
    Serial.print(" .");
  }
  if (WiFi.status() == WL_CONNECTED) {  
    Serial.println(" DONE");
    Serial.print("IP Address is: "); Serial.println(WiFi.localIP());
    Serial.print("Connecting to ");Serial.print(MQTT_SERVER);Serial.print(" Broker . .");
    delay(500);
    while (!mqttClient.connect(MQTT::Connect(MQTT_CLIENT).set_keepalive(90).set_auth(MQTT_USER, MQTT_PASS)) && kRetries --) {
      Serial.print(" .");
      delay(1000);
    }
    if(mqttClient.connected()) {
      Serial.println(" DONE");
      Serial.println("\n----------------------------  Logs  ----------------------------");
      Serial.println();
      mqttClient.subscribe(MQTT_TOPIC);
      blinkLED(LED, 40, 8);
      digitalWrite(LED, LOW);
    }
    else {
      Serial.println(" FAILED!");
      Serial.println("\n----------------------------------------------------------------");
      Serial.println();
    }
  }
  else {
    Serial.println(" WiFi FAILED!");
    Serial.println("\n----------------------------------------------------------------");
    Serial.println();
  }
  getTemp();
}

void loop() { 
  mqttClient.loop();
  timedTasks();
  checkStatus();
  if (tempReport) {
    getTemp();
  }
}

void blinkLED(int pin, int duration, int n) {             
  for(int i=0; i<n; i++)  {  
    digitalWrite(pin, HIGH);        
    delay(duration);
    digitalWrite(pin, LOW);
    delay(duration);
  }
}

void button() {
  if (!digitalRead(BUTTON)) {
    count++;
  } 
  else {
    if (count > 1 && count <= 40) {   
      digitalWrite(RELAY, !digitalRead(RELAY));
      sendStatus = true;
    } 
    else if (count >40){
      Serial.println("\n\nESP8266 Rebooting . . . . . . . . Please Wait"); 
      requestRestart = true;
    } 
    count=0;
  }
}

void checkConnection() {
  if (WiFi.status() == WL_CONNECTED)  {
    if (mqttClient.connected()) {
      Serial.println("mqtt broker connection . . . . . . . . . . OK");
    } 
    else {
      Serial.println("mqtt broker connection . . . . . . . . . . LOST");
      requestRestart = true;
    }
  }
  else { 
    Serial.println("WiFi connection . . . . . . . . . . LOST");
    requestRestart = true;
  }
}

void checkStatus() {
  if (sendStatus) {
    if(digitalRead(RELAY) == LOW)  {
      if (rememberRelayState) {
        EEPROM.write(0, 0);
      }
      mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/stat", "off").set_retain().set_qos(1));
      Serial.println("Relay . . . . . . . . . . . . . . . . . . OFF");
    } else {
      if (rememberRelayState) {
        EEPROM.write(0, 1);
      }
      mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/stat", "on").set_retain().set_qos(1));
      Serial.println("Relay . . . . . . . . . . . . . . . . . . ON");
    }
    if (rememberRelayState) {
      EEPROM.commit();
    }
    sendStatus = false;
  }
  if (requestRestart) {
    blinkLED(LED, 400, 4);
    ESP.restart();
  }
}

void getTemp() {
 
  Serial.print("DS18B20 read . . . . . . . . . . . . . . . . . ");
  float temp;
  char message_buff[60];

  do {
    DS18B20.requestTemperatures(); 
    temp = DS18B20.getTempCByIndex(0);
    Serial.print("Temperature: ");
    Serial.println(temp);
    } while (temp == 85.0 || temp == (-127.0));

    if (isnan(temp)) {
    mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/debug","\"DS18B20 Read Error\"").set_retain().set_qos(1));
    Serial.println("ERROR");
    tempReport = false;
    return;
  }
  String pubString = "{\"Temp\": "+String(temp)+"}";
  pubString.toCharArray(message_buff, pubString.length()+1);
  mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/temp", message_buff).set_retain().set_qos(1));
  Serial.println("OK");
    tempReport = false;
}

void timedTasks() {
  if ((millis() > TTasks + (kUpdFreq*60000)) || (millis() < TTasks)) { 
    TTasks = millis();
    checkConnection();
    tempReport = true;
  }
}
