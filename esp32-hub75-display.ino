// Display things on a Hub75 display with ESP32
// uses ESP32 DevKit v1 board, and `DOIT ESP32 DEVKIT V1` package name from: https://dl.espressif.com/dl/package_esp32_index.json
// 2021-04-25 jkl

/* includes much code from: 
 *  HackerBox 0065 Clock Demo for 64x32 LED Array
 *  Adapted from SmartMatrix example file
*/

/*
 * wifi connect code and parameters file from
 * https://github.com/paulgreg/esp32-weather-station
*/

//much laziness from https://arduinojson.org/v6/assistant/

//TODO
//TODO add Hourly+1 ?
//todo add air pollution, pm2.5 (or10?) for smoke season https://openweathermap.org/api/air-pollution
//todo add covid (see parameters.h)
//todo add rain chance - `pop` field (probability of precipitation)
//todo use NTP?
//TODO add retries to get data and/or parse data?

//FIXME RTC initializiation

#define ARDUINOJSON_USE_DOUBLE 1
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "parameters.h"

DynamicJsonDocument doc(24576);

#include "RTClib.h"
#include <MatrixHardware_ESP32_V0.h>    
#include <SmartMatrix.h>

#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 32;      // Set to the height of your display
const uint8_t kRefreshDepth = 36;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_32ROW_MOD16SCAN;   // Choose the configuration that matches your panels.  See more details in MatrixCommonHub75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki
const uint8_t kIndexedLayerOptions = (SM_INDEXED_OPTIONS_NONE);

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_INDEXED_LAYER(indexedLayer1, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kIndexedLayerOptions);
SMARTMATRIX_ALLOCATE_INDEXED_LAYER(indexedLayer2, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kIndexedLayerOptions);
SMARTMATRIX_ALLOCATE_INDEXED_LAYER(indexedLayer3, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kIndexedLayerOptions);

RTC_DS1307 rtc;
const int defaultBrightness = (35*255)/100;     // dim: 35% brightness
//char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char daysOfTheWeek[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char monthsOfTheYr[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JLY", "AUG", "SPT", "OCT", "NOV", "DEC"};

//debug/admin stuff
const bool debug = true;
const bool debugSerial = true;
const uint64_t SECOND = 1000;
const uint64_t MINUTE = 60 * SECOND;
const uint64_t HOUR = 60 * MINUTE;
const uint64_t MICRO_SEC_TO_MILLI_SEC_FACTOR = 1000;
uint64_t sleepTime = MINUTE;


void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("DS1307RTC Test");
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  // setup matrix
  matrix.addLayer(&indexedLayer1); 
  matrix.addLayer(&indexedLayer2);
  matrix.addLayer(&indexedLayer3);
  matrix.begin();

  matrix.setBrightness(defaultBrightness);

  //connect to wifi  
  connectToWifi();  
}

void loop() {
  char txtBuffer[12];
  DateTime now = rtc.now();

  // clear screen before writing new text
  indexedLayer1.fillScreen(0);
  indexedLayer2.fillScreen(0);
  indexedLayer3.fillScreen(0);

  sprintf(txtBuffer, "%02d:%02d", now.hour(), now.minute());
  indexedLayer1.setFont(font8x13);
  //indexedLayer1.setFont(font3x5);
  indexedLayer1.setIndexedColor(1,{0x00, 0x00, 0xff});
  indexedLayer1.drawString(0, 0, 1, txtBuffer);
  indexedLayer1.swapBuffers();
  indexedLayer2.setFont(font8x13);
  indexedLayer2.setIndexedColor(1,{0x00, 0xff, 0x00});
  indexedLayer2.drawString(0, 11, 1, daysOfTheWeek[now.dayOfTheWeek()]);
  indexedLayer2.swapBuffers();
  sprintf(txtBuffer, "%02d %s %04d", now.day(), monthsOfTheYr[(now.month()-1)], now.year());
  indexedLayer3.setFont(font5x7);
  indexedLayer3.setIndexedColor(1,{0xff, 0x00, 0x00});
  indexedLayer3.drawString(0, 25, 1, txtBuffer); 
  indexedLayer3.swapBuffers();

  delay(500);

  if (now.minute() % 15 == 0){ //mod15 = 0
//  if (1 == 1) {
    Serial.println("Retrieving data...");
    
    //connect to wifi
    if (WiFi.status() == WL_CONNECTED) {
      //get data
      bool dataSuccess = getData();
      
      if (debugSerial) {
        if (dataSuccess) {
          Serial.println("Succesfully got that data");
        } else {
          Serial.println("ERROR: Did not got that data");
        } 
      }
    } else {    
      if (debugSerial) {
        Serial.println("ERROR: Not connected to wifi, attempting to reconnect");
      }
      connectToWifi();
    }
  } else { 
    if (debugSerial) {
      Serial.print("NOT updating weather because it is NOT 15 minute mark, minute = ");
      Serial.println(now.minute());
    }
  }

  delay(MINUTE);
}

// functions

boolean connectToWifi() {
    
  Serial.print("\nconnecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned int retries = 50;
  while (WiFi.status() != WL_CONNECTED && (retries-- > 0)) {
    Serial.print(".");
    delay(1000);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWifi connection failed");
    return false;
  }
  Serial.println("");
  Serial.println("wifi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("DNS: ");
  Serial.println(WiFi.dnsIP(0));
  Serial.println("");
  return true;
}

boolean disconnectFromWifi() {
  WiFi.disconnect();
}

boolean getData() {
  HTTPClient http;
  
  //only for getStream
  http.useHTTP10(true);
  
  http.begin(URL); //construct the URL
  int httpCode = http.GET();  //send request
  if (debugSerial) {
    Serial.print("HTTP Response: ");
    Serial.println(httpCode);
  }
  
  //get stream
  if (httpCode > 0) {  
  
    DeserializationError error = deserializeJson(doc, http.getStream());
  
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      delay(MINUTE*5);
      return 0;
    }
  
    if (debugSerial) {
      Serial.println("Got that data");
      displayWeatherDebug();
    }
  } else {
  Serial.println("Error on HTTP request");
  return 0;
  }
  
  http.end(); 
  return 1;

}

void displayWeatherDebug() {

  float lat = doc["lat"];
  double lon = doc["lon"];
  const char* timezone = doc["timezone"];
  int timezone_offset = doc["timezone_offset"];
  
  JsonObject current = doc["current"];
  
  long current_dt = current["dt"]; // 1619381777
  long current_sunrise = current["sunrise"]; // 1619355684
  long current_sunset = current["sunset"]; // 1619406771
  float current_temp = current["temp"]; // 54.39
  float current_feels_like = current["feels_like"]; // 52.45
  int current_pressure = current["pressure"]; // 1007
  int current_humidity = current["humidity"]; // 62
  float current_dew_point = current["dew_point"]; // 41.67
  float current_uvi = current["uvi"]; // 1.37
  int current_clouds = current["clouds"]; // 90
  int current_visibility = current["visibility"]; // 10000
  float current_wind_speed = current["wind_speed"]; // 6.91
  int current_wind_deg = current["wind_deg"]; // 210
  
  JsonObject current_weather_0 = current["weather"][0];
  int current_weather_0_id = current_weather_0["id"]; // 804
  const char* current_weather_0_main = current_weather_0["main"]; // "Clouds"
  const char* current_weather_0_description = current_weather_0["description"]; // "overcast clouds"
  const char* current_weather_0_icon = current_weather_0["icon"]; // "04d"
  
  JsonArray hourly = doc["hourly"];
  
  JsonObject hourly_0 = hourly[0];
  long hourly_0_dt = hourly_0["dt"]; // 1619380800
  float hourly_0_temp = hourly_0["temp"]; // 54.39
  float hourly_0_feels_like = hourly_0["feels_like"]; // 52.45
  int hourly_0_pressure = hourly_0["pressure"]; // 1007
  int hourly_0_humidity = hourly_0["humidity"]; // 62
  float hourly_0_dew_point = hourly_0["dew_point"]; // 41.67
  float hourly_0_uvi = hourly_0["uvi"]; // 1.37
  int hourly_0_clouds = hourly_0["clouds"]; // 90
  int hourly_0_visibility = hourly_0["visibility"]; // 10000
  float hourly_0_wind_speed = hourly_0["wind_speed"]; // 6.89
  int hourly_0_wind_deg = hourly_0["wind_deg"]; // 215
  float hourly_0_wind_gust = hourly_0["wind_gust"]; // 12.64
  
  JsonObject hourly_0_weather_0 = hourly_0["weather"][0];
  int hourly_0_weather_0_id = hourly_0_weather_0["id"]; // 500
  const char* hourly_0_weather_0_main = hourly_0_weather_0["main"]; // "Rain"
  const char* hourly_0_weather_0_description = hourly_0_weather_0["description"]; // "light rain"
  const char* hourly_0_weather_0_icon = hourly_0_weather_0["icon"]; // "10d"
  
  float hourly_0_pop = hourly_0["pop"]; // 0.69
  
  float hourly_0_rain_1h = hourly_0["rain"]["1h"]; // 0.12


  Serial.print("Hourly Feels like: ");
  Serial.println(hourly_0_feels_like);
  Serial.print("Hourly High Temp: ");
  Serial.println(hourly_0_temp);
  Serial.print("Hourly Humidity: ");
  Serial.println(hourly_0_humidity);
  Serial.print("Hourly PoP: ");
  Serial.println(hourly_0_pop);
//  Serial.print("Today Min Temp: ");
//  Serial.println();
//  Serial.print("Today Max Temp: ");
//  Serial.println();
//  Serial.print("Today Humidity: ");
//  Serial.println();
//  Serial.print("Tomorrow Min Temp: ");
//  Serial.println();
//  Serial.print("Tomorrow Max Temp: ");
//  Serial.println();
//  Serial.print("Tomorrow Humidity: ");
//  Serial.println();
//  Serial.print("Updated at: ");
//  Serial.println();
}
