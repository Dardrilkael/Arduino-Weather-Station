// Autor: Lucas Fonseca e Gabriel Fonseca
// Titulo: GCIPM arduino
// Versão: 2.0.0 OTA;
//.........................................................................................................................

#include "constants.h"
#include "data.h"
#include "sd-repository.h"
#include "integration.h"
#include "sensores.h"
#include <stdio.h>
#include "esp_system.h"
#include "bt-integration.h"
#include <string>
#include <vector>
#include <rtc_wdt.h>
#include "mqtt.h"
#include "OTA.h"
#include <ArduinoJson.h>
#include "pch.h"
#include <Base64.h>
// -- WATCH-DOG
#define WDT_TIMEOUT 600000

bool processFiles(const char* dirPath, const char * todayDateString = nullptr, int amount = 1);
extern unsigned long lastPVLImpulseTime; 
extern unsigned int rainCounter;
extern unsigned long lastVVTImpulseTime;
extern float anemometerCounter;
extern int rps[20];
extern Sensors sensors;
long startTime;
long startTime5_Seconds, startTime_20m;
long startTime100_mS;
int timeRemaining=0;
std::string jsonConfig = "{}";
String formatedDateString = "";
bool isBeforeNoon=true;
struct HealthCheck healthCheck = {FIRMWARE_VERSION, 0, false, false, 0, 0};
// -- MQTT
String sysReportMqqtTopic;
//String softwareReleaseMqttTopic;
MQTT mqqtClient1;

// -- Novo
int wifiDisconnectCount=0;

void logIt(const std::string &message, bool store = false){
  Serial.print(message.c_str());
  if(store == true){
    storeLog(message.c_str());
  }
}

void watchdogRTC() {
  rtc_wdt_protect_off();      //Disable RTC WDT write protection
  rtc_wdt_disable();
  rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
  rtc_wdt_set_time(RTC_WDT_STAGE0, WDT_TIMEOUT); // timeout rtd_wdt 10000ms.
  rtc_wdt_enable();           //Start the RTC WDT timer
  rtc_wdt_protect_on();       //Enable RTC WDT write protection
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  delay(3000);
  logIt("\n >> Sistema Integrado de meteorologia << \n");

  pinMode(LED1,OUTPUT);
  pinMode(LED2,OUTPUT);
  pinMode(LED3,OUTPUT);
  digitalWrite(LED1,HIGH);
  digitalWrite(LED2,LOW);
  digitalWrite(LED3,LOW);
  
  pinMode(PLV_PIN, INPUT_PULLDOWN);
  pinMode(ANEMOMETER_PIN, INPUT_PULLUP);
  pinMode(16, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PLV_PIN), pluviometerChange, RISING);
  attachInterrupt(digitalPinToInterrupt(ANEMOMETER_PIN), anemometerChange, FALLING);

  logIt("\nIniciando cartão SD");
  initSdCard();
  
  logIt("\nCriando diretorios padrões");
  createDirectory("/metricas");
  createDirectory("/logs");

  OnDebug(Serial.printf("\n - Carregando variáveis de ambiente");)
  logIt("\n1. Estação iniciada;", true);

  bool loadedSD = loadConfiguration(SD, configFileName, config, jsonConfig);
  const char* bluetoothName = nullptr;
  if(loadedSD) bluetoothName=config.station_name;
  else bluetoothName = "est000";

  logIt("\n1.1 Iniciando bluetooth;", true);
  BLE::Init(bluetoothName, bluetoothController);
  BLE::updateValue(CONFIGURATION_UUID, jsonConfig);

  if(!loadedSD)
    while (!loadConfiguration(SD, configFileName, config, jsonConfig));
    
  logIt("\n1.2 Estabelecendo conexão com wifi ", true);
  setupWifi("  - Wifi", config.wifi_ssid, config.wifi_password);
  int nivelDbm = (WiFi.RSSI()) * -1;
  storeLog((String(nivelDbm) + ";").c_str());

  logIt("\n1.3 Estabelecendo conexão com NTP;", true);
  connectNtp("  - NTP");

  logIt("\n1.4 Estabelecendo conexão com MQTT;", true);
  mqqtClient1.setupMqtt("  - MQTT", config.mqtt_server, config.mqtt_port, config.mqtt_username, config.mqtt_password, config.mqtt_topic);
  //softwareReleaseMqttTopic = String("software-release/") + String(config.mqtt_topic);

  //mqqtClient2.setupMqtt("- MQTT2", config.mqtt_hostV2_server, config.mqtt_hostV2_port, config.mqtt_hostV2_username, config.mqtt_hostV2_password, softwareReleaseMqttTopic.c_str());
  mqqtClient1.setCallback(mqttSubCallback);
  mqqtClient1.setBufferSize(512);
  mqqtClient1.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
  sysReportMqqtTopic =(String("sys-report")+String(config.mqtt_topic));

  logIt("\n\n1.5 Iniciando controllers;", true);
  setupSensors();

  int now = millis();
  lastVVTImpulseTime = now;
  lastPVLImpulseTime = now;

  // 2; Inicio
  OnDebug(Serial.printf("\n >> PRIMEIRA ITERAÇÃO\n");)

  int timestamp = timeClient.getEpochTime();
  convertTimeToLocaleDate(timestamp);
  
  String dataHora = String(formatedDateString) + "T" + timeClient.getFormattedTime();
  storeLog(("\n" + dataHora + "\n").c_str());

  // -- WATCH-DOG
  watchdogRTC();

  for(int i=0; i<7; i++) {
    digitalWrite(LED1,i%2);
    delay(400);
  }
  
  char jsonPayload[100];
  sprintf(jsonPayload, "{\"version\":\"%s\",\"timestamp\":%lu}", FIRMWARE_VERSION, timestamp);
  mqqtClient1.publish((sysReportMqqtTopic + String("/handshake")).c_str(), jsonPayload, 1);
  
  startTime = millis();
  startTime5_Seconds=startTime;
  startTime100_mS=startTime;
  startTime_20m = startTime;
  //sendCSVFile("/config.txt");

}

int timestamp = 0;
void loop() {
  if (Serial.available()) {
    char input = Serial.read();
    if (input == 'r' || input == 'R') {
      Serial.println("🔄 Reiniciando dispositivo...");
      delay(1000);
      ESP.restart();
    }
  }

  digitalWrite(LED3,HIGH);
  unsigned long now = millis();
  WindGustRead(now);
  if (now - startTime100_mS >= 100){
    startTime100_mS = now;
    mqqtClient1.loopMqtt();
  }

  digitalWrite(LED1,LOW);
  digitalWrite(LED2,LOW);

   if (now-startTime_20m >= 40000){
    startTime_20m = now;
    processFiles("/falhas",formatedDateString.c_str());
   }
  if (now-startTime >= config.interval){
    startTime = now;

    digitalWrite(LED1,HIGH);
    digitalWrite(LED2,HIGH);

    rtc_wdt_feed(); // -- WATCH-DOG

    timeClient.update();
    timestamp = timeClient.getEpochTime();
    convertTimeToLocaleDate(timestamp);
    
    // Computando dados
  
    OnDebug(Serial.printf("\n\n Computando dados ...\n");)

    Data.timestamp = timestamp;
    Data.wind_dir = getWindDir();
    Data.rain_acc = rainCounter * VOLUME_PLUVIOMETRO;
    Data.wind_gust  = 3.052f /3.0f* ANEMOMETER_CIRC *findMax(rps,sizeof(rps)/sizeof(int));
    Data.wind_speed = 3.052 * (ANEMOMETER_CIRC * anemometerCounter) / (config.interval / 1000.0); // m/s
  
    DHTRead(Data.humidity, Data.temperature);
    BMPRead(Data.pressure);

    // Apresentação
    parseData();
    //OnDebug(Serial.printf("\nResultado CSV:\n%s", metricsCsvOutput); )
    OnDebug(Serial.printf("\nResultado JSON:\n%s\n", metricsjsonOutput);)

    // Armazenamento local
    OnDebug(Serial.println("\n Gravando em disco:");)
    storeMeasurement("/metricas", formatedDateString, metricsCsvOutput);

    // Enviando Dados Remotamente
    OnDebug(Serial.println("\n Enviando Resultados:  ");)
    bool measurementSent1 = mqqtClient1.publish(config.mqtt_topic, metricsjsonOutput);
    if(!measurementSent1)
      storeMeasurement("/falhas", formatedDateString, metricsCsvOutput);
    // Update metrics advertsting value
    BLE::updateValue(HEALTH_CHECK_UUID, ("ME: " + String(metricsCsvOutput)).c_str());
    OnDebug(Serial.printf("\n >> PROXIMA ITERAÇÃO\n");)
    resetSensors();
  }

  now = millis();
  if (now - startTime5_Seconds >= 5000) {
    startTime5_Seconds = now;

    // Health check
    healthCheck.timestamp = timestamp;
    healthCheck.isWifiConnected = WiFi.status() == WL_CONNECTED;
    healthCheck.wifiDbmLevel = !healthCheck.isWifiConnected ? 0 : (WiFi.RSSI()) * -1;
    healthCheck.isMqttConnected = mqqtClient1.loopMqtt();
    healthCheck.timeRemaining = ((startTime + config.interval - now) / 1000);

    const char * hcCsv = parseHealthCheckData(healthCheck, 1);
  
    OnDebug(Serial.printf("\n\nColetando dados, metricas em %d segundos ...", ((startTime + config.interval - now) / 1000));)
    OnDebug(Serial.printf("\n  - %s",hcCsv);)

    //if(!healthCheck.isWifiConnected)setupWifi("  - Wifi", config.wifi_ssid, config.wifi_password);
    // Garantindo conexão com mqqt broker;
    if (healthCheck.isWifiConnected && !healthCheck.isMqttConnected) {
      healthCheck.isMqttConnected = mqqtClient1.connectMqtt("\n  - MQTT", config.mqtt_username, config.mqtt_password, config.mqtt_topic);
      if(healthCheck.isMqttConnected)
        mqqtClient1.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
    }
  
    // Atualizando BLE advertsting value
    BLE::updateValue(HEALTH_CHECK_UUID, ("HC: " + String(hcCsv)).c_str());
  }

}

// callbacks
int bluetoothController(const char *uid, const std::string &content) {
  if (content.length() == 0) return 0;
  printf("Bluetooth message received: %s\n", uid);
  if (content == "@@RESTART") {
    logIt("Reiniciando Arduino a força;", true);
    delay(2000);
    ESP.restart();
    return 1;
  } else if(content == "@@BLE_SHUTDOWN") {
    logIt("Desligando o BLE permanentemente", true);
    delay(2000);
    BLE::stop();
    return 1;
  } else {
    logIt("Modificando configuração de ambiente via bluetooth", true);
    delay(2000);
    createFile(SD, "/config.txt", content.c_str());
    logIt("Reiniciando Arduino a força;", true);
    ESP.restart();
    return 1;
  }
  return 0;
}

void sendFileChunks(const char* path, const char* fileMqqtTopic, const char* id) {
    File file = SD.open(path);
    if (!file) {
        mqqtClient1.publish(fileMqqtTopic, "{\"error\":\"could not open dir\"}");
        return;
    }

    if (file.isDirectory()) {
       
        File dir = file;
        dir.rewindDirectory(); 
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break; 
            sendFileChunks((String(path)+String("/")+String(entry.name())).c_str(), fileMqqtTopic,id);
            entry.close();
        }
    } else {
      
        size_t fileSize = file.size();
        //TODO change chunkzise acoording to the filepath length
        size_t chunkSize = 256;
        size_t totalChunks = (fileSize + chunkSize - 1) / chunkSize;
        size_t chunkNum = 0;

        while (file.available()) {
            // Create a JSON object for the chunk
            StaticJsonDocument<512> jsonChunk;
            jsonChunk["type"] = "file";
            jsonChunk["filename"] = path;
            jsonChunk["chunk"] = chunkNum + 1; // 1-based index
            jsonChunk["total_chunks"] = totalChunks;
            jsonChunk["id"] = id;
            char data[chunkSize + 1]; 
            size_t bytesRead = file.readBytes(data, chunkSize);
            data[bytesRead] = '\0'; 
            String base64Data = base64::encode((unsigned char*)data, bytesRead);
            jsonChunk["data"] = base64Data;
            char jsonBuffer[513]; 
            serializeJson(jsonChunk, jsonBuffer);
            jsonBuffer[513]=0;
            Serial.print(jsonBuffer);
            mqqtClient1.publish(fileMqqtTopic, jsonBuffer);

            chunkNum++;
        }
        file.close();
        StaticJsonDocument<128> completeMessage;
        completeMessage["type"] = "file";
        completeMessage["filename"] = path;
        completeMessage["chunk"] = 0; // 0 indicates completion
        completeMessage["total_chunks"] = totalChunks;
        completeMessage["data"] = "complete"; // Indicate the transfer is complete
        completeMessage["id"] = id;

        // Publish the completion message
        char completeBuffer[128];
        serializeJson(completeMessage, completeBuffer);
        mqqtClient1.publish(fileMqqtTopic, completeBuffer);

    }
}
  

void executeCommand(JsonObject& docData, const char* sysReportMqqtTopic) {
    OnDebug(Serial.println("Executing command");)
    const char* strCommand = ((const char*)docData["cmd"]);
    char command = strCommand[0]; // Assuming cmd is a string containing a single character

    switch (command) {
        case 'r':{ // Restart
            ESP.restart();
            break;}   

        case 'l':{ // List directory
            const char* dirPath = docData["dir"] | "/";
            File dir = SD.open(dirPath);
            if (!dir || !dir.isDirectory()) {
               mqqtClient1.publish(sysReportMqqtTopic, "Could not open directory");
                return;
            }
            mqqtClient1.publish(sysReportMqqtTopic, "started_sending");
            for (const char* dirList; (dirList = listDirectory(dir, 256))[0]; mqqtClient1.publish(sysReportMqqtTopic, dirList));
            mqqtClient1.publish(sysReportMqqtTopic, "ended_sending");
            dir.close();
            break;}

        case 'g': {// Get file
            const char* filename = docData["fn"] | "";
            sendFileChunks(filename,(String("file") + String(config.mqtt_topic)).c_str(),docData["id"]|"0");break;
            }

        case 'a':{ // Append file
            const char* filename = docData["fn"] | "";
            const char* content = docData["content"] | "";
            appendFile(SD, filename, content);
            break;}

        case 'd':{ // Delete file
            const char* filename = docData["fn"] | "";
            if (SD.remove(filename)) {
              mqqtClient1.publish(sysReportMqqtTopic, "File_deleted_successfully");
            } else {
              mqqtClient1.publish(sysReportMqqtTopic, "could not delte file");
            }
            break;
        }
        case 'v':
        {
        mqqtClient1.publish(sysReportMqqtTopic, FIRMWARE_VERSION);break;
        }
        default:
            mqqtClient1.publish(sysReportMqqtTopic, "Unknown command");
            Serial.println("Unknown command");
            break;
    }
}


void mqttSubCallback(char* topic, unsigned char* payload, unsigned int length) {
    OnDebug(Serial.println("exec MQTT cmd");)
    char* jsonBuffer = new char[length + 1];
    memcpy(jsonBuffer, (char*)payload, length);
    jsonBuffer[length] = '\0';
   
    Serial.println(jsonBuffer);
    Serial.println(topic);

    DynamicJsonDocument doc(length + 1);
    DeserializationError error = deserializeJson(doc, jsonBuffer);
    delete[] jsonBuffer;
    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    JsonObject docData = doc.as<JsonObject>(); 
    if (doc.containsKey("data")) {
        docData = doc["data"].as<JsonObject>(); 
    }
    serializeJson(docData, Serial); 
   
    if (docData.containsKey("cmd")) {
       
       if (strcmp(docData["cmd"],"update")==0) {
      const char* url = docData["url"];
      const char* id = docData["id"];
      if(!id) return;

      Serial.print("URL: ");
      Serial.println(url);
      String urlStr(url);

      const size_t capacity = 100;
      char jsonString[capacity];
      snprintf(jsonString, sizeof(jsonString), "{\"id\":\"%s\",\"status\":1}", id);
      mqqtClient1.publish((sysReportMqqtTopic+String("/OTA")).c_str(), jsonString);
      bool result = OTA::update(urlStr);

      printf("%d",result);
       if(healthCheck.isWifiConnected && !mqqtClient1.loopMqtt()) {
       if (mqqtClient1.connectMqtt("\n  - MQTT2", config.mqtt_username, config.mqtt_password, config.mqtt_topic))
        mqqtClient1.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
      }

      snprintf(jsonString, sizeof(jsonString), "{\"id\":\"%s\",\"status\":%i}", id,4-2*result);
      mqqtClient1.publish((sysReportMqqtTopic+String("/OTA")).c_str(), jsonString);

      Serial.println("Update successful!");
      OnDebug(Serial.println("Reiniciando");)
      delay(1500);
      ESP.restart();
    } 
    else
    executeCommand(docData, sysReportMqqtTopic.c_str());
     
    }
}
  


void convertTimeToLocaleDate(long timestamp) {
  struct tm *ptm = gmtime((time_t *)&timestamp);
  int day = ptm->tm_mday;
  int month = ptm->tm_mon + 1;
  int year = ptm->tm_year + 1900;
  formatedDateString = String(day) + "-" + String(month) + "-" + String(year);
  isBeforeNoon = ptm->tm_hour < 12;
}

// Dever ser usado somente para debugar
void wifiWatcher(){
  if (!healthCheck.isWifiConnected ) {
    if(++wifiDisconnectCount > 60) {
      logIt("Reiniciando a forca, problemas no wifi identificado.");
      ESP.restart();
    };

  } else {
    wifiDisconnectCount = 0;
  }
}