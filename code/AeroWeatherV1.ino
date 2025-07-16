#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <sys/time.h>
#include "creds.h"
#include <ESP_Google_Sheet_Client.h>
#include "Adafruit_VEML7700.h"
#include <Adafruit_BME280.h>
#include "DEV_Config.h"
#include "LTR390.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>

// Timer variables
unsigned long lastTime = 0;  
unsigned long timerDelay = 60000; // 10000 for testing, 60000 for finished design

// Token Callback function
void tokenStatusCallback(TokenInfo info);

Adafruit_VEML7700 veml = Adafruit_VEML7700();

Adafruit_BME280 bme; // I2C
struct BMEData {
  float temperature;
  float pressure;
  float humidity;
};

UDOUBLE UV,ALS;

// NTP server to request epoch time
const char* ntpServer = "pool.ntp.org";

// Variable to save current epoch time
unsigned long epochTime; 
struct tm timeInfo;

// Function that gets current epoch time
time_t getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeInfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

void setup() {
  // initialize digital pin led as an output
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  //Configure time
  configTime(0, 0, ntpServer);

  setenv("TZ", "CST6CDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();  // Apply the new TZ setting

  GSheet.printf("ESP Google Sheet Client v%s\n\n", ESP_GOOGLE_SHEET_CLIENT_VERSION);

  pinMode(A0, INPUT);
  pinMode(A1, INPUT);

  // Connect to Wi-Fi
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(1000);              
    }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Set the callback for Google API access token generation status (for debug only)
  GSheet.setTokenCallback(tokenStatusCallback);

  // Set the seconds to refresh the auth token before expire (60 to 3540, default is 300 seconds)
  GSheet.setPrerefreshSeconds(10 * 60);

  // Begin the access token generation for Google API authentication
  GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

  //---------------------vemlSetup--------------------
  if (!veml.begin()) {
    Serial.println("Sensor not found");
    while (1);
  }
  Serial.println("Sensor found");
  veml.setLowThreshold(10000);
  veml.setHighThreshold(20000);
  veml.interruptEnable(true);

  //---------------------BMESetup-------------------
  unsigned status;
  status = bme.begin(0x76); 
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF   ); 
  if (!status) {
        Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
        Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
        Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
        Serial.print("        ID of 0x60 represents a BME 280.\n");
        Serial.print("        ID of 0x61 represents a BME 680.\n");
        while (1) delay(10);
    }

  //------------------UVSetup----------------------
  if(LTR390_Init() != 0){
      Serial.print("init err!!!");
      while(1);
  }
  LTR390_SetIntVal(5, 20);

}

//----------------------LUX LEVEL-----------------
float getLuxLevel(){
  float lux = veml.readLux();
  uint16_t irq = veml.interruptStatus();
  if (irq & VEML7700_INTERRUPT_LOW) {
    Serial.println("** Low threshold");
  }
  if (irq & VEML7700_INTERRUPT_HIGH) {
    Serial.println("** High threshold");
  }
  delay(500);
  return lux;
}

//--------------------Temperature, Pressure, Humidity----------
BMEData getBMEData() {
  BMEData data;
  bme.takeForcedMeasurement();
  data.temperature = bme.readTemperature();         // °C
  data.pressure    = bme.readPressure()/ 100.0F;   // hPa
  data.humidity    = bme.readHumidity();            // %
  return data;
}

float getUVLevel() {
  float UVLevel = LTR390_UVS();
  return UVLevel;
}

void loop() {
// Call ready() repeatedly in loop for authentication checking and processing
    bool ready = GSheet.ready();

    if (ready && millis() - lastTime > timerDelay){
        lastTime = millis();

        FirebaseJson response;
        FirebaseJson valueRange;

        float batteryVoltage = checkBatteryVoltage();
        float solarVoltage = checkSolarVoltage();
        float lux = getLuxLevel();
        BMEData BMEvalues = getBMEData();
        float UV = getUVLevel();

        Serial.print("Lux = ");
  Serial.println(lux);

  Serial.print("Temperature C = ");
  Serial.print(BMEvalues.temperature);
  Serial.println(" °C");

  Serial.print("Temperature F = ");
  Serial.print(BMEvalues.temperature*1.8+32);
  Serial.println(" °F");

  Serial.print("Pressure = ");
  Serial.print(BMEvalues.pressure);
  Serial.println(" hPa");

  
  Serial.print("Pressure = ");
  Serial.print(BMEvalues.pressure*0.02952998057228);
  Serial.println(" inHg");

  Serial.print("Humidity = ");
  Serial.print(BMEvalues.humidity);
  Serial.println(" %");

  Serial.print("UV Level: "); 
  Serial.println(UV);

        
        // Get timestamp
        time_t epochTime = getTime();

        localtime_r(&epochTime, &timeInfo);        // or localtime_r(...) for local TZ
        Serial.printf("%04d-%02d-%02d  %02d:%02d:%02d UTC\n",
                timeInfo.tm_year + 1900,
                timeInfo.tm_mon  + 1,
                timeInfo.tm_mday,
                timeInfo.tm_hour,
                timeInfo.tm_min,
                timeInfo.tm_sec);
        

        valueRange.add("majorDimension", "COLUMNS");
        valueRange.set("values/[0]/[0]", epochTime);
        valueRange.set("values/[1]/[0]", batteryVoltage);
        valueRange.set("values/[2]/[0]", solarVoltage);
        valueRange.set("values/[3]/[0]", lux);
        valueRange.set("values/[4]/[0]", BMEvalues.temperature*1.8+32);
        valueRange.set("values/[5]/[0]", BMEvalues.pressure*0.02952998057228);
        valueRange.set("values/[6]/[0]", BMEvalues.humidity);
        valueRange.set("values/[7]/[0]", UV);

        // For Google Sheet API ref doc, go to https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/append
        // Append values to the spreadsheet
        bool success = GSheet.values.append(&response /* returned response */, spreadsheetId /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
        if (success){
            response.toString(Serial, true);
            valueRange.clear();
        }
        else{
            Serial.println(GSheet.errorReason());
        }
        Serial.println();
        Serial.println(ESP.getFreeHeap());
    }
}

float checkBatteryVoltage(){
  float Vbatt = 0;
  for(int i = 0; i < 16; i++) {
    Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction   
  }
  float Vbattf = 2 * Vbatt / 16 / 1000.0;     // attenuation ratio 1/2, mV --> V
  Serial.print("Battery Voltage: ");
  Serial.println(Vbattf, 2);
  
  return Vbattf;
}

float checkSolarVoltage(){
  float Vsolar = 0;
  for(int i = 0; i < 16; i++){
    Vsolar = Vsolar + analogReadMilliVolts(A1);
  }
  float Vsolarf = 4 * Vsolar / 16 / 1000.0;
  Serial.print("Solar Voltage: ");
  Serial.println(Vsolarf, 2);

  return Vsolarf;
}

void tokenStatusCallback(TokenInfo info){
    if (info.status == token_status_error){
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
        GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
    }
    else{
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
    }
}