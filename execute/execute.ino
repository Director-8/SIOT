#include <WiFiNINA.h>
#include <Arduino_JSON.h>
#include <RTCZero.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>  // Include the TimeLib library for handling time

// Wi-Fi credentials
const char* ssid = "Glide_Resident";
const char* password = "RobeMostShaky";

// Server details
const char* server = "54.174.46.74";
const int port = 5000;
const char* endpoint = "/predict";

// LED pins
const int redLed = 7;
const int yellowLed = 8;

// RTC and NTP
RTCZero rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // NTP server, UTC offset (0 for GMT), update interval

WiFiClient client;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // LED setup
  pinMode(redLed, OUTPUT);
  pinMode(yellowLed, OUTPUT);

  // Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected!");

  // Initialize the RTC and sync with NTP
  rtc.begin();
  syncRTCWithNTP();
}

// Main loop to check the schedule
void loop() {
  checkSchedule();
  delay(60000); // Check every minute
}

// Function to sync RTC with NTP server
void syncRTCWithNTP() {
  Serial.print("Syncing with NTP server...");
  timeClient.begin();
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  Serial.println("NTP sync successful!");

  // Get current time in UTC
  unsigned long epochTime = timeClient.getEpochTime();

  // Break down epoch time into year, month, day, hour, minute, second
  tmElements_t timeInfo;
  breakTime(epochTime, timeInfo);

  // Set RTC to current time
  rtc.setTime(timeInfo.Hour, timeInfo.Minute, timeInfo.Second);
  rtc.setDate(timeInfo.Day, timeInfo.Month, timeInfo.Year + 1970);

  Serial.print("RTC set to: ");
  Serial.print(timeInfo.Day);
  Serial.print("/");
  Serial.print(timeInfo.Month);
  Serial.print("/");
  Serial.print(timeInfo.Year + 1970);
  Serial.print(" ");
  Serial.print(timeInfo.Hour);
  Serial.print(":");
  Serial.print(timeInfo.Minute);
  Serial.print(":");
  Serial.println(timeInfo.Second);
}

// Function to check if it's time to run the prediction (5:58 AM to 6:02 AM and 1:58 PM to 2:02 PM UK time)
void checkSchedule() {
  int hour = rtc.getHours();
  int minute = rtc.getMinutes();

  if ((hour == 19 && minute >= 00) || (hour == 19 && minute <= 03) || 
      (hour == 13 && minute >= 58) || (hour == 14 && minute <= 2)) {
    makePredictionRequest();
    delay(60000); // Avoid multiple requests within the same minute
  }
}

// Function to make the HTTP GET request and control the LEDs
void makePredictionRequest() {
  if (client.connect(server, port)) {
    Serial.println("Connected to server. Requesting prediction...");

    // Send HTTP GET request
    client.print(String("GET ") + endpoint + " HTTP/1.1\r\n" +
                 "Host: " + server + ":" + String(port) + "\r\n" +
                 "Connection: close\r\n\r\n");

    // Wait for response and read it character by character
    String response = "";
    while (client.connected()) {
      while (client.available()) {
        char c = client.read();
        response += c;
      }
    }

    // Extract the JSON part of the response
    int jsonStart = response.indexOf('{');
    int jsonEnd = response.lastIndexOf('}');

    if (jsonStart != -1 && jsonEnd != -1) {
      String jsonString = response.substring(jsonStart, jsonEnd + 1);

      // Parse the JSON response
      JSONVar result = JSON.parse(jsonString);
      if (JSON.typeof(result) == "undefined") {
        Serial.println("Parsing failed!");
      } else {
        int prediction = int(result["prediction"]);
        Serial.print("Prediction: ");
        Serial.println(prediction == 1 ? "Raining" : "Not Raining");

        // Control LEDs based on the prediction and keep the LED on until reset
        if (prediction == 1) {
          digitalWrite(redLed, HIGH);
          digitalWrite(yellowLed, LOW);
        } else {
          digitalWrite(redLed, LOW);
          digitalWrite(yellowLed, HIGH);
        }

        // Stop further execution until the reset button is pressed
        while (true) {
          delay(1000); // Keep looping indefinitely
        }
      }
    } else {
      Serial.println("JSON not found in the response.");
    }

    client.stop();
  } else {
    Serial.println("Connection to server failed!");
  }
}