// Titulo: Integração HTTP & MQQT
// Data: 30/07/2023
//.........................................................................................................................
#include "pch.h"

#pragma once
#include <WiFi.h> // #include <WiFiClientSecure.h>
#include <NTPClient.h>

/**** WIFI Client Initialisation *****/
WiFiClient wifiClient;

/**** Secure WiFi Connectivity Initialisation *****/
// WiFiClientSecure secureWifiClient;

/**** NTP Client Initialisation  *****/
WiFiUDP ntpUDP;
// const char* ntpServer = "a.st1.ntp.br";
const char *ntpServer = "br.pool.ntp.org";
NTPClient timeClient(ntpUDP, ntpServer);

/**** MQTT Client Initialisation Using WiFi Connection *****/

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];

/***** WIFI ****/

int setupWifi(const char *contextName, char *ssid, char *password)
{
  logDebugf("%s: Estabelecendo conexão inicial", contextName);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  // WiFi.persistent(true);
  //  secureWifiClient.setCACert(root_ca);    // enable this line and the the "certificate" code for secure connection
  while (WiFi.status() != WL_CONNECTED)
  {
    for (int i = 0; i < 5; i++)
    {
      digitalWrite(LED2, i % 2);
      delay(200);
    }
    logDebug(".");
  }

  logDebugf("\n%s: Contectado com sucesso \n", contextName);
  logDebugf("%s: IP address = %s \n", contextName, WiFi.localIP().toString().c_str());
  return 1;
}

/* Ntp Client */
int connectNtp(const char *contextName)
{
  logDebugf("%s: Estabelecendo conexão inicial\n", contextName);

  timeClient.begin();

  while (!timeClient.update())
  {
    logDebug(".");
    delay(1000);
  }

  logDebugf("%s: Conectado com sucesso. \n", contextName);
  return 1;
}
