#pragma once
#include <WiFi.h>
#include <string>

class WifiManager {
public:
    WifiManager();

    int setupWifi(const char* contextName, const char* ssid, const char* password);
    void checkWifiReconnection();

private:
    WiFiClient wifiClient;
    
    unsigned long lastReconnectAttempt;
    int retryCount;

    // 🔒 SSID e senha armazenados internamente
    std::string savedSSID;
    std::string savedPassword;
};


