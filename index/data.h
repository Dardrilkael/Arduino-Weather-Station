#pragma once
#include <time.h>  // for time_t
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
  time_t timestamp;   // Fix: was int — truncates 64-bit time_t on ESP-IDF 5.x
  bool isWifiConnected;
  bool isMqttConnected;
  int wifiDbmLevel;
  int timeRemaining;
};

extern char hcJsonOutput[240];
extern char hcCsvOutput[240];
// type=1 → CSV into hcCsvOutput, else → JSON into hcJsonOutput.
// Returns pointer to whichever buffer was written.
const char *parseHealthCheckData(HealthCheck hc, int type = 1);

// --- Metrics data  ---

struct Metrics
{
  time_t timestamp = 0;  // Fix: was long — truncates 64-bit time_t on ESP-IDF 5.x
  int wind_dir = -1;
  float wind_speed = 0;
  float wind_gust = 0;
  float rain_acc = 0;
  float humidity = 0;
  float temperature = 0;
  float pressure = 0;
};

// Fix #8: was writing to hidden globals metricsjsonOutput / metricsCsvOutput.
// Caller now passes explicit output buffers — no silent side effects,
// safe to call from multiple contexts (BLE callback, MQTT callback, loop).
void parseData(const Metrics &metric,
               char *jsonOut, size_t jsonSize,
               char *csvOut,  size_t csvSize);

struct Timer
{
  unsigned long lastTime;
  unsigned long interval;

  Timer(unsigned long intervalMs) : lastTime(0), interval(intervalMs) {}

  bool check(unsigned long now)
  {
    if (now - lastTime >= interval)
    {
      lastTime = now;
      return true;
    }
    return false;
  }
};