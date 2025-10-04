// ModemAT.h - Safe AT command handler for A7670/A7672 modules
#ifndef MODEM_AT_H
#define MODEM_AT_H

#include <HardwareSerial.h>
#include <WString.h>
#include <functional>

class ModemAT {
public:
    ModemAT(int rxPin = 16, int txPin = 17, long baudRate = 115200);
    ~ModemAT();

    bool begin();
    bool end();
    bool setupNetwork();

    // Basic AT commands
    bool sendAT(const char* cmd, const char* expected = "OK", int timeoutMs = 10000);
    bool sendATWithData(const char* cmd, const char* data, const char* expected = "OK", int timeoutMs = 10000);
    
    // Network functions
    bool checkSim();
    bool checkNetworkRegistration(int maxRetries = 5);
    bool configurePDPContext(const char* apn);
    bool activatePDPContext();
    bool getIPAddress();

    bool waitForResponse(const char* expected, unsigned long timeoutMs);
    
    // Utility functions
    bool executeWithRetry(std::function<bool()> operation, const char* operationName, int maxRetries = 2);
    void setRetryDelay(unsigned long delayMs) { retryDelay = delayMs; }
    bool isConnected() { return modemConnected; }
    void clearSerialBuffer();
    // Raw serial access (for advanced use)
    HardwareSerial& getSerial() { return serialAT; }

private:
    HardwareSerial serialAT;
    int rxPin, txPin;
    long baudRate;
    bool modemConnected;
    unsigned long retryDelay;
    
    String readResponse(unsigned long timeoutMs);
    bool waitForPrompt(unsigned long timeoutMs);
};

#endif