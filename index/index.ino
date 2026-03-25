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



