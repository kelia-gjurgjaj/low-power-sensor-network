/*
 * Project: ESP32 Anemometer + BME680 LoRa Sender (SSD1306)
 * Description: Reads analog anemometer and BME680 over I2C, computes wind
 *              speed (m/s, km/h, mph), displays values on a 128x64 OLED,
 *              and transmits a formatted packet over LoRa @ 915 MHz.
 *
 * Hardware: ESP32, RFM95/96 (SS=5, RST=26, DIO0=27), OLED SSD1306 (I2C 0x3C),
 *           BME680 (I2C on SDA=21, SCL=22)
 * LoRa:    freq=915E6 (NA), txPower=17, syncWord=0xF3
 * Packet:  "#<count> ID:<node> X:<mps> Y:<kmh> Z:<mph> T:<C> P:<hPa> H:<%> G:<kΩ>"
 *
 * Based on:
 *   - Random Nerd Tutorials: ESP32 Anemometer & ESP32 LoRa RFM95 guides
 *     https://randomnerdtutorials.com/esp32-anemometer-wind-speed-arduino/
 *     https://randomnerdtutorials.com/esp32-lora-rfm95-transceiver-arduino-ide/
 *   - Arduino LoRa library examples
 *
 * Modifications:
 *   - Combined anemometer + BME680 sensing
 *   - Voltage→wind-speed mapping with clamp
 *   - OLED status/telemetry screens
 *   - Custom LoRa packet schema (X/Y/Z/T/P/H/G)
 *
 * Author: Kelia Gjurgjaj, Ani Vardanyan, Alyssa Seims
 * Date Started: Oct 2024
 */

#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//define the pins used by the transceiver module **LORA
#define ss 5
#define rst 26
#define dio0 27


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME680 bme; // I2C

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Constants (Change the following variables if needed)
const int anemometerPin = 34;  // GPIO pin connected to anemometer (analog pin)
const float minVoltage = 0.4;  // Voltage corresponding to 0 m/s
const float maxVoltage = 2.0;  // Voltage corresponding to 32.4 m/s (max speed) (when using voltage divider)
const float maxWindSpeed = 32.4; // Maximum wind speed in m/s

// Conversion factors
const float mps_to_kmh = 3.6;   // 1 m/s = 3.6 km/h
const float mps_to_mph = 2.23694; // 1 m/s = 2.23694 mph

// LoRa Packet Sender **
int counter = 0;

void setup() {

  Wire.begin(21, 22);  // Use custom I2C pins if needed
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Starting...");
  display.display();

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  Serial.begin(115200); 
  while (!Serial);
  Serial.println("LoRa Sender");

  //setup LoRa transceiver module
  LoRa.setPins(ss, rst, dio0);


  //replace the LoRa.begin(---E-) argument with your location's frequency 
  //433E6 for Asia
  //868E6 for Europe
  //915E6 for North America
  while (!LoRa.begin(915E6)) {
    Serial.println(".");
    delay(500);
  }

  // Change sync word (0xF3) to match the receiver
  // The sync word assures you don't get LoRa messages from other LoRa transceivers
  // ranges from 0-0xFF
  LoRa.setTxPower(17);
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa Initializing OK!");

  Serial.println(F("BME680 async test"));
  if (!bme.begin()) {
    Serial.println(F("Could not find a valid BME680 sensor, check wiring!"));
    while (1);
  }

  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms 
}

void loop() {
  
  // Tell BME680 to begin measurement.
  //Serial.println("INSIDE LOOP");
  unsigned long endTime = bme.beginReading();
  if (endTime == 0) {
    Serial.println(F("Failed to begin reading :("));
    return;
  }  
  delay(50);
  
  
  // Obtain measurement results from BME680. Note that this operation isn't
  // instantaneous even if milli() >= endTime due to I2C/SPI latency.
  if (!bme.endReading()) {
    Serial.println(F("Failed to complete reading :("));
    return;
  }

  
  // Read analog value from anemometer (ADC value between 0-4095 on ESP32 for 0-3.3V)
  int adcValue = analogRead(anemometerPin);
  
  // Convert ADC value to voltage (ESP32 ADC range is 0-3.3V)
  float voltage = (adcValue / 4095.00) * 3.3;
  
  // Ensure the voltage is within the anemometer operating range
  if (voltage < minVoltage) {
    voltage = minVoltage;
  } else if (voltage > maxVoltage) {
    voltage = maxVoltage;
  }
  
  // Map the voltage to wind speed
  float windSpeed_mps = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * maxWindSpeed;

  // Convert wind speed to km/h and mph
  float windSpeed_kmh = windSpeed_mps * mps_to_kmh;
  float windSpeed_mph = windSpeed_mps * mps_to_mph;

  // Print wind speed
  Serial.print(" X:");
  Serial.print(windSpeed_mps);
  Serial.print(" Y:");
  Serial.print(windSpeed_kmh);
  Serial.print(" Z:");
  Serial.print(windSpeed_mph);
  
  delay(1000);
  
  Serial.print(" T:");
  Serial.print(bme.temperature);

  Serial.print(" P:");
  Serial.print(bme.pressure/100.0);

  Serial.print(" H:");
  Serial.print(bme.humidity);

  Serial.print(" G:");
  Serial.print(bme.gas_resistance / 1000.0);
/*
  Serial.print("Approx. Altitude = ");
  Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
  Serial.println(" m");
  */
  Serial.println();
  delay(1000);
  Serial.print("Sending packet: ");
  Serial.println(counter);

  // --- Show "Sending..." on screen ---
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Sending packet...");
  display.println(counter);
  display.display();
  delay(800);  // show it for half a second
  //display.clearDisplay();
  
  //Send LoRa packet to receiver
  LoRa.beginPacket();
  LoRa.print("#");
  LoRa.print(counter);
  // Print wind speed and environmental data
  LoRa.print(" ID:B X:" + String(windSpeed_mps) 
             + " Y:" + String(windSpeed_kmh) 
             + " Z:" + String(windSpeed_mph) 
             + " T:" + String(bme.temperature)
             + " P:" + String(bme.pressure / 100.0)
             + " H:" + String(bme.humidity)
             + " G:" + String(bme.gas_resistance / 1000.0));

  LoRa.endPacket();

  // --- Optional: change to "Sent!" then clear ---
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor((SCREEN_WIDTH - 6 * 5) / 2, 0);  // roughly center
  display.println("Sent!");
  display.display();
  delay(800); // show "Sent!" for a moment

  // Now show the selected sensor data
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.print("Temp: ");
  display.print(bme.temperature, 1);
  display.println(" C");

  display.print("Hum : ");
  display.print(bme.humidity, 1);
  display.println(" %");

  display.print("Gas : ");
  display.print(bme.gas_resistance / 1000.0, 1);
  display.println(" kOhm");

  display.print("Wind: ");
  display.print(windSpeed_mph, 1);
  display.println(" mph");
  display.display();

  counter++;
  delay(1000);
  delay(7000); // every 10 seconds
  //delay(30000);

}
