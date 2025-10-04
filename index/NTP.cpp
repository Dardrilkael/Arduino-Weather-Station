#include "NTP.h"
#include <time.h>

static constexpr unsigned long AT_TIMEOUT_SHORT = 5000;
static constexpr unsigned long AT_TIMEOUT_LONG  = 30000;

NTP::NTP(ModemAT& modemRef, const char* server, int tz) 
    : modem(modemRef), ntpServer(server), timezone(tz) 
    {
        if (tz < -48 || tz > 48)
        { // -12 to +12 hours in quarters
            Serial.println("Warning: Timezone out of range, using UTC");
            timezone = 0;
        }
        setNTPServer(ntpServer);
    }

bool NTP::setNTPServer(const char* server) {
    ntpServer = server;
    String cmd = String("AT+CNTP=\"") + server + "\"";  // Fixed: removed timezone
    return modem.sendAT(cmd.c_str(), "OK", AT_TIMEOUT_SHORT);
}

bool NTP::setTimezone(int tz) {
    timezone = tz;
    Serial.print("Timezone set to UTC");
    if (tz >= 0) Serial.print("+");
    Serial.println(tz / 4.0, 1);
    return true;
}

bool NTP::update() {
    Serial.println("Synchronizing time with NTP server...");
    if (modem.sendAT("AT+CNTP"), "+CNTP: 1", AT_TIMEOUT_LONG) {
        Serial.println("NTP time synchronization successful");
        return true;
    }
    
    Serial.println("NTP time synchronization failed");
    return false;
}

String NTP::getFormattedTime() {
    modem.clearSerialBuffer();
    modem.getSerial().println("AT+CCLK?");
    
    unsigned long start = millis();
    while (millis() - start < AT_TIMEOUT_SHORT) {
        if (modem.getSerial().available()) {
            String line = modem.getSerial().readStringUntil('\n');
            line.trim();
            if (line.startsWith("+CCLK:")) {
                int qs = line.indexOf('"');
                int qe = line.lastIndexOf('"');
                if (qs != -1 && qe != -1) {
                    lastTime = line.substring(qs + 1, qe);
                    return lastTime;
                }
            }
        }
    }
    return "";
}

bool NTP::getNTPStatus() {
    modem.clearSerialBuffer();
    modem.getSerial().println("AT+CNTP?");
    return modem.waitForResponse("+CNTP:", AT_TIMEOUT_SHORT);
}

time_t NTP::getEpochTime() {
    if (getFormattedTime().length() == 0) {
        return 0; // Return 0 if time is unavailable
    }
    return convertToTimestamp(lastTime.c_str());
}

time_t NTP::convertToTimestamp(const char* cclk) {
    struct tm t = {0};
    int year, month, day, hour, min, sec, tz_offset;
    char tz_sign;

    // Parse: "YY/MM/DD,HH:MM:SS+ZZ" or "YY/MM/DD,HH:MM:SS-ZZ"
    int parsed = sscanf(cclk, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d",
                       &year, &month, &day, &hour, &min, &sec, &tz_sign, &tz_offset);

    if (parsed != 8) {
        Serial.println("Failed to parse time string");
        return 0;
    }

    t.tm_year = 2000 + year - 1900;  // Years since 1900
    t.tm_mon  = month - 1;           // 0-11
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;

    // Convert to time_t (local time)
    time_t timestamp = mktime(&t);

    // Adjust for timezone (tz_offset is in 15-min units)
    int total_offset_minutes = tz_offset * 15;
    if (tz_sign == '+') {
        timestamp -= total_offset_minutes * 60; // Subtract to get UTC
    } else {
        timestamp += total_offset_minutes * 60; // Add to get UTC
    }

    return timestamp;
}