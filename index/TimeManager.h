#pragma once
#include <time.h>

enum TimeFormat {
    FMT_FULL,      // "2025-11-24 13:45:22"
    FMT_DATE,      // "24-11-2025"
    FMT_TIME,      // "13:45:22"
    FMT_YEAR_DAY,  // "Day 328 of 2025"
    FMT_CUSTOM     // Custom strftime() pattern
};

class TimeManager
{
  public:
  static void Init(const char* tz = "GMT");
  static void update();
  static time_t getTimestamp(); 
  static const char* getFormatted(TimeFormat type);
  static bool syncFromModemCCLK(const char* rawLine);
  static int year();
  static int month();
  static int day(); 
  static int hour(); 
  static int minute();
  static int second();


  
};