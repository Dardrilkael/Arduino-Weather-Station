#pragma once
#define FIRMWARE_VERSION "3.0.167"
#define UPDATE_URL
#include <Arduino.h>
#include <functional>
class OTA
{
public:
  static bool update(const String &url,std::function<void(int)> onProgress = [](int){});
};
