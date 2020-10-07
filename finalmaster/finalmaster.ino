#include "secrets.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>  // display library
#include <Wire.h>               // I2C library
#include <InfluxDbClient.h>
#include <DHT.h>
#include <TM1637Display.h>
#include <string>
#include <ctime>
#include <MQTT.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <ESP8266TelegramBOT.h>
#include <Servo.h>
//-------------DEFINES------------------
#define SERVO_PIN D3
#define SERVO_PWM_MIN 500   // minimum PWM pulse in microsecond
#define SERVO_PWM_MAX 2500  // maximum PWM pulse in microsecond
//LCD
#define DISPLAY_CHARS 16     // number of characters on a line
#define DISPLAY_LINES 2      // number of display lines
#define DISPLAY_ADDR 0x27    // display address on I2C bus
LiquidCrystal_I2C lcd(DISPLAY_ADDR, DISPLAY_CHARS, DISPLAY_LINES);
//dht11
#define DHTPIN D4        // sensor I/O pin, eg. D1 (D0 and D4 are already used by board LEDs)
#define DHTTYPE DHT11    // sensor type DHT 11 
//photoresistor
#define PHOTORESISTOR A0
// Initialize DHT sensor
DHT dht = DHT(DHTPIN, DHTTYPE);
//influxdb
#define LED_ONBOARD LED_BUILTIN_AUX   // D0
#define RSSI_THRESHOLD -60
//#define INFLUXDB_HOST "192.168.81.14"
#define INFLUXDB_URL "http://192.168.1.105:9999"
// InfluxDB 2 server or cloud API authentication token (Use: InfluxDB UI -> Load Data -> Tokens -> <select token>)
#define INFLUXDB_TOKEN "IIOKL1bNWz490qkS4KX223W5bM_ovZeVPFHgQUNn1WSY64p5YI3UwrggBfrIyWdcr808qmaIuiD6oPrqQBokQA=="
// InfluxDB 2 organization id (Use: InfluxDB UI -> Settings -> Profile -> <name under tile> )
#define INFLUXDB_ORG "labiot2020"
// InfluxDB 2 bucket name (Use: InfluxDB UI -> Load Data -> Buckets)
#define INFLUXDB_BUCKET "esp8266"
// InfluxDB v1 database name
//#define INFLUXDB_DB_NAME "database"
#define DEVICE "ESP8266"

#define BUTTON D5
#define BUTTON_DEBOUNCE_DELAY 20 
#define TILT D6
#define PowerLED D8

#define TM1637_DIO D7    // chip digital I/O

//------------------global variables---------------
Servo servo;
String weather;
const char weather_server[] = "api.openweathermap.org";
const char weather_query[] = "GET /data/2.5/weather?q=%s,%s&units=metric&APPID=%s";

// WiFi
char ssid[] = SECRET_SSID;   // your network SSID (name)
char pass[] = SECRET_PASS;   // your network password

char mqtt_broker[] = "iot.histella.myds.me";

// Clienth MQTT and WiFI
MQTTClient client;

//WiFiClient client;
WiFiClient net;
void Bot_ExecMessages();

TelegramBOT bot(BOTtoken, BOTname, BOTusername);

int Bot_mtbs = 1000; //mean time between scan messages
long Bot_lasttime;   //last time messages' scan has been d
bool Start = false;

InfluxDBClient client_idb(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
InfluxDBClient client_slave(INFLUXDB_URL, INFLUXDB_ORG, "slave", INFLUXDB_TOKEN);

Point pointDevice("device_status");
Point pointSlave("slave");

ESP8266WebServer server(80);
bool system_switch = HIGH;
float t;
float h;

static unsigned int lightSensorValue;
int MQTTlight = 0;
boolean slave1 = true;
unsigned long tempo;

long connectToWiFi() {
  long rssi_strenght;
  // connect to WiFi (if not already connected)
  if (WiFi.status() != WL_CONNECTED) {
    lcd.home();
    lcd.print("wifi lost");
    Serial.print("Connecting to SSID: ");
    Serial.println(ssid);
    //WiFi.config(ip, dns, gateway, subnet);   // by default network is configured using DHCP
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(250);
    }
    Serial.println("\nConnected!");
    rssi_strenght = WiFi.RSSI(); //get wifi signal strenght
    //printWifiStatus();
  }
  else {
    rssi_strenght = WiFi.RSSI(); //get wifi signal strenght
  }

  return rssi_strenght;
}

void checklcd(){
  Serial.println("\n\nCheck LCD connection...");
  Wire.begin();
  Wire.beginTransmission(DISPLAY_ADDR);
  byte error = Wire.endTransmission();

  if (error == 0) {
    Serial.println("LCD found.");
    lcd.begin(DISPLAY_CHARS, 2);   // initialize the lcd

  } else {
    Serial.print("LCD not found. Error ");
    Serial.println(error);
    Serial.println("Check connections and configuration. Reset to try again!");
    while (true);
  }
  Serial.println("\n\nSetup completed.\n\n");
  lcd.setBacklight(255);
}

void settime(){
  configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("\nWaiting for time");
  while (!time(nullptr)) {
    Serial.print(".");
    delay(1000);
  }
}

int WriteMultiToDB(char ssid[], int rssi, int alarm, float temp, int humidity, int light, int tiltVal) {

  // Store measured value into point
  pointDevice.clearFields();
  // Report RSSI of currently connected network
  pointDevice.addField("rssi", rssi);
  pointDevice.addField("alarm", alarm);
  pointDevice.addField("temperature", temp);
  pointDevice.addField("humidity", humidity);
  //pointDevice.addField("light", light);
  pointDevice.addField("tilt", tiltVal);
  Serial.print("Writing: ");
  Serial.println(pointDevice.toLineProtocol());
  if (!client_idb.writePoint(pointDevice)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client_idb.getLastErrorMessage());
  }

}
void check_influxdb() {
  // Check server connection
  if (client_idb.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client_idb.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client_idb.getLastErrorMessage());
  }
}

boolean isButtonPressed() {
  static byte lastState = digitalRead(BUTTON);       // the previous reading from the input pin
  
  for (byte count = 0; count < BUTTON_DEBOUNCE_DELAY; count++) {
    if (digitalRead(BUTTON) == lastState) return false;
    delay(1);
  }
  
  lastState = !lastState;
  return lastState == HIGH ? false : true;
}

void handle_root() {
  Serial.print("New Client with IP: ");
  Serial.println(server.client().remoteIP().toString());
  server.send(200, "text/html", SendHTML(system_switch));
}

void handle_ledon() {
  system_switch = HIGH;
  Serial.println("Led ON");
  server.send(200, "text/html", SendHTML(system_switch));
}

void handle_ledoff() {
  system_switch = LOW;
  Serial.println("Led OFF");
  server.send(200, "text/html", SendHTML(system_switch));
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}
String SendHTML(uint8_t ledstat){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta http-equiv=\"refresh\" content=\"3;URL=/\" name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>Home Monitoring system</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +=".button {display: block;width: 80px;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #1abc9c;}\n";
  ptr +=".button-on:active {background-color: #16a085;}\n";
  ptr +=".button-off {background-color: #ff4133;}\n";
  ptr +=".button-off:active {background-color: #d00000;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<h1>Home Monitoring system </h1>\n";
  
   if(ledstat)
  {
    ptr +="<p>Home Monitoring is ON</p><a class=\"button button-off\" href=\"/OFF\">OFF</a>\n";
    ptr +="<h3>Temperature: " + (String) t + "&deg\;C </h3>\n";
    ptr +="<h3>Humidity: " + (String) h + "% </h3>\n";
    ptr +="<a href=\"http://192.168.1.111:1880/ui/#!/2?socketid=x-oIo5ZeVQKpxA39AABK\">More</a>";
    
  }
  else
  {ptr +="<p>Home Monitoring is OFF</p><a class=\"button button-on\" href=\"/ON\">ON</a>\n";}

  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

//===============================SETUP=====================
void setup()
{
  Serial.begin(115200);
  dht.begin();
  WiFi.mode(WIFI_STA);

  servo.attach(SERVO_PIN, SERVO_PWM_MIN, SERVO_PWM_MAX);
  servo.write(50);

  pinMode(LED_ONBOARD, OUTPUT);
  digitalWrite(LED_ONBOARD, HIGH);

  pinMode(PowerLED, OUTPUT);
  digitalWrite(PowerLED, HIGH);

  pinMode(BUTTON, INPUT_PULLUP);

  pinMode(TILT, INPUT_PULLUP);

  checklcd();
  settime();

  server.on("/", handle_root);
  server.on("/ON", handle_ledon);
  server.on("/OFF", handle_ledoff);
  server.onNotFound(handle_NotFound);

  server.begin();
  Serial.println("HTTP server started");

  client.begin(mqtt_broker,1883, net);
  client.onMessage(messageReceived);

  Serial.println("\n\nSetup completed.\n\n");
}

//=================================================================LOOP=================================================================
void loop()
{
  tempo = millis();
  
  //=======================Monitoring ON======================
  if (system_switch == HIGH) {
    lcd.clear();
    h = dht.readHumidity();     // humidity percentage, range 20-80% (±5% accuracy)
    t = dht.readTemperature();  // temperature Celsius, range 0-50°C (±2°C accuracy)

    if (isnan(h) || isnan(t)) {  // readings failed, skip
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }
    // compute heat index in Celsius (isFahreheit = false)

    Serial.print(F("Humidity: "));
    Serial.print(h);
    Serial.print(F("%  Temperature: "));
    Serial.print(t);
    client.publish("temp", String((int)t).c_str());

    lcd.setCursor(0, 1);
    lcd.print("T: ");
    lcd.print(int(t));
    lcd.print("C");
    lcd.print(" H: ");
    lcd.print(int(h));
    lcd.print("%");

    lcd.home();
    time_t now = time(nullptr);
    
    if (now > 1580000000){
      lcd.print(ctime(&now));
    }
    else{
      lcd.print("Waiting for time");
    }

    Serial.println("light: ");

    int static init_db = 0;
    bool static alarm = HIGH;

    long rssi = connectToWiFi(); // WiFi connect if not established and if connected get wifi signal strenght
    lcd.print("rssi:");
    lcd.print((int)rssi);

    byte tiltVal = digitalRead(TILT);
    Serial.print("tilt value");
    Serial.print(tiltVal);
    lcd.print(" ");
    lcd.print(tiltVal);
    //publish to jsontopic for node red
    const int capacity = JSON_OBJECT_SIZE(4); 
    StaticJsonDocument<capacity> doc;
    doc["wifirssi"] = WiFi.RSSI();
    doc["temp"] = t;
    doc["humid"] = h;
    doc["tilt"] = tiltVal;
    char buffer[128];
    size_t n = serializeJson(doc, buffer);
    Serial.print("JSON message: ");
    Serial.println(buffer);
    client.publish("jsontopic", buffer, n);    

    //=============================MONITORING CORE======================
    if (((t > 28) || (t < 10) || (h < 20) || (h > 80) || (MQTTlight) > 1000) || (tiltVal == 1) && (alarm)) {  
      alarm = LOW; // led on
      digitalWrite(LED_ONBOARD, alarm);
    }
    else{ 
      alarm = HIGH;//  led off
      digitalWrite(LED_ONBOARD, alarm);
    }
    //==========================message on lcd======================
    if (MQTTlight > 1000){ //light value only for test
      lcd.home();
      lcd.print("high light      ");
    }
    if(t>28){
      lcd.home();
      lcd.print("high temperature");
    }
    if(t<10){
      lcd.home();
      lcd.print("low temperature");
    }
    if(tiltVal==1){
      lcd.home();
      lcd.print("overturn error!!");
    }

    check_influxdb();

    if (init_db == 0) { // Set tags
      pointDevice.addTag("device", "ESP8266");
      pointDevice.addTag("SSID", WiFi.SSID());
      init_db = 1;
    }

    WriteMultiToDB(ssid, (int)rssi, alarm, (float)t, (int)h, (int)lightSensorValue, (int)tiltVal); //write on MySQL table if connection works
    
  }

  //=======================monitoring system OFF===================
  if (system_switch == LOW) {
    lcd.clear();
    lcd.home();
    lcd.print("Monitoring OFF");
    Serial.print("Monitoring OFF");
    digitalWrite(LED_ONBOARD, HIGH);//turn off alarm
  }


  for (int count = 0; count < 10000; count++) {
    server.handleClient(); // listening for clients on port 80
    digitalWrite(PowerLED, !system_switch);
    //============================button controll==========================
    if (isButtonPressed() == true) {   // button pressed
      system_switch = !system_switch;
    }

      if (!client.connected()) {
    connect_to_mqtt();
  }
  client.loop(); //MQTT client loop
  
    if(MQTTlight != 0 && slave1){
      slave1=false;
      Serial.println("inizializza DB per slave data comming");
      pointSlave.addTag("device", "slave");//aggiunge tag quando è connesso slave. 
    }
    if(MQTTlight != 0){
      Serial.println("save MQTTlight frome slave node ...");
      WriteMQTTlight(MQTTlight);
      MQTTlight = 0;
      
    }
    if (millis() > Bot_lasttime + Bot_mtbs)  {
      bot.getUpdates(bot.message[0][1]);   // launch API GetUpdates up to xxx message
      Bot_ExecMessages();   // reply to message with Echo
      Bot_lasttime = millis();
    }
  
    //every 60 sec check weather anche cotrol window
    if(millis() % 5000 == 0){
      printCurrentWeather();//check weather
      
      if(weather == "Rain"){
        servo.write(50);//close
        Serial.println("close by weather");
      }
    }   
    delay(1);
  }
  
}

void connect_to_mqtt() {

  Serial.print("\nconnecting to MQTT broker...");
  while (!client.connect("master", "mqtt", "2MFfprU2W")) {
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

  MQTTlight = doc["light"];
  Serial.println(MQTTlight);
}

int WriteMQTTlight(int MQTTli) {

  // Store measured value into point
  pointSlave.clearFields();
  
  pointSlave.addField("light", MQTTli);
  Serial.print("Writing: ");
  Serial.println(pointSlave.toLineProtocol());
  if (!client_slave.writePoint(pointSlave)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client_slave.getLastErrorMessage());
  }

}

void Bot_ExecMessages() {
 
  for (int i = 1; i < bot.message[0][0].toInt() + 1; i++)      {
     
    String message_rvd = bot.message[i][5];
    Serial.println(message_rvd);
    if (message_rvd.equals("/weather")) {
      printCurrentWeather();
      bot.sendMessage(bot.message[i][4], "The weather of Milan is: " + weather, "");
    }
    if (message_rvd.equals("/openwin")) {
      servo.write(0);
      bot.sendMessage(bot.message[i][4], "opened window", "");
    }
    if (message_rvd.equals("/closewin")) {
      servo.write(50);
      bot.sendMessage(bot.message[i][4], "closed window", "");
    }
    if (message_rvd.equals("/start")) {
      
      String wellcome = "Wellcome from Monitoring bot, ESP8266 based sysem at your home";
      //String wellcome = "Wellcome Home";
      String wellcome1 = "/openwin : open the window";
      String wellcome2 = "/closewin : close the window";
      String wellcome3 = "/weather : to get current weather";
      bot.sendMessage(bot.message[i][4], wellcome, "");
      bot.sendMessage(bot.message[i][4], wellcome1, "");
      bot.sendMessage(bot.message[i][4], wellcome2, "");
      bot.sendMessage(bot.message[i][4], wellcome3, "");
      Start = true;
    }
  }
  bot.message[0][0] = "";   // All messages have been replied - reset new messages
}

void printCurrentWeather() {
  // Current weather api documentation at: https://openweathermap.org/current
  Serial.println(F("\n=== Current weather ==="));

  // call API for current weather
  if (net.connect(weather_server, 80)) {
    char request[100];
    sprintf(request, weather_query, WEATHER_CITY, WEATHER_COUNTRY, WEATHER_API_KEY);
    net.println(request);
    net.println(F("Host: api.openweathermap.org"));
    net.println(F("User-Agent: ArduinoWiFi/1.1"));
    net.println(F("Connection: close"));
    net.println();
  } 
  else {
    Serial.println(F("Connection to api.openweathermap.org failed!\n"));
  }

  while(net.connected() && !net.available()) delay(1);  // wait for data
  String result;
  while (net.connected() || net.available()) {   // read data
    char c = net.read();
    result = result + c;
  }
  
  net.stop();  // end communication
  //Serial.println(result);  // print JSON

  char jsonArray [result.length() + 1];
  result.toCharArray(jsonArray, sizeof(jsonArray));
  jsonArray[result.length() + 1] = '\0';
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, jsonArray);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  Serial.print(F("Weather: "));
  Serial.println(doc["weather"][0]["main"].as<String>());
  weather = (doc["weather"][0]["main"].as<String>());
}
