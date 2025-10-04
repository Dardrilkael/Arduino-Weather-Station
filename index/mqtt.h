#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "ModemAT.h"
#include <functional>

typedef std::function<void(char*, unsigned char*, unsigned int)> MQTTCallback;

enum class RxState {
    IDLE,
    WAITING_FOR_TOPIC,           // Waiting for +CMQTTRXTOPIC
    WAITING_FOR_TOPIC_DATA,      // Waiting for actual topic data line
    WAITING_FOR_PAYLOAD_HEADER,  // Waiting for +CMQTTRXPAYLOAD
    WAITING_FOR_PAYLOAD_DATA,    // Receiving payload data
    WAITING_FOR_END              // Waiting for +CMQTTRXEND
};

class MQTT {
public:
    MQTT(ModemAT& modem);
    ~MQTT();
    
    bool setupMqtt(const char* contextName, const char* mqtt_server, int mqtt_port, 
                   const char* mqtt_username, const char* mqtt_password, const char* mqtt_topic);
    bool connectMqtt(const char* contextName, const char* mqtt_username, const char* mqtt_password, const char* mqtt_topic);
    bool disconnect();
    
    bool publish(const char* topic, const char* payload, bool retained = false);
    bool beginPublish(const char* topic, unsigned int plength, bool retained = false);
    int endPublish();
    int write(char data);
    int write(const unsigned char* buffer, int size);
    
    bool subscribe(const char* topic);
    bool unsubscribe(const char* topic);
    void setCallback(MQTTCallback callback);
    
    bool loopMqtt();
    void setBufferSize(int size) { bufferSize = size; }
    bool isConnected() { return mqttConnected; }
    
    bool cleanup();
    bool checkExistingConnection();
    
    // New session management functions
    bool checkAndRecoverSession();
    bool isSessionActive();
    void setClientID(const char* clientID) { currentClientID = clientID; }
    String getClientID() { return currentClientID; }

private:
    ModemAT& modem;
    bool mqttConnected;
    bool publishInProgress;
    int bufferSize;
    String currentTopic;
    String currentClientID;
    unsigned int expectedPayloadLength;
    unsigned int currentPayloadLength;
    MQTTCallback messageCallback;
    

    unsigned int expectedTopicLength = 0;

   


    // Message reception state machine
    RxState rxState = RxState::IDLE;
    String currentRxTopic;
    unsigned int currentRxPayloadLength = 0;
    String currentRxPayload;
    unsigned long rxTimeout = 0;
    const unsigned long RX_TIMEOUT_MS = 10000;
    
    // Session management
    unsigned long lastSessionCheck = 0;
    const unsigned long SESSION_CHECK_INTERVAL = 30000;
    
    bool initializeMQTTService();
    bool acquireClient(const char* clientID);
    bool connectToBroker(const char* brokerURL, int keepalive, bool cleansession, 
                        const char* username, const char* password);
    
    // Enhanced message processing
    void processIncomingMessages();
    void processLine(const String& line);
    void handleRxStart(const String& line);
    void handleRxTopic(const String& line);
    void handleRxPayloadHeader(const String& line);
    void handleRxPayloadData(const String& line);
    void handleRxEnd(const String& line);
    void resetRxState();
    
    // Session recovery functions
    bool forceCleanup();
    bool checkMQTTServiceStatus();
    bool checkClientStatus();
    bool checkConnectionStatus();


    bool checkClientAcquired();

     void handleRxTopicData(const String& line);
};

#endif