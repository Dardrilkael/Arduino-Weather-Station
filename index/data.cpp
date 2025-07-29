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
    const char *json_template = "{\"isWifiConnected\": %d, \"isMqttConnected\": %d, \"wifiDbmLevel\": %i, \"timestamp\": %i}";
    sprintf(hcJsonOutput, json_template,
            hc.isWifiConnected ? 1 : 0,
            hc.isMqttConnected ? 1 : 0,
            hc.wifiDbmLevel,
            hc.timestamp);
    return hcJsonOutput;
  }
}
// --- Metrics data  ---

Metrics Data;

char metricsjsonOutput[240]{0};
char metricsCsvOutput[240]{0};
char csvHeader[200]{0};

void parseData(const Metrics &metric)
{
  // parse measurements data to json
  const char *json_template = "{\"timestamp\": %i, \"temperatura\": %s, \"umidade_ar\": %s, \"velocidade_vento\": %.2f, \"rajada_vento\": %.2f, \"dir_vento\": %d, \"volume_chuva\": %.2f, \"pressao\": %s, \"uid\": \"%s\", \"identidade\": \"%s\"}";
  sprintf(metricsjsonOutput, json_template,
          metric.timestamp,
          isnan(metric.temperature) ? "null" : String(metric.temperature),
          isnan(metric.humidity) ? "null" : String(metric.humidity),
          metric.wind_speed,
          metric.wind_gust,
          metric.wind_dir,
          metric.rain_acc,
          metric.pressure == -1 ? "null" : String(metric.pressure),
          config.station_uid,
          config.station_name);

  // parse measurement data to csv
  const char *csv_template = "%i,%s,%s,%.2f,%.2f,%d,%.2f,%s,%s,%s\n";
  sprintf(metricsCsvOutput, csv_template,
          metric.timestamp,
          isnan(metric.temperature) ? "null" : String(metric.temperature),
          isnan(metric.humidity) ? "null" : String(metric.humidity),
          metric.wind_speed,
          metric.wind_gust,
          metric.wind_dir,
          metric.rain_acc,
          metric.pressure == -1 ? "null" : String(metric.pressure),
          config.station_uid,
          config.station_name);
}