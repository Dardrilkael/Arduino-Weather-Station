#pragma once

#include "ModemAT.h"

class NTP {
private:
    ModemAT& modem;
    const char* ntpServer;
    int timezone;
    String lastTime;
    
    time_t convertToTimestamp(const char* cclk);

public:
   
    NTP(ModemAT& modemRef, const char* server = "pool.ntp.org", int tz = 0);
    bool setNTPServer(const char* server);
    bool setTimezone(int tz);
    bool update();
    String getFormattedTime();
    bool getNTPStatus();
    time_t getEpochTime();  // Changed to time_t
};

