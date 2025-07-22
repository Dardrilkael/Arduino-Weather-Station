#pragma once
// --- Config data  ---
struct Config
{
  char station_uid[32]{0};
  char station_name[64]{0};
  char wifi_ssid[64]{0};
  char wifi_password[64]{0};
  char mqtt_server[64]{0};
  char mqtt_username[64]{0};
  char mqtt_password[64]{0};
  char mqtt_topic[64]{0};
  int mqtt_port;
  int interval;
};

extern Config config;
extern const char *configFileName;

// --- HeachCheck data  ---

struct HealthCheck
{
  const char *softwareVersion;
  int timestamp;
  bool isWifiConnected;
  bool isMqttConnected;
  int wifiDbmLevel;
  int timeRemaining;
};

extern char hcJsonOutput[240];
extern char hcCsvOutput[240];
const char *parseHealthCheckData(HealthCheck hc, int type = 1);
// --- Metrics data  ---

struct Metrics
{
  long timestamp=0;
  int wind_dir = -1;
  float wind_speed = 0;
  float wind_gust = 0;
  float rain_acc = 0;
  float humidity = 0;
  float temperature = 0;
  float pressure = 0;
};



extern char metricsjsonOutput[240];
extern char metricsCsvOutput[240];
extern char csvHeader[200];

void parseData(const Metrics& metric);