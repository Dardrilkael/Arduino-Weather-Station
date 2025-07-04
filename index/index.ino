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
#define HTTP_BACKUP_INTERVAL 3600000
bool sendCSVFile(File &file, const char *url);
bool processFiles(const char *dirPath, const char *todayDateString = nullptr, int amount = 1);
extern volatile unsigned long lastPVLImpulseTime;
extern volatile unsigned int rainCounter;
extern volatile unsigned long lastVVTImpulseTime;
extern volatile int anemometerCounter;
extern int rps[20];
extern Sensors sensors;
long startTime;
long startTime5_Seconds;
unsigned long startTime_BACKUP;
long startTime100_mS;
int timeRemaining = 0;
std::string jsonConfig = "{}";
String formatedDateString = "";
bool isBeforeNoon = true;
struct HealthCheck healthCheck = {FIRMWARE_VERSION, 0, false, false, 0, 0};
// -- MQTT
String sysReportMqqtTopic;
// String softwareReleaseMqttTopic;
MQTT mqqtClient1;

// -- Novo
int wifiDisconnectCount = 0;

void logIt(const std::string &message, bool store = false)
{
  logDebug(message.c_str());
  if (store == true)
  {
    storeLog(message.c_str());
  }
}

void watchdogRTC()
{
  rtc_wdt_protect_off(); // Disable RTC WDT write protection
  rtc_wdt_disable();
  rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
  rtc_wdt_set_time(RTC_WDT_STAGE0, WDT_TIMEOUT); // timeout rtd_wdt 10000ms.
  rtc_wdt_enable();                              // Start the RTC WDT timer
  rtc_wdt_protect_on();                          // Enable RTC WDT write protection
}

void setup()
{
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  delay(3000);
  logIt("\n >> Sistema Integrado de meteorologia << \n");

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

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

  logDebugf("\n - Carregando variáveis de ambiente");
  logIt("\n1. Estação iniciada: ", true);
  esp_reset_reason_t reason = esp_reset_reason();
  logIt(String(reason).c_str(), true);

  bool loadedSD = loadConfiguration(SD, configFileName, config, jsonConfig);
  const char *bluetoothName = nullptr;
  if (loadedSD)
    bluetoothName = config.station_name;
  else
    bluetoothName = "est000";

  logIt("\n1.1 Iniciando bluetooth;", true);
  BLE::Init(bluetoothName, bluetoothController);
  BLE::updateValue(CONFIGURATION_UUID, jsonConfig);

  if (!loadedSD)
    while (!loadConfiguration(SD, configFileName, config, jsonConfig))
      ;

  logIt("\n1.2 Estabelecendo conexão com wifi ", true);
  setupWifi("  - Wifi", config.wifi_ssid, config.wifi_password);
  int nivelDbm = (WiFi.RSSI()) * -1;
  storeLog((String(nivelDbm) + ";").c_str());

  logIt("\n1.3 Estabelecendo conexão com NTP;", true);
  connectNtp("  - NTP");

  logIt("\n1.4 Estabelecendo conexão com MQTT;", true);
  mqqtClient1.setupMqtt("  - MQTT", config.mqtt_server, config.mqtt_port, config.mqtt_username, config.mqtt_password, config.mqtt_topic);
  // softwareReleaseMqttTopic = String("software-release/") + String(config.mqtt_topic);

  // mqqtClient2.setupMqtt("- MQTT2", config.mqtt_hostV2_server, config.mqtt_hostV2_port, config.mqtt_hostV2_username, config.mqtt_hostV2_password, softwareReleaseMqttTopic.c_str());
  mqqtClient1.setCallback(mqttSubCallback);
  mqqtClient1.setBufferSize(512);
  mqqtClient1.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
  sysReportMqqtTopic = (String("sys-report") + String(config.mqtt_topic));

  logIt("\n\n1.5 Iniciando controllers;", true);
  setupSensors();

  int now = millis();
  lastVVTImpulseTime = now;
  lastPVLImpulseTime = now;

  // 2; Inicio
  logDebugf("\n >> PRIMEIRA ITERAÇÃO\n");

  int timestamp = timeClient.getEpochTime();
  convertTimeToLocaleDate(timestamp);

  String dataHora = String(formatedDateString) + "T" + timeClient.getFormattedTime();
  storeLog(("\n" + dataHora + "\n").c_str());

  // -- WATCH-DOG
  watchdogRTC();

  for (int i = 0; i < 7; i++)
  {
    digitalWrite(LED1, i % 2);
    delay(400);
  }

  logDebugln(reason);
  char jsonPayload[100]{0};
  sprintf(jsonPayload, "{\"version\":\"%s\",\"timestamp\":%lu,\"reason\":%i}", FIRMWARE_VERSION, timestamp, reason);
  mqqtClient1.publish((sysReportMqqtTopic + String("/handshake")).c_str(), jsonPayload, 1);

  startTime = millis();
  startTime5_Seconds = startTime;
  startTime100_mS = startTime;
  startTime_BACKUP = startTime - HTTP_BACKUP_INTERVAL;
}

int timestamp = 0;
void loop()
{
  if (Serial.available())
  {
    char input = Serial.read();
    if (input == 'r' || input == 'R')
    {
      logDebugln("🔄 Reiniciando dispositivo...");
      delay(1000);
      ESP.restart();
    }
  }

  digitalWrite(LED3, HIGH);
  unsigned long now = millis();
  WindGustRead(now);
  if (now - startTime100_mS >= 100)
  {
    startTime100_mS = now;
    mqqtClient1.loopMqtt();
    timestamp = timeClient.getEpochTime();
  }

  digitalWrite(LED1, LOW);

  if (now - startTime_BACKUP >= HTTP_BACKUP_INTERVAL)
  {
    startTime_BACKUP = now;
    processFiles("/falhas", formatedDateString.c_str());
  }
  if (now - startTime >= config.interval)
  {
    startTime = now;

    digitalWrite(LED1, HIGH);

    rtc_wdt_feed(); // -- WATCH-DOG

    timeClient.update();
    timestamp = timeClient.getEpochTime();
    convertTimeToLocaleDate(timestamp);

    // Computando dados

    logDebugf("\n\n Computando dados ...\n");

    Data.timestamp = timestamp;
    Data.wind_dir = getWindDir();
    Data.rain_acc = rainCounter * VOLUME_PLUVIOMETRO;
    Data.wind_gust = 3.052f / 3.0f * ANEMOMETER_CIRC * findMax(rps, sizeof(rps) / sizeof(int));
    Data.wind_speed = 3.052 * (ANEMOMETER_CIRC * anemometerCounter) / (config.interval / 1000.0); // m/s

    DHTRead(Data.humidity, Data.temperature);
    BMPRead(Data.pressure);

    // Apresentação
    parseData();
    // logDebugf("\nResultado CSV:\n%s", metricsCsvOutput); )
    logDebugf("\nResultado JSON:\n%s\n", metricsjsonOutput);

    // Armazenamento local
    logDebugln("\n Gravando em disco:");
    storeMeasurement("/metricas", formatedDateString, metricsCsvOutput);

    // Enviando Dados Remotamente
    logDebugln("\n Enviando Resultados:  ");
    bool measurementSent1 = mqqtClient1.publish(config.mqtt_topic, metricsjsonOutput);
    if (!measurementSent1)
      storeMeasurement("/falhas", formatedDateString, metricsCsvOutput);
    // Update metrics advertsting value
    BLE::updateValue(HEALTH_CHECK_UUID, ("ME: " + String(metricsCsvOutput)).c_str());
    logDebugf("\n >> PROXIMA ITERAÇÃO\n");
    resetSensors();
  }

  now = millis();
  if (now - startTime5_Seconds >= 5000)
  {
    startTime5_Seconds = now;

    // Health check
    healthCheck.timestamp = timestamp;
    healthCheck.isWifiConnected = WiFi.status() == WL_CONNECTED;
    healthCheck.wifiDbmLevel = !healthCheck.isWifiConnected ? 0 : (WiFi.RSSI());
    healthCheck.isMqttConnected = mqqtClient1.loopMqtt();
    healthCheck.timeRemaining = ((startTime + config.interval - now) / 1000);

    digitalWrite(LED2, healthCheck.isWifiConnected);
    if (!healthCheck.isWifiConnected)
    {
      logIt((formatedDateString + "  ").c_str(), true);
      logIt(timeClient.getFormattedTime().c_str(), true);
      logIt(": lp-wf-rcnt\n", true);
      WiFi.disconnect(true, true);
      delay(500);
      WiFi.mode(WIFI_STA);
      WiFi.begin(config.wifi_ssid, config.wifi_password);
    }
    const char *hcCsv = parseHealthCheckData(healthCheck, 1);

    logDebugf("\n\nColetando dados, metricas em %d segundos ...", ((startTime + config.interval - now) / 1000));
    logDebugf("\n  - %s\n", hcCsv);
    // Atualizando BLE advertsting value
    BLE::updateValue(HEALTH_CHECK_UUID, ("HC: " + String(hcCsv)).c_str());

    // if(!healthCheck.isWifiConnected)setupWifi("  - Wifi", config.wifi_ssid, config.wifi_password);
    //  Garantindo conexão com mqqt broker;
    if (healthCheck.isWifiConnected && !healthCheck.isMqttConnected)
    {
      healthCheck.isMqttConnected = mqqtClient1.connectMqtt("\n  - MQTT", config.mqtt_username, config.mqtt_password, config.mqtt_topic);
      if (healthCheck.isMqttConnected)
        mqqtClient1.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
    }
    // Enviando dados de health check
    char mqttHealthCheck[100];
    sprintf(mqttHealthCheck, "{\"timestamp\":%lu,\"wifiDBM\":\"%i\"}", timestamp, WiFi.RSSI());
    mqqtClient1.publish((sysReportMqqtTopic + String("/healthcheck")).c_str(), mqttHealthCheck, 1); // retained
  }
}

// callbacks
int bluetoothController(const char *uid, const std::string &content)
{
  if (content.length() == 0)
    return 0;
  printf("Bluetooth message received: %s\n", uid);
  if (content == "@@RESTART")
  {
    logIt("Reiniciando Arduino a força;", true);
    delay(2000);
    ESP.restart();
    return 1;
  }
  else if (content == "@@BLE_SHUTDOWN")
  {
    logIt("Desligando o BLE permanentemente", true);
    delay(2000);
    BLE::stop();
    return 1;
  }
  else
  {
    logIt("Modificando configuração de ambiente via bluetooth", true);
    delay(2000);
    createFile(SD, "/config.txt", content.c_str());
    logIt("Reiniciando Arduino a força;", true);
    ESP.restart();
    return 1;
  }
  return 0;
}

void publishJsonResponse(const char *topic, const JsonObject &response)
{
  char buffer[256];
  size_t len = serializeJson(response, buffer, sizeof(buffer));
  mqqtClient1.publish(topic, buffer, false);
}

void sendFileChunks(const char *path, const char *fileMqqtTopic, const char *id)
{
  File file = SD.open(path);
  if (!file)
  {
    mqqtClient1.publish(fileMqqtTopic, "{\"error\":\"could not open file\"}");
    return;
  }

  if (file.isDirectory())
  {
    File dir = file;
    dir.rewindDirectory();
    while (true)
    {
      File entry = dir.openNextFile();
      if (!entry)
        break;
      sendFileChunks((String(path) + "/" + entry.name()).c_str(), fileMqqtTopic, id);
      entry.close();
    }
    file.close();
    return;
  }

  size_t fileSize = file.size();
  size_t chunkSize = 256;
  size_t totalChunks = (fileSize + chunkSize - 1) / chunkSize;
  size_t chunkNum = 0;

  // Send start message with totalChunks
  {
    StaticJsonDocument<256> startMsg;
    startMsg["status"] = "start";
    startMsg["filename"] = path;
    startMsg["totalChunks"] = totalChunks;
    startMsg["id"] = id;

    publishJsonResponse(fileMqqtTopic, startMsg.as<JsonObject>());
  }

  // Send data chunks
  while (file.available())
  {
    StaticJsonDocument<512> jsonChunk;
    jsonChunk["chunk"] = chunkNum + 1; // 1-based index
    jsonChunk["id"] = id;

    char data[chunkSize];
    size_t bytesRead = file.readBytes(data, chunkSize);
    String base64Data = base64::encode((unsigned char *)data, bytesRead);
    jsonChunk["data"] = base64Data;

    char jsonBuffer[513];
    serializeJson(jsonChunk, jsonBuffer);
    mqqtClient1.publish(fileMqqtTopic, jsonBuffer);

    chunkNum++;
  }
  file.close();

  // Send end message with totalChunks

  StaticJsonDocument<256> endMsg;
  endMsg["status"] = "end";
  endMsg["filename"] = path;
  endMsg["totalChunks"] = totalChunks;
  endMsg["id"] = id;
  publishJsonResponse(fileMqqtTopic, endMsg.as<JsonObject>());
}

void executeCommand(JsonObject &docData, const char *sysReportMqqtTopic)
{
  logDebugln("Executing command");

  const char *id = docData["id"] | "0";
  const char *strCommand = docData["cmd"] | "z";
  char command = strCommand[0]; // First char as command

  // Helper function to publish JSON responses
  auto send = [sysReportMqqtTopic](JsonDocument &doc)
  {
    publishJsonResponse(sysReportMqqtTopic, doc.as<JsonObject>());
  };

  DynamicJsonDocument response(256);
  response["id"] = id;

  switch (command)
  {

  case 'r':
  { // Restart
    response["status"] = "single";
    response["message"] = "Restarting ESP32";
    send(response);
    delay(100); // Allow MQTT to send
    ESP.restart();
    break;
  }

  case 'l':
  { // List directory
    const char *dirPath = docData["dir"] | "/";
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory())
    {
      response["status"] = "error";
      response["error"] = "Could not open directory";
      send(response);
      return;
    }

    // Start message
    response["status"] = "start";
    send(response);

    // Send directory contents in chunks
    DynamicJsonDocument chunkResponse(256);
    chunkResponse["id"] = id;

    for (const char *dirList; (dirList = listDirectory(dir, 128))[0];)
    {
      // tring base64Data = base64::encode((unsigned char*)data, bytesRead);
      chunkResponse["data"] = dirList; // base64::encode((unsigned char*)dirList,strlen(dirList));
      send(chunkResponse);
    }

    // End message
    response.clear();
    response["id"] = id;
    response["status"] = "end";
    send(response);
    dir.close();
    break;
  }

  case 'g':
  { // Get file (delegates to existing handler)
    const char *filename = docData["fn"] | "";
    sendFileChunks(filename, (String("file") + String(config.mqtt_topic)).c_str(), id);
    break;
  }

  case 'h':
  { // Get file (delegates to existing handler)
    const char *filename = docData["fn"] | "";
    File file = SD.open(filename);
    if (!file)
    {
      response["error"] = "could not open file";
      send(response);
      break;
    }

    response["status"] = "single";
    response["sent"] = sendCSVFile(file, "http://192.168.0.223:3000/api/upload-file");
    send(response);
    break;
  }

  case 'a':
  { // Append to file
    const char *filename = docData["fn"] | "";
    const char *content = docData["content"] | "";
    appendFile(SD, filename, content);
    response["status"] = "single";
    response["message"] = "Content appended";
    send(response);
    break;
  }

  case 'd':
  { // Delete file
    const char *filename = docData["fn"] | "";
    response["status"] = "single";
    if (SD.remove(filename))
    {
      response["message"] = "File deleted successfully";
    }
    else
    {
      response["message"] = "Could not delete file";
    }
    send(response);
    break;
  }

  case 'v':
  { // Firmware version
    response["status"] = "single";
    response["version"] = FIRMWARE_VERSION;
    send(response);
    break;
  }

  default:
  {
    response["status"] = "error";
    response["error"] = "Unknown command";
    send(response);
    logDebugln("Unknown command");
    break;
  }
  }
}

void mqttSubCallback(char *topic, unsigned char *payload, unsigned int length)
{
  logDebugln("exec MQTT cmd");
  char *jsonBuffer = new char[length + 1];
  memcpy(jsonBuffer, (char *)payload, length);
  jsonBuffer[length] = '\0';

  logDebugln(jsonBuffer);
  logDebugln(topic);

  DynamicJsonDocument doc(length + 1);
  DeserializationError error = deserializeJson(doc, jsonBuffer);
  delete[] jsonBuffer;
  if (error)
  {
    logDebug("deserializeJson() failed: ");
    logDebugln(error.c_str());
    return;
  }

  JsonObject docData = doc.as<JsonObject>();
  if (doc.containsKey("data"))
  {
    docData = doc["data"].as<JsonObject>();
  }
  serializeJson(docData, Serial);

  if (docData.containsKey("cmd"))
  {

    if (strcmp(docData["cmd"], "update") == 0)
    {
      const char *url = docData["url"];
      const char *id = docData["id"];
      if (!id)
        return;

      logDebug("URL: ");
      logDebugln(url);
      String urlStr(url);

      const size_t capacity = 100;
      char jsonString[capacity];
      snprintf(jsonString, sizeof(jsonString), "{\"id\":\"%s\",\"status\":1}", id);
      mqqtClient1.publish((sysReportMqqtTopic + String("/OTA")).c_str(), jsonString);
      bool result = OTA::update(urlStr);

      printf("%d", result);
      if (healthCheck.isWifiConnected && !mqqtClient1.loopMqtt())
      {
        if (mqqtClient1.connectMqtt("\n  - MQTT2", config.mqtt_username, config.mqtt_password, config.mqtt_topic))
          mqqtClient1.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
      }

      snprintf(jsonString, sizeof(jsonString), "{\"id\":\"%s\",\"status\":%i}", id, 4 - 2 * result);
      mqqtClient1.publish((sysReportMqqtTopic + String("/OTA")).c_str(), jsonString);

      logDebugln("Update successful!");
      logDebugln("Reiniciando");
      delay(1500);
      ESP.restart();
    }
    else
      executeCommand(docData, sysReportMqqtTopic.c_str());
  }
}

void convertTimeToLocaleDate(long timestamp)
{
  struct tm *ptm = gmtime((time_t *)&timestamp);
  int day = ptm->tm_mday;
  int month = ptm->tm_mon + 1;
  int year = ptm->tm_year + 1900;
  formatedDateString = String(day) + "-" + String(month) + "-" + String(year);
  isBeforeNoon = ptm->tm_hour < 12;
}
