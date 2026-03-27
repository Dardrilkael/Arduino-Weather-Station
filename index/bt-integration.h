#pragma once
#include <string>

// Station BLE service & characteristics
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONFIGURATION_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define HEALTH_CHECK_UUID "7c4c8722-8b05-4cca-b5d2-05ec864f90ee"

// ADD THIS — shared mailbox between BLE task and main loop
struct BLECommand {
    volatile bool pending = false;
    char characteristicUid[64]{0};
    char content[512]{0};         // adjust size to your max BLE payload
};
extern BLECommand blePendingCommand;

class BLE
{
public:
  static void Init(const char *boardName);
  static void updateValue(const std::string & characteristicId, const std::string &newValue);
  static bool stop();
};
