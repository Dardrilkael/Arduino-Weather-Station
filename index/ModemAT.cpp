// ModemAT.cpp - Safe AT command handler implementation
#include "ModemAT.h"
#include <Arduino.h>
#include "settings.h"
constexpr const char* apn = "zap.vivo.com.br";
ModemAT::ModemAT(int rxPin, int txPin, long baudRate) 
    : rxPin(rxPin), txPin(txPin), baudRate(baudRate), 
      modemConnected(false), retryDelay(2000), serialAT(1) {
}

ModemAT::~ModemAT() {
    end();
}

bool ModemAT::begin() {
    serialAT.begin(baudRate, SERIAL_8N1, rxPin, txPin);
    delay(2000);
    
    modemConnected = executeWithRetry([this]() {
        return sendAT("AT", "OK", 5000);
    }, "Modem communication");
    
    return modemConnected;
}

bool ModemAT::end() {
    if (modemConnected) {
        serialAT.end();
        modemConnected = false;
    }
    return true;
}

bool ModemAT::setupNetwork() {
    bool success = true;

    // Initialize modem
    if (!begin()) {
        Serial.println("Failed to initialize modem");
        return false;
    }

    // Check SIM
    success &= checkSim();

    // Check network registration
    if (success) success &= checkNetworkRegistration(5);

    // Configure PDP context
    if (success) success &= configurePDPContext(apn);

    // Activate PDP context
    if (success) success &= activatePDPContext();

    // Get IP address
    if (success) success &= getIPAddress();

    return success;
}


bool ModemAT::sendAT(const char* cmd, const char* expected, int timeoutMs) {
    Serial.print(">> "); Serial.println(cmd);
    serialAT.println(cmd);
    
    String response = "";
    unsigned long start = millis();
    
    while (millis() - start < timeoutMs) {
        if (serialAT.available()) {
            char c = serialAT.read();
            Serial.write(c);
            response += c;
            
            if (response.indexOf(expected) != -1) {
                return true;
            }
            if (response.indexOf("ERROR") != -1) {
                Serial.println("*** ERROR response received ***");
                return false;
            }
        }
    }
    
    Serial.println("*** TIMEOUT waiting for response ***");
    return false;
}

bool ModemAT::sendATWithData(const char* cmd, const char* data, const char* expected, int timeoutMs) {
    Serial.print(">> "); Serial.println(cmd);


    serialAT.println(cmd);
    if (!waitForPrompt(timeoutMs)) {
        Serial.println("*** TIMEOUT waiting for data prompt ***");
        return false;
    }

    // Send data and wait for final response
    Serial.print(">> [DATA] "); Serial.println(data);
    serialAT.println(data);
    
    String response = readResponse(timeoutMs);
    return (response.indexOf(expected) != -1);
}

String ModemAT::readResponse(unsigned long timeoutMs) {
    String response = "";
    unsigned long start = millis();
    
    while (millis() - start < timeoutMs) {
        if (serialAT.available()) {
            char c = serialAT.read();
            Serial.write(c);
            response += c;
            
            if (response.indexOf("ERROR") != -1 || 
                response.indexOf("OK") != -1 ||
                response.indexOf("+CMQTTSTART: 0") != -1 ||
                response.indexOf("+CMQTTCONNECT: 0,0") != -1) {
                break;
            }
        }
    }
    return response;
}

bool ModemAT::waitForPrompt(unsigned long timeoutMs) {
    unsigned long start = millis();
    String response = "";
    
    while (millis() - start < timeoutMs) {
        if (serialAT.available()) {
            char c = serialAT.read();
            Serial.write(c);
            response += c;
            
            if (c == '>') {
                return true;
            }
            if (response.indexOf("ERROR") != -1) {
                Serial.printf("O que foi aqui %s",response.c_str());
                return false;
            }
        }
    }
    return false;
}

bool ModemAT::executeWithRetry(std::function<bool()> operation, const char* operationName, int maxRetries) {
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        Serial.print("Attempt "); Serial.print(attempt); Serial.print("/"); Serial.print(maxRetries); 
        Serial.print(": "); Serial.println(operationName);
        
        if (operation()) {
            Serial.print("✓ "); Serial.println(operationName);
            return true;
        }
        
        if (attempt < maxRetries) {
            Serial.print("Retrying in "); Serial.print(retryDelay/1000); Serial.println(" seconds...");
            delay(retryDelay);
        }
    }
    
    Serial.print("✗ Failed: "); Serial.println(operationName);
    return false;
}

bool ModemAT::checkSim() {
    return executeWithRetry([this]() {
        return sendAT("AT+CPIN?", "READY", 10000);
    }, "SIM check");
}

bool ModemAT::checkNetworkRegistration(int maxRetries) {
    return executeWithRetry([this]() {
        // Accept either home network (1) or roaming (5) registration
        return sendAT("AT+CREG?", "+CREG: 0,1", 5000) || 
               sendAT("AT+CREG?", "+CREG: 0,5", 5000);
    }, "Network registration", maxRetries);
}

bool ModemAT::configurePDPContext(const char* apn) {
    String cmd = String("AT+CGDCONT=1,\"IP\",\"") + apn + "\"";
    return sendAT(cmd.c_str(), "OK");
}

bool ModemAT::activatePDPContext() {
    return sendAT("AT+CGACT=1,1", "OK", 15000);
}

bool ModemAT::getIPAddress() {
    return sendAT("AT+CGPADDR=1", "OK", 5000);
}


void ModemAT::clearSerialBuffer() {
        while (getSerial().available()) {
            getSerial().read();
            delay(1);
        }
    }


bool ModemAT::waitForResponse(const char* expected, unsigned long timeoutMs) {
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (serialAT.available()) {
            String line = serialAT.readStringUntil('\n');
            line.trim();
            if (line.indexOf(expected) >= 0) {
                return true;
            }
            if (line == "ERROR") {
                return false;
            }
        }
    }
    return false;
}