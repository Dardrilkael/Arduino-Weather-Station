// Autor: Lucas Fonseca e Gabriel Fonseca
// Titulo: GCIPM arduino
// Versão: 2.0.0 OTA;
//.........................................................................................................................

#include "pch.h"
#include <ArduinoJson.h>
#include <rtc_wdt.h>
#include <esp_bt.h>
#include <stdio.h>
#include <string>

#include "constants.h"
#include "data.h"
#include "sd-repository.h"
#include "TimeManager.h"
//#include "WifiManager.h"

#include "Sensors.h"
#include "esp_system.h"
#include "bt-integration.h"

#include "OTA.h"
#include "base64.h"
#include "Commands.h"

#include "ModemAT.h"
#include "mqtt.h"
#include "httpClient.h"

static constexpr int   RX_PIN    = 16;
static constexpr int   TX_PIN    = 17;
static constexpr ulong BAUD_RATE = 115200;
// -- WATCH-DOG

// Timing intervals in milliseconds
constexpr unsigned long WDT_TIMEOUT_MS = 600000;

Timer timer100ms(100);
Timer timerBackup(3600000); // 1 hour
Timer timerMain(config.interval);
Timer timerHealthCheck(10000);

int bluetoothController(const char *uid, const std::string &content);

unsigned long startTime;
std::string jsonConfig = "{}";
struct HealthCheck healthCheck = {FIRMWARE_VERSION, 0, false, false, 0, 0};
String formatedDateString = "";

String sysReportMqttTopic;

ModemAT modem(16, 17, 115200);
MQTT mqttClient(modem);
HttpClient http(modem);

Sensors sensores;
// -- Novo
int wifiDisconnectCount = 0;

//TODO fix log it fruin include in intgration.h also pch.h
void logIt(const std::string &message, bool store)
{
  logDebug(message.c_str());
  if (store)
    storeLog(message.c_str());
}

void watchdogRTC()
{
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
  rtc_wdt_set_time(RTC_WDT_STAGE0, WDT_TIMEOUT_MS);
  rtc_wdt_enable();
  rtc_wdt_protect_on();
}

bool syncClock()
{
    ATResponse resp = modem.sendCommand("AT+CCLK?", 5000);

    for (int i = 0; i < resp.lineCount; i++)
    {
        if (resp.lines[i].startsWith("+CCLK:"))
        {
            return TimeManager::syncFromModemCCLK(resp.lines[i].c_str());
        }
    }
    return false;
}

void setup()
{
  #if DEBUG_LOG_ENABLED
    Serial.begin(115200);
  #endif

  delay(3000);
  logIt("\n >> Sistema Integrado de meteorologia << \n");

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);



    modem.begin();

  start:
    delay(300);
    auto resp = modem.checkSim();
    Serial.printf("O modem %s foi inicializado\n", resp ? "" : "nao");
    if (!resp)
        goto start;
  
  modem.setupNetwork("zap.vivo.com.br");
  IPAddressView IP = modem.query<IPAddressView>("AT+CGPADDR=1");
    Serial.printf("The newly found ip is: %s\n", IP.ip);

    SignalQuality quality = modem.query<SignalQuality>("AT+CSQ");
    quality.print();

  logDebug("\n\nIniciando sensores ...\n");
  pinMode(16, INPUT_PULLUP);

  logIt("\nIniciando cartão SD");
  initSdCard();
  sensores.init();

  logIt("\nCriando diretorios padrões");
  createDirectory("/metricas");
  createDirectory("/logs");

  logIt("\n1. Estação iniciada: ", true);
  esp_reset_reason_t reason = esp_reset_reason();
  logIt(String(reason).c_str(), true);

  bool loadedSD = loadConfiguration(SD, configFileName, config, jsonConfig);
  const char *bluetoothName = loadedSD ? config.station_name : "est000";

  logIt("\n1.1 Iniciando bluetooth;", true);
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  BLE::Init(bluetoothName, bluetoothController);
  BLE::updateValue(CONFIGURATION_UUID, jsonConfig);

  if (!loadedSD)
    while (!loadConfiguration(SD, configFileName, config, jsonConfig))
      ;

  delay(100);
  logIt("\n1.2 Estabelecendo conexão com wifi ", true);
  //wifiClient.setupWifi("  - Wifi", config.wifi_ssid, config.wifi_password);
  //int nivelDbm = WiFi.RSSI();
  //storeLog((String(nivelDbm) + ";").c_str());

  logIt("\n1.3 Estabelecendo conexão com NTP;", true);
  //connectNtp("  - NTP");
  TimeManager::Init();

  logIt("\n1.4 Estabelecendo conexão com MQTT;", true);
  mqttClient.setupMqtt("  - MQTT", config.mqtt_server, config.mqtt_port, config.mqtt_username, config.mqtt_password, config.mqtt_topic);
  delay(400);
  mqttClient.setCallback(mqttSubCallback);


 

  
  String mierda = (String("sys") + String(config.mqtt_topic));
  mqttClient.subscribe(mierda.c_str());
  
    modem.addURCHandler([&](const String &line) -> bool
                        { return mqttClient.processLine(line); });

  sysReportMqttTopic = (String("sys-report") + String(config.mqtt_topic));

  logIt("\n\n1.5 Iniciando controllers;", true);

  time_t setupTimestamp = TimeManager::getTimestamp();
  formatedDateString = TimeManager::getFormatted(FMT_DATE);
  storeLog(TimeManager::getFormatted(FMT_FULL));

  watchdogRTC();

  for (int i = 0; i < 7; i++)
  {
    digitalWrite(LED1, i % 2);
    delay(400);
  }

  logDebugln(reason);
  char jsonPayload[100]{0};
  snprintf(jsonPayload, sizeof(jsonPayload),
           "{\"version\":\"%s\",\"timestamp\":%lld,\"reason\":%i}",
           FIRMWARE_VERSION, (long long)setupTimestamp, reason);
  mqttClient.publish((sysReportMqttTopic + String("/handshake")).c_str(), jsonPayload, 1);

  startTime = millis();
  timerMain.interval = config.interval;
  timerMain.lastTime = startTime;
  timer100ms.lastTime = startTime;
  timerBackup.lastTime = startTime - timerBackup.interval;
  timerHealthCheck.lastTime = startTime;
}

time_t timestamp = 0;
// Output buffers for parsed metrics — defined here (the only place they're used)
// rather than as globals exported from data.h.
static char metricsjsonOutput[240];
static char metricsCsvOutput[240];
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
    //mqttClient.loopMqtt();
  }

  digitalWrite(LED1, LOW);

  //if (timerBackup.check(now))
    //processFiles("/falhas", formatedDateString.c_str());

  if (timerMain.check(now))
  {

    digitalWrite(LED1, HIGH);

    rtc_wdt_feed(); // -- WATCH-DOG

    TimeManager::update();
    timestamp = TimeManager::getTimestamp();
    formatedDateString = TimeManager::getFormatted(FMT_DATE);

    logDebugf("\n\n Computando dados ...\n");
    const Metrics &sensorsData = sensores.getMeasurements(timestamp);
    parseData(sensorsData,
              metricsjsonOutput, sizeof(metricsjsonOutput),
              metricsCsvOutput,  sizeof(metricsCsvOutput));

    logDebugf("\nResultado JSON:\n%s\n", metricsjsonOutput);

    logDebugln("\n Gravando em disco:");
    storeMeasurement("/metricas", formatedDateString, metricsCsvOutput);

    logDebugln("\n Enviando Resultados:  ");
    bool measurementSent = mqttClient.publish(config.mqtt_topic, metricsjsonOutput);
    if (!measurementSent)
      storeMeasurement("/falhas", formatedDateString, metricsCsvOutput);

    BLE::updateValue(HEALTH_CHECK_UUID, ("ME: " + String(metricsCsvOutput)).c_str());
    logDebugf("\n >> PROXIMA ITERAÇÃO\n");
  }
  //wifiClient.checkWifiReconnection();
  if (timerHealthCheck.check(now))
  {
    TimeManager::update();
    timestamp = TimeManager::getTimestamp();

    healthCheck.timestamp = timestamp;
    healthCheck.isWifiConnected = true;//WiFi.status() == WL_CONNECTED;
    healthCheck.wifiDbmLevel = 1;// !healthCheck.isWifiConnected ? 0 : (WiFi.RSSI());
    healthCheck.isMqttConnected = false;//mqttClient.loopMqtt();
    healthCheck.timeRemaining = ((timerMain.lastTime + timerMain.interval - now) / 1000);

    digitalWrite(LED2, healthCheck.isWifiConnected);

    const char *hcCsv = parseHealthCheckData(healthCheck, 1);
    logDebugf("\n\nColetando dados, metricas em %d segundos ...", healthCheck.timeRemaining);
    logDebugf("\n  - %s\n", hcCsv);
    BLE::updateValue(HEALTH_CHECK_UUID, ("HC: " + String(hcCsv)).c_str());

    if (healthCheck.isWifiConnected && !healthCheck.isMqttConnected)
    {
     // healthCheck.isMqttConnected = mqttClient.connectMqtt("\n  - MQTT", config.mqtt_username, config.mqtt_password, config.mqtt_topic);
     // if (healthCheck.isMqttConnected)
     //   mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
    }

    char mqttHealthCheck[100];
    snprintf(mqttHealthCheck, sizeof(mqttHealthCheck),
             "{\"timestamp\":%lld,\"wifiDBM\":\"%i\"}",
             (long long)timestamp,1);
    mqttClient.publish((sysReportMqttTopic + String("/healthcheck")).c_str(), mqttHealthCheck, 1);
  }
}

int bluetoothController(const char *uid, const std::string &content)
{
  if (content.length() == 0)
    return 0;
  printf("Bluetooth message received: %s\n", uid);

  if (content == "@@RESTART")
  {
    logIt("Reiniciando Arduino a força;", true);
    delay(2000);
    flushLog();
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
    flushLog();
    ESP.restart();
    return 1;
  }
  return 0;
}

/*
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
    //response["sent"] = sendCSVFile(file, url, id);
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
    //if (healthCheck.isWifiConnected && !mqttClient.loopMqtt())
    {
     // if (mqttClient.connectMqtt("\n  - MQTT2", config.mqtt_username, config.mqtt_password, config.mqtt_topic))
    //    mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
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

void mqttSubCallback(const char *topic, const unsigned char *payload, unsigned int length)
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
}*/


