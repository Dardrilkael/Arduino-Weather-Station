#include "TimeManager.h"
#include <string.h>

#include "esp_sntp.h"

#if ESP_IDF_VERSION_MAJOR >= 5
#include "esp_netif_sntp.h"
#endif

//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"

//#include <Arduino.h>
static bool sntpSynced = false;
extern "C" void time_sync_notification_cb(struct timeval *tv) {
    sntpSynced = true;
}

struct
{
  time_t now;
  char strftime_buf[64];
  struct tm timeinfo;
} timeData;


uint64_t nowMs() {
    return esp_timer_get_time() / 1000;
} 
void TimeManager::Init(const char* tz) 
{
  //esp_netif_init();
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  config.sync_cb = time_sync_notification_cb;
  esp_netif_sntp_init(&config);
  esp_netif_sntp_start();
#else
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov");
sntp_setservername(2, "time.google.com");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
#endif

  setenv("TZ", tz, 1);
  tzset();

  unsigned long start = nowMs();
      while (!sntpSynced) {
          vTaskDelay(pdMS_TO_TICKS(100));

      }
  update();
}

void TimeManager::update()
{
  time(&timeData.now);
  localtime_r(&timeData.now, &timeData.timeinfo);
}

time_t TimeManager::getTimestamp()
{
  return timeData.now;
}

const char* TimeManager::getFormatted(TimeFormat type)
{
    switch (type)
    {
        // "2025-11-24 13:45:22"
        case FMT_FULL:
            strftime(timeData.strftime_buf, sizeof(timeData.strftime_buf),
                     "%Y-%m-%d %H:%M:%S", &timeData.timeinfo);
            break;

        // "24-11-2025"
        case FMT_DATE:
            strftime(timeData.strftime_buf, sizeof(timeData.strftime_buf),
                     "%d-%m-%Y", &timeData.timeinfo);
            break;

        // "13:45:22"
        case FMT_TIME:
            strftime(timeData.strftime_buf, sizeof(timeData.strftime_buf),
                     "%H:%M:%S", &timeData.timeinfo);
            break;

        // "Day 328 of 2025"
        case FMT_YEAR_DAY:
        {
            // tm_yday ranges from 0–365 → +1 for human display
            int dayOfYear = timeData.timeinfo.tm_yday + 1;
            snprintf(timeData.strftime_buf, sizeof(timeData.strftime_buf),
                     "Day %d of %d",
                     dayOfYear, timeData.timeinfo.tm_year + 1900);
            break;
        }

        default:
            strncpy(timeData.strftime_buf, "Invalid format",
                    sizeof(timeData.strftime_buf));
            timeData.strftime_buf[sizeof(timeData.strftime_buf) - 1] = '\0';
            break;
    }

    return timeData.strftime_buf;
}



int TimeManager::year()   { return timeData.timeinfo.tm_year + 1900; }
int TimeManager::month()  { return timeData.timeinfo.tm_mon + 1; }
int TimeManager::day()    { return timeData.timeinfo.tm_mday; }
int TimeManager::hour()   { return timeData.timeinfo.tm_hour; }
int TimeManager::minute() { return timeData.timeinfo.tm_min; }
int TimeManager::second() { return timeData.timeinfo.tm_sec; }