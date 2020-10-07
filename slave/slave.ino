#define PHOTORESISTOR A0             // photoresistor pin
#define PHOTORESISTOR_THRESHOLD 128  // turn led on for light values lesser than this

#include <ArduinoJson.h>
#include "secrets.h"
#include <ESP8266WiFi.h>
#include <MQTT.h>

// network definition
char ssid[] = SECRET_SSID;   // your network SSID (name)
char pass[] = SECRET_PASS;   // your network password
IPAddress ip(IP);
IPAddress subnet(SUBNET);
IPAddress dns(DNS);
IPAddress gateway(GATEWAY);
char mqtt_broker[] = "iot.histella.myds.me";

// Client MQTT and WiFI
MQTTClient client;
WiFiClient net;

unsigned long tempo;

void setup()
{
  client.begin(mqtt_broker,1883, net);
  client.onMessage(messageReceived);
  
  Serial.begin(115200);
  Serial.println("\n\nSetup completed.\n\n"); 

}

void loop()
{
  tempo = millis();
  // connect to WiFi (if not already connected)
  connectToWiFi();

  if (!client.connected()) {
    connect_to_mqtt();
  }
  client.loop(); //MQTT client loop

  //every 10 sencond send light sensor value
  if (tempo % 10000 == 0){
    static unsigned int lightSensorValue;
  
    lightSensorValue = analogRead(PHOTORESISTOR);   // read analog value (range 0-1023)
    Serial.print("Light sensor value: ");
    Serial.println(lightSensorValue);
  
    const int capacity = JSON_OBJECT_SIZE(3); 
    StaticJsonDocument<capacity> doc;;
    doc["name"] = "slave1";
    doc["light"] = lightSensorValue;
    doc["wifirssi"] = WiFi.RSSI();
    char buffer[128];
    size_t n = serializeJson(doc, buffer);
    Serial.print("JSON message: ");
    Serial.println(buffer);
    client.publish("jsontopic", buffer, n);
  }

}

void printWifiStatus() {
  
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.println(WiFi.macAddress());
}

void connectToWiFi() {

  // wifi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(SECRET_SSID);

    while (WiFi.status() != WL_CONNECTED) {
      //WiFi.config(ip, dns, gateway, subnet);
      WiFi.mode(WIFI_STA);
      WiFi.begin(SECRET_SSID, SECRET_PASS);
      Serial.print(".");
      delay(5000);
    }
    Serial.println("\nConnected");
    printWifiStatus();
  }
}

void connect_to_mqtt() {

  Serial.print("\nconnecting to MQTT broker...");
  while (!client.connect("slave1", "mqtt", "2MFfprU2W")) { // client ID, username, password
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected!");

  client.subscribe("jsontopic");
  Serial.println("\nsubscribed to jsontopic topic!");
}

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
  StaticJsonDocument<128> doc;
  deserializeJson(doc, payload);

  const char* name = doc["name"];
  Serial.println(name);
  
  const int li = doc["light"];
  Serial.println(li);
  
  const float rssi = doc["wifirssi"];
  Serial.println(rssi);

 
}
