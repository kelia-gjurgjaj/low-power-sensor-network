/*********
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Modified from the examples of the Arduino LoRa library
  More resources: https://RandomNerdTutorials.com/esp32-lora-rfm95-transceiver-arduino-ide/
*********/

#include <SPI.h>
#include <LoRa.h>
// WiFi / Google Sheets
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>

//define the pins used by the transceiver module
#define ss 5
#define rst 26
#define dio0 27

#define SHARP_SCK   14  // CLK
#define SHARP_MOSI  13  // MOSI
#define SHARP_SS    15  // CS
#define SHARP_DISP  25  // Display enable pin
#define SHARP_VCOM  4   // VCOM toggle pin
#define BLACK 0
#define WHITE 1

Adafruit_SharpMem display(SHARP_SCK, SHARP_MOSI, SHARP_SS, 400, 240);

// WiFi Stuff below

// Replace with your network credentials
const char* ssid = "Starlink";
const char* password = "*********";
const char* serverAddress = "*******";
const int serverPort = 80;

// Google Apps Script Web App URL
const char* serverName = "https://script.google.com/macros/s/AKfycbywnSWLs8_LqM3vRi_-mjHhK1A9MMaT3hCivDwBCKAHPst_-MhH9RBAlrO_p4jzx8mIag/exec";

// Initialize WiFi and NTP client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000); // Update every 60 seconds

void setup() {
  //initialize Serial Monitor
  Serial.begin(115200);
  while (!Serial);
  Serial.println("LoRa Receiver");

  //setup LoRa transceiver module
  LoRa.setPins(ss, rst, dio0);
  
  //replace the LoRa.begin(---E-) argument with your location's frequency 
  //433E6 for Asia
  //868E6 for Europe
  //915E6 for North America
  while (!LoRa.begin(915E6)) {
    Serial.println(".");
    delay(500);

  pinMode(SHARP_DISP, OUTPUT);
  digitalWrite(SHARP_DISP, HIGH);
  pinMode(SHARP_VCOM, OUTPUT);

  display.begin();
  display.clearDisplay();
  display.refresh();  
  }
   // Change sync word (0xF3) to match the receiver
  // The sync word assures you don't get LoRa messages from other LoRa transceivers
  // ranges from 0-0xFF
  LoRa.setTxPower(20); //CHANGE THIS DURING TESTING ***************************************************************
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa Initializing OK!");

  // Setup WiFi connectionl;'
  ///////5
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  // Initialize NTP client
  timeClient.begin();
  timeClient.update();
}

void loop() {
  // Toggle EXTCOMIN for Sharp display every second
  static unsigned long lastVCOM = 0;
  static bool vcomState = false;
  if (millis() - lastVCOM >= 1000) {
    vcomState = !vcomState;
    digitalWrite(SHARP_VCOM, vcomState);
    lastVCOM = millis();
  }

      // Display on Sharp Memory LCD
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(BLACK);
    display.setCursor(0, 0);

    display.print("Starting... ");
        display.refresh();

  // Check for LoRa packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // Read LoRa message into input
    String input = "";
    while (LoRa.available()) {
      input += (char)LoRa.read();
    }

    Serial.print("Received: ");
    Serial.println(input);
    float rssi = LoRa.packetRssi();
    Serial.print("RSSI: ");
    Serial.println(rssi);

    // Parse values
    int num = input.substring(input.indexOf("#") + 1, input.indexOf(" ID:")).toInt();
    String id = input.substring(input.indexOf("ID:") + 3, input.indexOf(" X:"));
    float windSpeed = input.substring(input.indexOf("X:") + 2, input.indexOf(" Y:")).toFloat();
    float windSpeedKm = input.substring(input.indexOf("Y:") + 2, input.indexOf(" Z:")).toFloat();
    float windSpeedMph = input.substring(input.indexOf("Z:") + 2, input.indexOf(" T:")).toFloat();
    float temperature = input.substring(input.indexOf("T:") + 2, input.indexOf(" P:")).toFloat();
    float pressure = input.substring(input.indexOf("P:") + 2, input.indexOf(" H:")).toFloat();
    float humidity = input.substring(input.indexOf("H:") + 2, input.indexOf(" G:")).toFloat();
    float gas = input.substring(input.indexOf("G:") + 2).toFloat();

    // Display on Sharp Memory LCD
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(BLACK);
    display.setCursor(0, 0);

    display.print("Temp: ");
    display.print(temperature, 1);
    display.println(" C");

    display.print("Hum:  ");
    display.print(humidity, 1);
    display.println(" %");

    display.print("Gas:  ");
    display.print(gas, 1);
    display.println(" kOhm");

    display.print("Wind: ");
    display.print(windSpeedMph, 1);
    display.println(" mph");

    display.refresh();

    // Send to Google Sheets
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverName);
      http.addHeader("Content-Type", "application/json");

      timeClient.update();
      String formattedTime = timeClient.getFormattedTime();

      String jsonData = "{";
      jsonData += "\"method\":\"append\",";
      jsonData += "\"timestamp\":\"" + formattedTime + "\",";
      jsonData += "\"count\":\"" + String(num) + "\",";
      jsonData += "\"id\":\"" + id + "\",";
      jsonData += "\"temp\":\"" + String(temperature) + "\",";
      jsonData += "\"hum\":\"" + String(humidity) + "\",";
      jsonData += "\"pres\":\"" + String(pressure) + "\",";
      jsonData += "\"gas\":\"" + String(gas) + "\",";
      jsonData += "\"wkm\":\"" + String(windSpeedKm) + "\",";
      jsonData += "\"wmps\":\"" + String(windSpeed) + "\",";
      jsonData += "\"wmph\":\"" + String(windSpeedMph) + "\",";
      jsonData += "\"rssi\":\"" + String(rssi) + "\"}";

      Serial.println("Sending JSON Data: " + jsonData);

      int httpResponseCode = http.POST(jsonData);
      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println(httpResponseCode);
        Serial.println(response);
      } else {
        Serial.println("Error on sending POST: " + String(httpResponseCode));
      }

      http.end();
    } else {
      Serial.println("WiFi Disconnected");
    }

    Serial.println("done with wifi stuff");
  }
}
