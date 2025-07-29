#pragma once
#define FIRMWARE_VERSION "3.0.163"
#define UPDATE_URL
#include <Arduino.h>

class OTA
{
public:
  static bool update(const String &url);
};
