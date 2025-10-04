// MQTT.cpp - MQTT client implementation for A7670/A7672
#include "MQTT.h"
#include <Arduino.h>

MQTT::MQTT(ModemAT& modemRef) 
    : modem(modemRef), mqttConnected(false), publishInProgress(false),
      bufferSize(512), expectedPayloadLength(0), currentPayloadLength(0) {
}

MQTT::~MQTT() {
    disconnect();
}

bool MQTT::setupMqtt(const char* contextName, const char* mqtt_server, int mqtt_port, 
                     const char* mqtt_username, const char* mqtt_password, const char* mqtt_topic) {
    
    // First, check if we're already connected
    if (checkExistingConnection()) {
        Serial.println("✓ MQTT connection already established, reusing...");
        mqttConnected = true;
        return true;
    }
    
    // Check if client is already acquired but not connected
    if (checkClientAcquired()) {
        Serial.println("✓ MQTT client already acquired, connecting to broker...");
        String brokerURL = String("tcp://") + mqtt_server + ":" + String(mqtt_port);
        if (connectToBroker(brokerURL.c_str(), 60, true, mqtt_username, mqtt_password)) {
            mqttConnected = true;
            Serial.println("✓ Successfully connected with existing client");
            return true;
        }
    }
    
    // If we get here, we need full setup
    Serial.println("Performing full MQTT setup...");
    
    // Initialize MQTT service (gently - don't force restart if already running)
    if (!initializeMQTTService()) {
        Serial.println("Failed to initialize MQTT service");
        return false;
    }
    
    // Acquire client
    if (!acquireClient(contextName)) {
        Serial.println("Failed to acquire MQTT client");
        return false;
    }
    
    // Connect to broker
    String brokerURL = String("tcp://") + mqtt_server + ":" + String(mqtt_port);
    if (!connectToBroker(brokerURL.c_str(), 60, true, mqtt_username, mqtt_password)) {
        Serial.println("Failed to connect to MQTT broker");
        return false;
    }
    
    mqttConnected = true;
    Serial.println("✓ Full MQTT setup completed successfully");
    return true;
}

bool MQTT::connectMqtt(const char* contextName, const char* mqtt_username, const char* mqtt_password, const char* mqtt_topic) {
    if (mqttConnected) {
        return true;
    }
    
    // Try to reuse existing connection first
    if (checkExistingConnection()) {
        mqttConnected = true;
        return true;
    }
    
    // Fall back to setup
    return setupMqtt(contextName, "default_server", 1883, mqtt_username, mqtt_password, mqtt_topic);
}

bool MQTT::initializeMQTTService() {
    return modem.executeWithRetry([this]() {
        // First try to start the service
        if (modem.sendAT("AT+CMQTTSTART", "+CMQTTSTART: 0", 10000)) {
            return true;
        }
        
        // If that fails with ERROR, service might already be running
        // Try a gentle check by sending a harmless MQTT command
        Serial.println("Service might already be running, checking...");
        if (checkClientAcquired()) {
            Serial.println("✓ MQTT service is already running");
            return true;
        }
        
        // If both fail, try one more time to start
        Serial.println("Retrying MQTT service start...");
        return modem.sendAT("AT+CMQTTSTART", "OK", 10000); // Accept OK as success too
    }, "MQTT service initialization", 2); // Only 2 retries
}

bool MQTT::acquireClient(const char* clientID) {
    return modem.executeWithRetry([this, clientID]() {
        String cmd = String("AT+CMQTTACCQ=0,\"") + clientID + "\",0";
        
        if (modem.sendAT(cmd.c_str(), "OK")) {
            return true;
        }
        
        // If acquisition fails, check if client is already acquired
        Serial.println("Client acquisition failed, checking if already acquired...");
        if (checkClientAcquired()) {
            Serial.println("✓ Client already acquired, reusing...");
            return true;
        }
        
        // If not acquired, try gentle cleanup and retry
        Serial.println("Performing gentle cleanup...");
        modem.sendAT("AT+CMQTTREL=0", "OK", 5000);
        delay(1000);
        return modem.sendAT(cmd.c_str(), "OK");
    }, "MQTT client acquisition", 2);
}

bool MQTT::connectToBroker(const char* brokerURL, int keepalive, bool cleansession, 
                          const char* username, const char* password) {
    return modem.executeWithRetry([this, brokerURL, keepalive, cleansession, username, password]() {
        // Use the two-step connection method (command + data)
        String connectData = String(brokerURL) + "," + username + "," + password;
       // String cmd = String("AT+CMQTTCONNECT=0,\"tcp://") + brokerURL + "\",60,1,\"" + username + "\",\"" + password + "\"";
        String cmd = String("AT+CMQTTCONNECT=0,\"") + brokerURL + "\"," + String(keepalive) + "," + String(cleansession ? 1 : 0);

        if (username != nullptr && strlen(username) > 0) {
            cmd += ",\"" + String(username) + "\"";
            if (password != nullptr && strlen(password) > 0) {
                cmd += ",\"" + String(password) + "\"";
            }
        }
 
        if (modem.sendAT(cmd.c_str(), "+CMQTTCONNECT: 0,0", 15000)) {
            delay(100);
            return true;
        }
        delay(100);
        // Check if already connected
        return checkConnectionStatus();
    }, "MQTT broker connection");
}

bool MQTT::checkExistingConnection() {
    // Check both client acquisition and connection status
    return checkClientAcquired() && checkConnectionStatus();
}

bool MQTT::checkClientAcquired() {
    String response = "";
    modem.getSerial().println("AT+CMQTTACCQ?");
    
    unsigned long start = millis();
    while (millis() - start < 3000) {
        if (modem.getSerial().available()) {
            String line = modem.getSerial().readStringUntil('\n');
            line.trim();
            
            // Check if this is a client 0 entry AND has a non-empty client ID
            if (line.startsWith("+CMQTTACCQ: 0,")) {
                // Extract the content between quotes
                int firstQuote = line.indexOf('"');
                int secondQuote = line.indexOf('"', firstQuote + 1);
                
                if (firstQuote != -1 && secondQuote != -1) {
                    String clientId = line.substring(firstQuote + 1, secondQuote);
                    // Return true if client ID is not empty
                    if (clientId.length() > 0) {
                        return true;
                    }
                }
            }
            
            if (line == "OK") {
                break;
            }
        }
    }
    return false;
}

bool MQTT::checkConnectionStatus() {
    String response = "";
    modem.getSerial().println("AT+CMQTTCONNECT?");
    
    bool connected = false;
    unsigned long start = millis();
    
    while (millis() - start < 5000) {
        if (modem.getSerial().available()) {
            String line = modem.getSerial().readStringUntil('\n');
            line.trim();
            
            Serial.print("CONN_CHECK: ");
            Serial.println(line);
            
            // Check for connection status indicators
            if (line.startsWith("+CMQTTCONNECT: 0,")) {
                // Client 0 has connection parameters - assume connected
                connected = true;
            } else if (line == "+CMQTTCONNECT: 1") {
                // Simple state format - 1 means connected
                connected = true;
            }
            
            if (line == "OK") {
                break;
            }
        }
    }
    
    Serial.print("Connection status: ");
    Serial.println(connected ? "CONNECTED" : "DISCONNECTED");
    return connected;
}

bool MQTT::publish(const char* topic, const char* payload, bool retained) {
    if (!mqttConnected) {
        Serial.println("Not connected, cannot publish");
        return false;
    }
    
    bool success = true;
    
    // Set topic
    success &= modem.executeWithRetry([this, topic]() {
        String cmd = String("AT+CMQTTTOPIC=0,") + String(strlen(topic));
        return modem.sendATWithData(cmd.c_str(), topic);
    }, "MQTT topic set");
    
    // Set payload
    if (success) {
        success &= modem.executeWithRetry([this, payload]() {
            String cmd = String("AT+CMQTTPAYLOAD=0,") + String(strlen(payload));
            return modem.sendATWithData(cmd.c_str(), payload);
        }, "MQTT payload set");
    }
    
    // Publish
    if (success) {
        String pubCmd = String("AT+CMQTTPUB=0,") + String(retained ? "1" : "0") + ",60";
        success &= modem.executeWithRetry([this, pubCmd]() {
            return modem.sendAT(pubCmd.c_str(), "+CMQTTPUB: 0,0", 10000);
        }, "MQTT publish");
    }
    
    if (success) {
        Serial.println("✓ Publish successful");
    } else {
        Serial.println("✗ Publish failed");
    }
    
    return success;
}

bool MQTT::beginPublish(const char* topic, unsigned int plength, bool retained) {
    if (!mqttConnected || publishInProgress) return false;
    
    currentTopic = topic;
    expectedPayloadLength = plength;
    currentPayloadLength = 0;
    publishInProgress = true;
    
    String cmd = String("AT+CMQTTTOPIC=0,") + String(strlen(topic));
    return modem.sendATWithData(cmd.c_str(), topic);
}

int MQTT::endPublish() {
    if (!publishInProgress) return 0;
    
    publishInProgress = false;
    
    if (modem.sendAT("AT+CMQTTPUB=0,0,60", "+CMQTTPUB: 0,0", 10000)) {
        return currentPayloadLength;
    }
    
    return 0;
}

int MQTT::write(char data) {
    if (!publishInProgress) return 0;
    return 0;
}

int MQTT::write(const unsigned char* buffer, int size) {
    if (!publishInProgress) return 0;
    
    String payload;
    for (int i = 0; i < size; i++) {
        payload += (char)buffer[i];
    }
    
    String cmd = String("AT+CMQTTPAYLOAD=0,") + String(size);
    if (modem.sendATWithData(cmd.c_str(), payload.c_str())) {
        currentPayloadLength += size;
        return size;
    }
    
    return 0;
}


bool MQTT::subscribe(const char* topic) {
    if (!mqttConnected) {
        Serial.println("Not connected to MQTT broker");
        return false;
    }
    
    return modem.executeWithRetry([this, topic]() {
        modem.clearSerialBuffer();
        
        Serial.print("Attempting to subscribe to: ");
        Serial.println(topic);
        
        // Send command
        String cmd = String("AT+CMQTTSUB=0,") + String(strlen(topic)) + ",0";
        Serial.print(">> ");
        Serial.println(cmd);
        modem.getSerial().println(cmd);
        
        // Wait for ">" prompt
        unsigned long start = millis();
        bool gotPrompt = false;
        while (millis() - start < 5000) {
            if (modem.getSerial().available()) {
                String line = modem.getSerial().readStringUntil('\n');
                line.trim();
                Serial.print("<< ");
                Serial.println(line);
                
                if (line == ">") {
                    gotPrompt = true;
                    break;
                }
                if (line == "ERROR") {
                    Serial.println("ERROR before prompt");
                    return false;
                }
            }
        }
        
        if (!gotPrompt) {
            Serial.println("No prompt received");
            return false;
        }
        
        // Send topic data
        Serial.print(">>> [TOPIC] ");
        Serial.println(topic);
        modem.getSerial().println(topic);
        
        // Wait for response - we need both OK and +CMQTTSUB: 0,0
        start = millis();
        bool gotOK = false;
        
        while (millis() - start < 15000) {
            if (modem.getSerial().available()) {
                String line = modem.getSerial().readStringUntil('\n');
                line.trim();
                Serial.print("<< ");
                Serial.println(line);
                
                if (line == "OK") {
                    gotOK = true;
                    Serial.println("Got OK, waiting for +CMQTTSUB...");
                } 
                else if (line.startsWith("+CMQTTSUB: 0,0")) {
                    Serial.println("Got +CMQTTSUB: 0,0 - SUCCESS!");
                    return true;
                }
                else if (line.startsWith("+CMQTTSUB: 0,")) {
                    Serial.print("Subscribe failed with error: ");
                    Serial.println(line);
                    return false;
                }
                else if (line == "ERROR") {
                    Serial.println("Got ERROR");
                    return false;
                }
            }
        }
        
        Serial.print("Timeout - Got OK: ");
        Serial.println(gotOK ? "YES" : "NO");
        return false;
        
    }, "MQTT subscribe", 2);
}

bool MQTT::unsubscribe(const char* topic) {
    if (!mqttConnected) return false;
    
    return modem.executeWithRetry([this, topic]() {
        String cmd = String("AT+CMQTTUNSUB=0,") + String(strlen(topic));
        return modem.sendATWithData(cmd.c_str(), topic, "+CMQTTUNSUB: 0,0", 10000);
    }, "MQTT unsubscribe");
}

void MQTT::setCallback(MQTTCallback callback) {
    messageCallback = callback;
}

bool MQTT::loopMqtt() {
    if (!mqttConnected) return false;
    
    // Process incoming messages
    processIncomingMessages();
    
    // Simple connection check - if we can still send commands, we're probably connected
    return true;
}

bool MQTT::disconnect() {
    bool success = true;
    
    if (mqttConnected) {
        success &= modem.sendAT("AT+CMQTTDISC=0,120", "OK", 10000);
        delay(1000);
    }
    
    success &= modem.sendAT("AT+CMQTTREL=0", "OK", 5000);
    delay(1000);
    
    mqttConnected = false;
    return success;
}

bool MQTT::cleanup() {
    Serial.println("Performing MQTT cleanup...");
    bool success = true;
    success &= disconnect();
    success &= modem.sendAT("AT+CMQTTSTOP", "OK", 10000);
    delay(2000);
    return success;
}

// Rest of the methods remain the same...
void MQTT::processIncomingMessages() {
    static String responseBuffer = "";
    
    while (modem.getSerial().available()) {
        char c = modem.getSerial().read();
        Serial.write(c); // Echo for debugging
        
        responseBuffer += c;
        
        // Check for complete lines
        if (c == '\n') {
            String line = responseBuffer;
            line.trim();
            responseBuffer = "";
            
            processLine(line);
        }
    }
   
    // Handle timeout
    if (rxState != RxState::IDLE && millis() > rxTimeout) {
        Serial.println("RX timeout, resetting state machine");
        resetRxState();
    }
}












void MQTT::processLine(const String& line) {
    Serial.print("STATE: ");
    Serial.print(static_cast<int>(rxState));
    Serial.print(" LINE: ");
    Serial.println(line);
    
    switch (rxState) {
        case RxState::IDLE:
            if (line.startsWith("+CMQTTRXSTART:")) {
                handleRxStart(line);
            }
            break;
            
        case RxState::WAITING_FOR_TOPIC:
            if (line.startsWith("+CMQTTRXTOPIC:")) {
                handleRxTopic(line);
            } else {
                Serial.println("Unexpected response while waiting for topic");
                resetRxState();
            }
            break;
            
        case RxState::WAITING_FOR_TOPIC_DATA:
            // This is the actual topic data line
            handleRxTopicData(line);
            break;
            
        case RxState::WAITING_FOR_PAYLOAD_HEADER:
            if (line.startsWith("+CMQTTRXPAYLOAD:")) {
                handleRxPayloadHeader(line);
            } else {
                Serial.println("Unexpected response while waiting for payload header");
                resetRxState();
            }
            break;
            
        case RxState::WAITING_FOR_PAYLOAD_DATA:
            handleRxPayloadData(line);
            break;
            
        case RxState::WAITING_FOR_END:
            if (line.startsWith("+CMQTTRXEND:")) {
                handleRxEnd(line);
            } else {
                Serial.println("Unexpected response while waiting for RX end");
                resetRxState();
            }
            break;
    }
}

void MQTT::handleRxStart(const String& line) {
    // Format: +CMQTTRXSTART: <client_index>,<topic_length>,<payload_length>
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    
    if (firstComma != -1 && secondComma != -1) {
        String topicLenStr = line.substring(firstComma + 1, secondComma);
        String payloadLenStr = line.substring(secondComma + 1);
        
        expectedTopicLength = topicLenStr.toInt();
        expectedPayloadLength = payloadLenStr.toInt();
        currentRxTopic = "";
        currentRxPayload = "";
        
        rxState = RxState::WAITING_FOR_TOPIC;
        rxTimeout = millis() + RX_TIMEOUT_MS;
        
        Serial.print("RX START received, topic length: ");
        Serial.print(expectedTopicLength);
        Serial.print(", payload length: ");
        Serial.println(expectedPayloadLength);
    } else {
        Serial.println("Invalid RX START format");
        resetRxState();
    }
}

void MQTT::handleRxTopic(const String& line) {
    // Format: +CMQTTRXTOPIC: <client_index>,<topic_length>
    // The actual topic data comes on the next line
    int firstComma = line.indexOf(',');
    if (firstComma != -1) {
        String topicLenStr = line.substring(firstComma + 1);
        
        // Verify the length matches what we got from RXSTART
        int topicLength = topicLenStr.toInt();
        if (topicLength != expectedTopicLength) {
            Serial.print("Topic length mismatch! Expected: ");
            Serial.print(expectedTopicLength);
            Serial.print(", Got: ");
            Serial.println(topicLength);
        }
        
        // Now wait for the actual topic data on the next line
        rxState = RxState::WAITING_FOR_TOPIC_DATA;
        rxTimeout = millis() + RX_TIMEOUT_MS;
        
        Serial.println("Waiting for topic data...");
    } else {
        Serial.println("Invalid RX TOPIC format");
        resetRxState();
    }
}

void MQTT::handleRxTopicData(const String& line) {
    // This is the actual topic data line
    currentRxTopic = line;
    currentRxTopic.trim();
    
    Serial.print("Topic data received: ");
    Serial.println(currentRxTopic);
    
    // Verify length
    if (currentRxTopic.length() != expectedTopicLength) {
        Serial.print("Topic length mismatch! Expected: ");
        Serial.print(expectedTopicLength);
        Serial.print(", Actual: ");
        Serial.println(currentRxTopic.length());
    }
    
    // Move to waiting for payload header
    rxState = RxState::WAITING_FOR_PAYLOAD_HEADER;
    rxTimeout = millis() + RX_TIMEOUT_MS;
}

void MQTT::handleRxPayloadHeader(const String& line) {
    // Format: +CMQTTRXPAYLOAD: <client_index>,<payload_length>
    int firstComma = line.indexOf(',');
    if (firstComma != -1) {
        String payloadLenStr = line.substring(firstComma + 1);
        currentRxPayloadLength = payloadLenStr.toInt();
        currentRxPayload = "";
        
        // Verify the length matches what we got from RXSTART
        if (currentRxPayloadLength != expectedPayloadLength) {
            Serial.print("Payload length mismatch! Expected: ");
            Serial.print(expectedPayloadLength);
            Serial.print(", Got: ");
            Serial.println(currentRxPayloadLength);
        }
        
        rxState = RxState::WAITING_FOR_PAYLOAD_DATA;
        rxTimeout = millis() + RX_TIMEOUT_MS;
        
        Serial.print("Payload header received, length: ");
        Serial.println(currentRxPayloadLength);
        
        // If payload length is 0, move directly to end state
        if (currentRxPayloadLength == 0) {
            rxState = RxState::WAITING_FOR_END;
        }
    } else {
        Serial.println("Invalid RX PAYLOAD header format");
        resetRxState();
    }
}

void MQTT::handleRxPayloadData(const String& line) {
    // Accumulate payload data
    currentRxPayload += line;
    
    // Check if we've received all the payload data
    if (currentRxPayload.length() >= currentRxPayloadLength) {
        // Trim to exact length if we received more
        if (currentRxPayload.length() > currentRxPayloadLength) {
            currentRxPayload = currentRxPayload.substring(0, currentRxPayloadLength);
        }
        
        rxState = RxState::WAITING_FOR_END;
        rxTimeout = millis() + RX_TIMEOUT_MS;
        
        Serial.print("Payload data received: ");
        Serial.println(currentRxPayload);
    } else {
        // Still waiting for more payload data
        Serial.print("Partial payload received, total so far: ");
        Serial.print(currentRxPayload.length());
        Serial.print("/");
        Serial.println(currentRxPayloadLength);
    }
}

void MQTT::handleRxEnd(const String& line) {
    Serial.println("RX END received, message complete!");
    
    if (messageCallback) {
        char topicBuffer[256];
        currentRxTopic.toCharArray(topicBuffer, sizeof(topicBuffer));
        
        unsigned char payloadBuffer[512];
        currentRxPayload.toCharArray((char*)payloadBuffer, sizeof(payloadBuffer));
        
        messageCallback(topicBuffer, payloadBuffer, currentRxPayload.length());
        
        Serial.println("Callback executed successfully");
    } else {
        Serial.println("No callback set, message ignored");
    }
    
    resetRxState();
}

void MQTT::resetRxState() {
    rxState = RxState::IDLE;
    currentRxTopic = "";
    expectedTopicLength = 0;
    expectedPayloadLength = 0;
    currentRxPayloadLength = 0;
    currentRxPayload = "";
    rxTimeout = 0;
}