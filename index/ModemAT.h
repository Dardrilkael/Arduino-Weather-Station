#pragma once
#include <HardwareSerial.h>
#include <Arduino.h>
#include "ATResponse.h"
#include <vector>
using URCHandler = std::function<bool(const String &)>;
class ModemAT
{

public:
    ModemAT(int rx, int tx, unsigned long baudRate);

    void begin();
    void end();
    bool setupNetwork(const char *apn, const char *user = nullptr, const char *pass = nullptr);

    String getResponse() const;

    // Basic AT command sender with timeout
    bool writeData(uint8_t* data, size_t size, unsigned long timeout);
    ATResponse sendCommand(const String &command, unsigned long timeout = 5000, unsigned long quietTimeout = 100,std::function<bool(const String&)> stopFn=nullptr);
    ATResponse sendCommand(const char *command, unsigned long timeout = 5000, unsigned long quietTimeout = 100,std::function<bool(const String&)> stopFn=nullptr);

    // Network related commands
    bool checkSim();

    inline HardwareSerial &getSerial() { return m_Serial; }
    void clearSerialBuffer();

    String getIpAdress()
    {
        return (sendCommand("AT+CGPADDR=1").as<IPAddressView>()).ip;
    }
    void pollURC();
    void handleURC(const String &line);

    void writeRaw(const uint8_t *buf, size_t len);
    template <typename T>
    // requires requires(const ATResponse& r) { T(r); } // ensure constructible
    T query(const char *cmd, unsigned long timeout = 1000)
    {
        ATResponse resp = sendCommand(cmd, timeout);
        return T(resp);
    }
    String m_URCBuffer;
    void addURCHandler(URCHandler h);

private:
    HardwareSerial m_Serial;
    int m_RxPin, m_TxPin;
    unsigned long m_BaudRate;
    String m_Buffer;
    int retryDelay = 1000; // milliseconds
    // URC

    std::vector<URCHandler> urcHandlers;
};