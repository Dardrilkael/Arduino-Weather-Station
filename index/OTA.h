#pragma once
#define FIRMWARE_VERSION "3.0.169"
#define UPDATE_URL
#include <Arduino.h>
#include <functional>

struct OTA_Result 
{
    bool success;
    std::string error;
    explicit OTA_Result(bool s = false, std::string e = "")
        : success(s), error(std::move(e)) {}
    operator bool() const { return success; }
};

class OTA
{
public:
  static OTA_Result update(const String &url,std::function<void(int)> onProgress = [](int){});
};
