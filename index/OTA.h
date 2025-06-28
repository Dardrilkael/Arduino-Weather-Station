#pragma once
#define FIRMWARE_VERSION "3.0.159"
#define UPDATE_URL 
#include <Arduino.h>

class OTA {
public:
  static bool update(const String& url);
};
