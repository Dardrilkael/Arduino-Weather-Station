// Autor: Lucas Fonseca e Gabriel Fonseca
// Titulo: GCIPM arduino
// Versão: 2.0.0 OTA;
//.........................................................................................................................

#include "pch.h"
#include "constants.h"
#include "data.h"
#include "sd-repository.h"
String formatedDateString = "";
#include "integration.h"
#include "Sensors.h"
#include <stdio.h>
#include "esp_system.h"
#include "bt-integration.h"
#include <string>
#include <vector>
#include <rtc_wdt.h>
#include "mqtt.h"
#include "OTA.h"
#include <ArduinoJson.h>
#include <Base64.h>
// -- WATCH-DOG

// Timing intervals in milliseconds
constexpr unsigned long WDT_TIMEOUT_MS = 600000;

Timer timer100ms(100);
Timer timerBackup(3600000); // 1 hour
Timer timerMain(config.interval);
Timer timerHealthCheck(10000);

bool sendCSVFile(File &file, const char *url, const char *id = "0");
bool processFiles(const char *dirPath, const char *todayDateString = nullptr, int amount = 1);
int bluetoothController(const char *uid, const std::string &content);
void convertTimeToLocaleDate(long timestamp);

long startTime;
int timeRemaining = 0;
std::string jsonConfig = "{}";
bool isBeforeNoon = true;
struct HealthCheck healthCheck = {FIRMWARE_VERSION, 0, false, false, 0, 0};

// Define constant for wind gust calculation

// -- MQTT
String sysReportMqttTopic;
// String softwareReleaseMqttTopic;,
ModemAT modem;
MQTT mqttClient(modem);
Sensors sensores;
// -- Novo
int wifiDisconnectCount = 0;
//TODO fix log it fruin include in intgration.h also pch.h
void logIt(const std::string &message, bool store)
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
  rtc_wdt_set_time(RTC_WDT_STAGE0, WDT_TIMEOUT_MS); // timeout rtd_wdt 600000ms (10 minutes).
  rtc_wdt_enable();                                 // Start the RTC WDT timer
  rtc_wdt_protect_on();                             // Enable RTC WDT write protection
}




void setup()
{
  #if DEBUG_LOG_ENABLED
    Serial.begin(115200);
  #endif

  // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  delay(3000);
  logIt("\n >> Sistema Integrado de meteorologia << \n");

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
  logDebug("\n\nIniciando sensores ...\n");

  pinMode(16, INPUT_PULLUP);

  logIt("\nIniciando cartão SD");
  initSdCard();
  
  sensores.init();

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
  int nivelDbm = WiFi.RSSI();
  storeLog((String(nivelDbm) + ";").c_str());

  logIt("\n1.3 Estabelecendo conexão com NTP;", true);
  connectNtp("  - NTP");

 // Setup network
    if (!modem.setupNetwork()) {
        Serial.println("Network setup failed!");
        return;
    }
  logIt("\n1.4 Estabelecendo conexão com MQTT;", true);
  mqttClient.setupMqtt("MQTTesp32", config.mqtt_server, config.mqtt_port, config.mqtt_username, config.mqtt_password, config.mqtt_topic);
  mqttClient.setCallback(mqttSubCallback);
  mqttClient.setBufferSize(512);
  mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
  sysReportMqttTopic = (String("sys-report") + String(config.mqtt_topic));

  logIt("\n\n1.5 Iniciando controllers;", true);

  int now = millis();
  // TODO its in sensors init now
  // lastVVTImpulseTime = now;
  // lastPVLImpulseTime = now;

  // 2; Inicio
  logDebugf("\n >> PRIMEIRA ITERAÇÃO\n");

  int setupTimestamp = timeClient.getEpochTime();
  convertTimeToLocaleDate(setupTimestamp);

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
  sprintf(jsonPayload, "{\"version\":\"%s\",\"timestamp\":%lu,\"reason\":%i}", FIRMWARE_VERSION, setupTimestamp, reason);
  mqttClient.publish((sysReportMqttTopic + String("/handshake")).c_str(), jsonPayload, 1);

  // Inicialização dos timers com o tempo atual
  startTime = millis(); // marca o momento de referência

  timerMain.interval = config.interval;
  timerMain.lastTime = startTime;

  timer100ms.lastTime = startTime;
  timerBackup.lastTime = startTime - timerBackup.interval;
  timerHealthCheck.lastTime = startTime;
}

int timestamp = 0;
void loop()
{
  delay(100);
  /*
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
  */
  digitalWrite(LED3, HIGH);
  unsigned long now = millis();
  sensores.updateWindGust(now);
  if (timer100ms.check(now))
  {
    if (!mqttClient.loopMqtt()) {
        Serial.println("MQTT connection lost, attempting reconnect...");
        delay(5000);
        
        if (mqttClient.connectMqtt("\n  - MQTT", config.mqtt_username, config.mqtt_password, config.mqtt_topic)) {
            Serial.println("✓ MQTT reconnected!");
        }
    }
    timestamp = timeClient.getEpochTime();
  }

  digitalWrite(LED1, LOW);

  if (timerBackup.check(now))
  {
    processFiles("/falhas", formatedDateString.c_str());
  }
  if (timerMain.check(now))
  {

    digitalWrite(LED1, HIGH);

    rtc_wdt_feed(); // -- WATCH-DOG

    timeClient.update();
    timestamp = timeClient.getEpochTime();
    convertTimeToLocaleDate(timestamp);

    // Computando dados

    logDebugf("\n\n Computando dados ...\n");

    const Metrics &sensorsData = sensores.getMeasurements(timestamp);
    parseData(sensorsData);

    // Apresentação
    logDebugf("\nResultado JSON:\n%s\n", metricsjsonOutput);

    // Armazenamento local
    logDebugln("\n Gravando em disco:");
    storeMeasurement("/metricas", formatedDateString, metricsCsvOutput);

    // Enviando Dados Remotamente
    logDebugln("\n Enviando Resultados:  ");
    bool measurementSent = mqttClient.publish(config.mqtt_topic, metricsjsonOutput);
    if (!measurementSent)
      storeMeasurement("/falhas", formatedDateString, metricsCsvOutput);

    // Update metrics advertsting value
    BLE::updateValue(HEALTH_CHECK_UUID, ("ME: " + String(metricsCsvOutput)).c_str());
    logDebugf("\n >> PROXIMA ITERAÇÃO\n");
  }
  checkWifiReconnection(config.wifi_ssid, config.wifi_password);
  if (timerHealthCheck.check(now))
  {

    // Health check
    healthCheck.timestamp = timestamp;
    healthCheck.isWifiConnected = WiFi.status() == WL_CONNECTED;
    healthCheck.wifiDbmLevel = !healthCheck.isWifiConnected ? 0 : (WiFi.RSSI());
    healthCheck.isMqttConnected = mqttClient.loopMqtt();
    healthCheck.timeRemaining = ((timerMain.lastTime + timerMain.interval - now) / 1000);

    digitalWrite(LED2, healthCheck.isWifiConnected);

    const char *hcCsv = parseHealthCheckData(healthCheck, 1);

    logDebugf("\n\nColetando dados, metricas em %d segundos ...", healthCheck.timeRemaining);
    logDebugf("\n  - %s\n", hcCsv);
    // Atualizando BLE advertsting value
    BLE::updateValue(HEALTH_CHECK_UUID, ("HC: " + String(hcCsv)).c_str());

    // if(!healthCheck.isWifiConnected)setupWifi("  - Wifi", config.wifi_ssid, config.wifi_password);
    //  Garantindo conexão com mqqt broker;
    if (healthCheck.isWifiConnected && !healthCheck.isMqttConnected)
    {
      healthCheck.isMqttConnected = mqttClient.connectMqtt("\n  - MQTT", config.mqtt_username, config.mqtt_password, config.mqtt_topic);
      if (healthCheck.isMqttConnected)
        mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
    }
    // Enviando dados de health check
    char mqttHealthCheck[100];
    sprintf(mqttHealthCheck, "{\"timestamp\":%lu,\"wifiDBM\":\"%i\"}", timestamp, WiFi.RSSI());
    mqttClient.publish((sysReportMqttTopic + String("/healthcheck")).c_str(), mqttHealthCheck, 1); // retained
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
  char buffer[512];
  size_t len = serializeJson(response, buffer, sizeof(buffer));
  mqttClient.publish(topic, buffer, false);
}

void sendFileChunks(const char *path, const char *fileMqqtTopic, const char *id)
{
  File file = SD.open(path);
  if (!file)
  {
    mqttClient.publish(fileMqqtTopic, "{\"error\":\"could not open file\"}");
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
    jsonChunk["chunk"] = ++chunkNum; // 1-based index
    jsonChunk["id"] = id;

    char data[chunkSize];
    size_t bytesRead = file.readBytes(data, chunkSize);
    String base64Data = base64::encode((unsigned char *)data, bytesRead);
    jsonChunk["data"] = base64Data;

    publishJsonResponse(fileMqqtTopic, jsonChunk.as<JsonObject>());
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

void executeCommand(JsonObject &docData, const char *sysReportMqttTopic)
{
  logDebugln("Executing command");

  const char *id = docData["id"] | "0";
  const char *strCommand = docData["cmd"] | "z";
  char command = strCommand[0]; // First char as command

  // Helper function to publish JSON responses
  auto send = [sysReportMqttTopic](JsonDocument &doc)
  {
    publishJsonResponse(sysReportMqttTopic, doc.as<JsonObject>());
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

    const char *dirList = "\00";
    for (; (dirList = listDirectory(dir, 128))[0];)
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
    const char *url = docData["url"] | "0";

    if (!url[0])
    {
      response["error"] = "no destination";
      send(response);
    }
    File file = SD.open(filename);
    if (!file)
    {
      response["error"] = "could not open file";
      send(response);
      break;
    }

    response["status"] = "single";
    response["sent"] = sendCSVFile(file, url, id);
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

  case 'c':
  { // Configuration
    response["status"] = "single";
    response["config"] = jsonConfig;
    send(response);
    break;
  }
  case 'u':
  { // Update OTA
    const char *url = docData["url"] | "";
    if (!url || !*url)
    {
      response["status"] = "error";
      response["error"] = "No URL provided";
      send(response);
      return;
    }
    // Send OTA start status
    response["status"] = "start";
    send(response);
    response["status"] = "prs";
    OTA_Result result = OTA::update(String(url), [&](int progres)
                              {
        response["prs"] = progres;
        send(response); });

    // Ensure MQTT connection after OTA
    if (healthCheck.isWifiConnected && !mqttClient.loopMqtt())
    {
      if (mqttClient.connectMqtt("\n  - MQTT", config.mqtt_username, config.mqtt_password, config.mqtt_topic))
        mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
    }

    // Send OTA result status
    response["status"] = result ? "success" : "failure";
    response["message"] = result.error.c_str();
    send(response);

    logDebugln("Update successful!");
    logDebugln("Reiniciando");
    delay(1500);
    ESP.restart();
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

  DynamicJsonDocument doc(1024 + 128); // Add some headroom
  DeserializationError error = deserializeJson(doc, payload, length);
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
    const char *cmd = docData["cmd"];
    if (strcmp(cmd, "update") == 0)
      docData["cmd"] = "u";
    executeCommand(docData, sysReportMqttTopic.c_str());
 
  }
}

void convertTimeToLocaleDate(long timestamp)
{
  time_t t = (time_t)timestamp;
  struct tm *ptm = gmtime(&t);
  char dateBuffer[11];
  int day = ptm->tm_mday;
  int month = ptm->tm_mon + 1;
  int year = ptm->tm_year + 1900;
  isBeforeNoon = ptm->tm_hour < 12;
  sprintf(dateBuffer, "%02d-%02d-%04d", day, month, year);
  formatedDateString = String(dateBuffer);
}
