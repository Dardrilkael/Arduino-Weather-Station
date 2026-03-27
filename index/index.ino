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
#include "WifiManager.h"
#include "Sensors.h"
#include "esp_system.h"
#include "bt-integration.h"
#include "mqtt.h"
#include "OTA.h"
#include "httpRecover.h"
#include "commands.h"  // publishJsonResponse, sendFileChunks, executeCommand, mqttSubCallback

// Timing intervals in milliseconds
constexpr unsigned long WDT_TIMEOUT_MS = 600000;

Timer timerBackup(3600000); // 1 hour
Timer timerMain(0);         // interval set correctly in setup() after config loads
Timer timerHealthCheck(10000);

int bluetoothController(const char *uid, const std::string &content);

unsigned long startTime;
std::string jsonConfig = "{}";
struct HealthCheck healthCheck = {FIRMWARE_VERSION, 0, false, false, 0, 0};
String formatedDateString = "";

String sysReportMqttTopic;
WifiManager wifiClient;
MQTT mqttClient;
Sensors sensores;

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
  BLE::Init(bluetoothName);
  BLE::updateValue(CONFIGURATION_UUID, jsonConfig);

  if (!loadedSD)
    while (!loadConfiguration(SD, configFileName, config, jsonConfig))
      ;


   watchdogRTC();

  delay(100);
  logIt("\n1.2 Estabelecendo conexão com wifi ", true);
  wifiClient.setupWifi("  - Wifi", config.wifi_ssid, config.wifi_password);
  storeLog((String(WiFi.RSSI()) + ";").c_str());

  logIt("\n1.3 Estabelecendo conexão com NTP;", true);
  TimeManager::Init();

  logIt("\n1.4 Estabelecendo conexão com MQTT;", true);
  mqttClient.setupMqtt("  - MQTT", config.mqtt_server, config.mqtt_port, config.mqtt_username, config.mqtt_password, config.mqtt_topic);
  mqttClient.setCallback(mqttSubCallback);
  mqttClient.setBufferSize(512);
  mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
  sysReportMqttTopic = (String("sys-report") + String(config.mqtt_topic));

  logIt("\n\n1.5 Iniciando controllers;", true);

  time_t setupTimestamp = TimeManager::getTimestamp();
  formatedDateString = TimeManager::getFormatted(FMT_DATE);
  storeLog(TimeManager::getFormatted(FMT_FULL));

 

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

    // ── BLE mailbox check ──────────────────────────────────────────────
    if (blePendingCommand.pending) {
        blePendingCommand.pending = false;          // clear FIRST before acting
        bluetoothController(blePendingCommand.characteristicUid,
                            blePendingCommand.content);
    }
    // ──────────────────────────────────────────────────────────────────

  digitalWrite(LED3, HIGH);
  unsigned long now = millis();

  sensores.updateWindGust(now);


  mqttClient.loopMqtt();

  digitalWrite(LED1, LOW);

  if (timerBackup.check(now))
    processFiles("/falhas", formatedDateString.c_str());

  if (timerMain.check(now))
  {
    digitalWrite(LED1, HIGH);
    rtc_wdt_feed();

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

  wifiClient.checkWifiReconnection();

  if (timerHealthCheck.check(now))
  {
    rtc_wdt_feed();
    TimeManager::update();
    timestamp = TimeManager::getTimestamp();

    healthCheck.timestamp = timestamp;
    healthCheck.isWifiConnected = WiFi.status() == WL_CONNECTED;
    healthCheck.wifiDbmLevel = !healthCheck.isWifiConnected ? 0 : WiFi.RSSI();
    healthCheck.isMqttConnected = mqttClient.loopMqtt();
    healthCheck.timeRemaining = ((timerMain.lastTime + timerMain.interval - now) / 1000);

    digitalWrite(LED2, healthCheck.isWifiConnected);

    const char *hcCsv = parseHealthCheckData(healthCheck, 1);
    logDebugf("\n\nColetando dados, metricas em %d segundos ...", healthCheck.timeRemaining);
    logDebugf("\n  - %s\n", hcCsv);
    BLE::updateValue(HEALTH_CHECK_UUID, ("HC: " + String(hcCsv)).c_str());

    if (healthCheck.isWifiConnected && !healthCheck.isMqttConnected)
    {
      healthCheck.isMqttConnected = mqttClient.connectMqtt("\n  - MQTT", config.mqtt_username, config.mqtt_password, config.mqtt_topic);
      if (healthCheck.isMqttConnected)
        mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
    }

    char mqttHealthCheck[100];
    snprintf(mqttHealthCheck, sizeof(mqttHealthCheck),
             "{\"timestamp\":%lld,\"wifiDBM\":\"%i\"}",
             (long long)timestamp, WiFi.RSSI());
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
