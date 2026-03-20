// MQTT.h - MQTT client interface for AT command-based modems
#pragma once

#include <Arduino.h>
#include "ModemAT.h"

// Forward declaration
class ATResponse;

// Callback type for incoming MQTT messages

typedef void (*MQTTCallback)(const char *topic, const unsigned char *payload, unsigned  int length);
struct mqttData
{
    const char *mqtt_server;
    int mqtt_port;
    const char *mqtt_username;
    const char *mqtt_password;
    const char *mqtt_topic;
};

class MQTT
{
public:
    // Constructor/destructor
    MQTT(ModemAT &modemRef);
    ~MQTT();

    // Connection management
    bool setupMqtt(const char *contextName, const char *mqtt_server, int mqtt_port,
                   const char *mqtt_username, const char *mqtt_password, const char *mqtt_topic);
    // bool connectMqtt(const char* contextName, const char* mqtt_username, const char* mqtt_password, const char* mqtt_topic);
    // bool disconnect();
    // bool cleanup();

    // Message operations
    bool publish(const char *topic, const char *payload, bool retained = false);
    bool subscribe(const char *topic, uint8_t qos = 0, uint8_t clientIndex = 0);
    // bool beginPublish(const char* topic, unsigned int plen, bool retained = false);
    // int endPublish();
    // int write(const unsigned char* buffer, int size);

    // Subscription management
    // bool unsubscribe(const char* topic);
    void setCallback(MQTTCallback callback);

    // Runtime operations
    // bool loopMqtt();
    bool isConnected() const { return mqttConnected; }

    bool checkConnectionStatus();
    MQTTDiscReport getConnectionStatus();

    bool reconnect();


private:
    mqttData m_Data;
    // Internal state machine for message reception
    enum class RxState
    {
        IDLE,
        WAITING_FOR_TOPIC,
        WAITING_FOR_TOPIC_DATA,
        WAITING_FOR_PAYLOAD_HEADER,
        WAITING_FOR_PAYLOAD_DATA,
        WAITING_FOR_END
    };

    // Constants
    static const unsigned long RX_TIMEOUT_MS = 10000;

    // Member variables
    ModemAT &m_Modem;
    bool mqttConnected;
    bool publishInProgress;
    size_t bufferSize;

    // Publishing state
    unsigned int expectedPayloadLength;
    unsigned int currentPayloadLength;
    String currentTopic;

    // Receiving state
    unsigned int expectedTopicLength;
    unsigned int expectedRxTopic;
    unsigned int currentRxPayloadLength;
    RxState rxState;
    unsigned long rxTimeout;
    String currentRxTopic;
    String currentRxPayload;
    int rxClientIndex = 0; // TODO REVIEW was not in the orignals

    // Callback
    MQTTCallback messageCallback;

    // Internal methods
    bool connectBroker(const String &url, const String &user, const String &password);
    bool initializeMQTTService();
    bool acquireClient(const char *clientID);
    // bool connectToBroker(const char* brokerURL, int keepalive, bool cleansession, const char* username, const char* password);

    bool checkClientAcquired(String &clientId);
    // Message processing

    // void processIncomingMessages();
    // void processLine(const String& line);
    void handleRxStart(const String &line);
    void handleRxTopic(const String &line);
    void handleRxTopicData(const String &line);
    void handleRxPayloadHeader(const String &line);
    void handleRxPayloadData(const String &line);
    void handleRxEnd(const String &line);
    void resetRxState();

public:
    bool processLine(const String &line);
};
