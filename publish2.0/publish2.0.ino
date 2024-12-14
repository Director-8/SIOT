#include <WiFi.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <BH1745.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10
#define SEALEVELPRESSURE_HPA (1013.25)

BH1745 bh = BH1745();
Adafruit_BME280 bme;

// for ESP32 Change the SDA and SCL
#ifdef ESP32
#define SDA 21
#define SCL 22
#endif

#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

// WiFi AP SSID
#define WIFI_SSID "Glide_Resident"
// WiFi password
#define WIFI_PASSWORD "RobeMostShaky"
//influx
#define INFLUXDB_URL "https://eu-central-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "qHh7hD6ktnmrGa4xsHc8P_MYkgJoqu6hOCkcqwZqg4ZHdOVCTCMO8-4o51m38UN_EU3ndeDhg6ASZur2oi7-Cw=="
#define INFLUXDB_ORG "c2dff4f83fdabaaa"
#define INFLUXDB_BUCKET "Weather"

// OpenWeather API details
const char* apiEndpoint = "api.openweathermap.org";
const char* apiPath = "/data/2.5/weather?lat=51.4921&lon=-0.1928&units=metric&lang=en&appid=58c1e789007de54f01b89620a806e6ad";

// Time zone info
#define TZ_INFO "UTC0"
// #define Sensor_REFRESH_TIME 5000   // 5 seconds
// #define INFLUXDB_SEND_TIME 120000   // 2 minute
// #define SENSOR_BUFFER_SIZE INFLUXDB_SEND_TIME/Sensor_REFRESH_TIME


// Declare InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Declare Data point
Point sensor("sensor_data");

int ifrain = 0;
int visibility = 0;
int clouds = 0;
float rain = 0.0;
String dateTime = "";
int R; 
int G;
int B;
int C;
float Temp;
float Pressure;
float Humidity;
// static uint8_t temp_buffer[SENSOR_BUFFER_SIZE] = { 0 };
// static uint8_t humidity_buffer[SENSOR_BUFFER_SIZE] = { 0 };
// static uint16_t sensor_buffer_idx = 0;

// WiFi client for HTTP requests
WiFiClient Client1;

void setup() {
  Serial.begin(115200);
  // while (!Serial) {
  //   ; // Wait for serial port to connect
  // }
  wifisetup();
  delay(1000);
  SensorSetup();
  delay(1000);
  Influex_Init();
  delay(1000);
}

void loop() {
  fetchWeatherData();
  delay (10000);
  SensorRead();
  Influx_Push();
  Serial.println("Waiting 2 minutes");
  delay(120000);
}

void wifisetup() {
  // Setup wifi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to wifi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void SensorSetup() {
  unsigned status = bme.begin();
  if (!status) {
    Serial.println("BME280 Device Error");
    while (1) delay(10);
  }

  #ifdef ESP32
    bool result = bh.begin(SDA, SCL);
  #else
    bool result = bh.begin();
  #endif
  if (!result) {
    Serial.println("BH1745 Device Error");
    while (1) { delay(10); }
  }
  bh.setGain(bh.GAIN_16X);
  bh.setRgbcMode(bh.RGBC_8_BIT);
}

void Influex_Init() {
  // Add tags to the data point
  sensor.addTag("device", DEVICE);
  // Accurate time is necessary for certificate validation and writing in batches
  // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial.
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
  // Check server connection
  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}

void fetchWeatherData() {
  Serial.println("Connecting to OpenWeather API...");
  if (Client1.connect(apiEndpoint, 80)) {
    Serial.println("Connected to OpenWeather API.");
    Client1.print(String("GET ") + apiPath + " HTTP/1.1\r\n" +
                 "Host: " + apiEndpoint + "\r\n" +
                 "Connection: close\r\n\r\n");
                
    while (Client1.connected() && !Client1.available()) delay(10);

    String response = "";
    while (Client1.available()) {
      response += Client1.readString();
    }

    delay(5000);

    int jsonStart = response.indexOf("\r\n\r\n");
    if (jsonStart == -1) {
      Serial.println("Invalid HTTP response: JSON not found.");
      return;
    }

    String json = response.substring(jsonStart + 4);
    delay(5000);

    Serial.println("Extracted JSON:");
    Serial.println(json);
    delay(5000);

    parseWeatherData(json);
  } else {
    Serial.println("Failed to connect to OpenWeather API.");
  }
  Client1.stop();
}

void parseWeatherData(String json) {
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print("JSON deserialization failed: ");
    Serial.println(error.c_str());
    return;
  }

  ifrain = (strcmp(doc["weather"][0]["description"], "Rain") == 0) ? 1 : 0;
  visibility = doc["visibility"] | 0;
  clouds = doc["clouds"]["all"] | 0;
  rain = doc["rain"]["1h"] | 0.0;
  dateTime = convertUnixTimestampToDateTime(doc["dt"]);

  delay(5000);

  Serial.print("Weather: ");
  Serial.println(ifrain);

  Serial.print("Visibility: ");
  Serial.print(visibility);
  Serial.println(" meters");

  Serial.print("Cloudiness: ");
  Serial.print(clouds);
  Serial.println(" %");

  Serial.print("Rain: ");
  Serial.print(rain);
  Serial.println(" mm");

  Serial.print("Date: ");
  Serial.println(dateTime);
  delay(5000);
}

String convertUnixTimestampToDateTime(unsigned long unixTime) {
  time_t t = unixTime;
  return String(day(t)) + "/" + String(month(t)) + "/" + String(year(t)) + " " +
         String(hour(t)) + ":" + String(minute(t)) + ":" + String(second(t)) + " GMT";
}

void SensorRead() {
  // Read BH1745 sensor values
  bh.read();
  R = bh.red, G = bh.green, B = bh.blue, C = bh.clear;
  // Read BME280 sensor values
  Temp = bme.readTemperature();
  Pressure = bme.readPressure() / 100.0;
  Humidity = bme.readHumidity();
  //print to serial
  Serial.print("Red: "); Serial.println(R);
  Serial.print("Green: "); Serial.print(G);
  Serial.print("Blue: "); Serial.print(B); 
  Serial.print("Clear: "); Serial.print(C); 
  Serial.print("Temperature: "); Serial.println(Temp);
  Serial.print("Pressure: "); Serial.println(Pressure);
  Serial.print("Humidity: "); Serial.println(Humidity);
}

void Influx_Push() {
  // Clear fields for reusing the point. Tags will remain the same as set above.
  sensor.clearFields();

  // Store measured value into point
  // Report RSSI of currently connected network
  sensor.addField("rssi", WiFi.RSSI());
  // add temperature and humidity values also
  sensor.addField( "temperature", Temp );
  sensor.addField( "pressure", Pressure );
  sensor.addField( "red", R );
  sensor.addField( "green", G );
  sensor.addField( "blue", B );
  sensor.addField( "luminosity", C );
  sensor.addField( "weather", ifrain );
  sensor.addField( "visibility", visibility );
  sensor.addField( "clouds", clouds );
  sensor.addField( "rainfall", rain );
  sensor.addField( "humidity", Humidity );
  // sensor.addField( "time", dateTime );
  // Print what are we exactly writing
  Serial.print("Writing: ");
  Serial.println(sensor.toLineProtocol());

  // Check WiFi connection and reconnect if needed
  if (wifiMulti.run() != WL_CONNECTED) {
    Serial.println("Wifi connection lost");
  }

  // Write point
  if (!client.writePoint(sensor)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}