#include "data.h"
#include <stdio.h>
#include <cmath>
#include <Arduino.h>
Config config;
const char *configFileName = "/config.txt";

char hcJsonOutput[240]{0};
char hcCsvOutput[240]{0};
const char *parseHealthCheckData(HealthCheck hc, int type)
{
  if (type == 1)
  {
    const char *hc_dto = "%s,%d,%d,%i,%i,%i";
    sprintf(hcCsvOutput, hc_dto,
            hc.softwareVersion,
            hc.isWifiConnected ? 1 : 0,
            hc.isMqttConnected ? 1 : 0,
            hc.wifiDbmLevel,
            hc.timestamp,
            hc.timeRemaining);
    return hcCsvOutput;
  }
  else
  {
    const char *json_template = "{\"isWifiConnected\": %d, \"isMqttConnected\": %d, \"wifiDbmLevel\": %i, \"timestamp\": %lld}";
    sprintf(hcJsonOutput, json_template,
            hc.isWifiConnected ? 1 : 0,
            hc.isMqttConnected ? 1 : 0,
            hc.wifiDbmLevel,
            (long long)hc.timestamp);
    return hcJsonOutput;
  }
}
// --- Metrics data  ---

Metrics Data;

char metricsjsonOutput[240]{0};
char metricsCsvOutput[240]{0};
char csvHeader[200]{0};

// Helper: writes a float as "null" or a decimal string into buf.
// Uses dtostrf to avoid temporary String objects.
static const char *floatOrNull(float val, char *buf, int bufSize)
{
  if (isnan(val))
    return "null";
  dtostrf(val, 1, 2, buf); // width=1 (auto), 2 decimal places
  return buf;
}

void parseData(const Metrics &metric)
{
  // Stable char buffers — no temporary String objects
  char tempBuf[16], humBuf[16], presBuf[16];

  const char *tempStr = floatOrNull(metric.temperature, tempBuf, sizeof(tempBuf));
  const char *humStr  = floatOrNull(metric.humidity,    humBuf,  sizeof(humBuf));
  // Fix #5: use isnan() consistently for pressure (was: == -1 in CSV branch)
  const char *presStr = floatOrNull(metric.pressure,    presBuf, sizeof(presBuf));

  // parse measurements data to json
  const char *json_template = "{\"timestamp\": %lld, \"temperatura\": %s, \"umidade_ar\": %s, \"velocidade_vento\": %.2f, \"rajada_vento\": %.2f, \"dir_vento\": %d, \"volume_chuva\": %.2f, \"pressao\": %s, \"uid\": \"%s\", \"identidade\": \"%s\"}";
  sprintf(metricsjsonOutput, json_template,
          (long long)metric.timestamp,
          tempStr,
          humStr,
          metric.wind_speed,
          metric.wind_gust,
          metric.wind_dir,
          metric.rain_acc,
          presStr,
          config.station_uid,
          config.station_name);

  // parse measurement data to csv
  const char *csv_template = "%lld,%s,%s,%.2f,%.2f,%d,%.2f,%s,%s,%s\n";
  sprintf(metricsCsvOutput, csv_template,
          (long long)metric.timestamp,
          tempStr,
          humStr,
          metric.wind_speed,
          metric.wind_gust,
          metric.wind_dir,
          metric.rain_acc,
          presStr,
          config.station_uid,
          config.station_name);
}