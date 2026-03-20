// MQTT.cpp - MQTT client implementation refactored to use ATResponse
#include "MQTT.h"
#include <Arduino.h>

// -------------------- MQTT implementation --------------------

MQTT::MQTT(ModemAT &modemRef)
    : m_Modem(modemRef),
      mqttConnected(false),
      publishInProgress(false),
      bufferSize(512),
      expectedPayloadLength(0),
      currentPayloadLength(0),
      expectedTopicLength(0),
      expectedRxTopic(0),
      rxState(RxState::IDLE),
      rxTimeout(0)
{
    messageCallback = nullptr;
}

MQTT::~MQTT()
{
    // disconnect();
    //AT+CMQTTDISC=0,120
}

// Top-level setup flow
bool MQTT::setupMqtt(const char *contextName, const char *mqtt_server, int mqtt_port,
                     const char *mqtt_username, const char *mqtt_password, const char *mqtt_topic)
{
    m_Data = { mqtt_server,  mqtt_port, mqtt_username, mqtt_password, mqtt_topic};

    initializeMQTTService();
    String clientID;
    if (!checkClientAcquired(clientID))
    {
        clientID = "est***";
        String cmd;
        cmd.reserve(25);
        cmd = F("AT+CMQTTACCQ=0,\"");
        cmd += clientID;
        cmd += F("\",0");

        ATResponse r = m_Modem.sendCommand(cmd);
        if (!r)
        {
            Serial.println("Could not aquire mqtt client");
            return false;
        }
    }
    else
    {

        Serial.println("There was already a client");
    }

    if (checkConnectionStatus())
    {
        mqttConnected = true;
        Serial.println("✓ Successfully connected with existing client");
        return true;
    }

    Serial.println("trying to connect broker...");
    String brokerURL = String("tcp://") + mqtt_server + ":" + String(mqtt_port);
    if (connectBroker(brokerURL, mqtt_username, mqtt_password))
    {
        mqttConnected = true;
        Serial.println("✓ Full MQTT setup completed successfully");
        return true;
    }
    return false;
}

bool MQTT::publish(const char *topic, const char *payload, bool retained /*= false*/)
{
    unsigned long t0, t1;

    // Serial.println("MQTT::publish started");

    // 1️⃣ Send topic length
    int topicLen = strlen(topic);
    String cmd = String("AT+CMQTTTOPIC=0,") + topicLen;

    // Serial.print("Sending topic length command: "); Serial.println(cmd);
    // t0 = millis();
    ATResponse r = m_Modem.sendCommand(cmd, 2234);
    // t1 = millis();
    // Serial.print("Topic length command done in ms: "); Serial.println(t1 - t0);

    // r.print(); TODO
    if (!r.isPrompt())
    {
        Serial.println("Error: Not a prompt after topic length");
        return false;
    }

    // 2️⃣ Send topic data (raw)
    // Serial.print("Sending topic data: "); Serial.println(topic);
    // t0 = millis();
    m_Modem.sendCommand(topic);
    // t1 = millis();
    // Serial.print("Topic data sent in ms: "); Serial.println(t1 - t0);

    // 3️⃣ Send payload length
    int payloadLen = strlen(payload);
    cmd = String("AT+CMQTTPAYLOAD=0,") + payloadLen;
    // Serial.print("Sending payload length command: "); Serial.println(cmd);
    // t0 = millis();
    r = m_Modem.sendCommand(cmd, 2000);
    r.print(123);
    // t1 = millis();
    // Serial.print("Payload length command done in ms: "); Serial.println(t1 - t0);
    Serial.println(r.lineCount);
    r.print(8888);
    if (!r.isPrompt())
    {
        Serial.println("Error: Not a prompt after payload length");
        return false;
    }

    // 4️⃣ Send payload data
    // Serial.print("Sending payload data: "); Serial.println(payload);
    // t0 = millis();
    m_Modem.sendCommand(payload);
    // t1 = millis();
    // Serial.print("Payload data sent in ms: "); Serial.println(t1 - t0);

    // 5️⃣ Publish
    cmd = String("AT+CMQTTPUB=0,") + (retained ? "1" : "0") + ",60";
    // Serial.print("Sending publish command: "); Serial.println(cmd);
    // t0 = millis();
    r = m_Modem.sendCommand(cmd, 5000);
    // t1 = millis();
    // Serial.print("Publish command done in ms: "); Serial.println(t1 - t0);

    // Serial.println("MQTT::publish finished");

    return r.status == ATStatus::MQTT_MSG_SENT;
}

bool MQTT::subscribe(const char *topic, uint8_t qos /*=0*/, uint8_t clientIndex /*=0*/)
{
    // 1️⃣ Send topic length command
    int topicLen = strlen(topic);
    String cmd = String("AT+CMQTTSUBTOPIC=") + clientIndex + "," + topicLen + "," + qos;

    ATResponse r = m_Modem.sendCommand(cmd, 2000);
    r.print();
    if (!r.isPrompt())
    {
        Serial.println("Error: Not a prompt after AT+CMQTTSUBTOPIC");
        return false;
    }

    // 2️⃣ Send topic data
    r = m_Modem.sendCommand(topic, 2000);
    if (!r)
    {
        Serial.println("Error: Failed to send subscription topic data");
        return false;
    }

    // 3️⃣ Actually subscribe the topic(s)
    cmd = String("AT+CMQTTSUB=") + clientIndex;
    r = m_Modem.sendCommand(cmd, 3000,9000,[](const String& buf){return (buf.indexOf("+CMQTTSUB: ")!= -1); }); // server responds with OK + +CMQTTSUB:<client>,0
    r.print(4402);
    if (!r)
    {
        Serial.println("Error: Subscription failed at AT+CMQTTSUB");
        return false;
    }

    return r.status == ATStatus::OK;
}

bool MQTT::connectBroker(const String &url, const String &user, const String &password)
{
    Serial.printf("\turl: %s\n\tuser:%s\n\tpass:%s\n",url.c_str(),user.c_str(),password.c_str());
    String cmd = String("AT+CMQTTCONNECT=0,\"") + url + "\",60,1,\"" + user + "\",\"" + password + "\"";
    ATResponse r = m_Modem.sendCommand(cmd);
    // r.print(); TODO
    return r;
}

bool MQTT::initializeMQTTService()
{

    ATResponse r = m_Modem.sendCommand("AT+CMQTTSTART", 1200);
    if (r.endsIn("+CMQTTSTART: 0") || r)
    {
        Serial.println("Successfully started MQTT");
        return true;
    }
    else if (r.endsIn("ERROR") || !r)
    {
        Serial.println("Service was already started!");
        return true;
    }

    Serial.print("Could not start mqtt service:");
    Serial.println(r.lastLine());
    return false;
}

// TODO save the cliendID
bool MQTT::checkClientAcquired(String &clientId)
{
    ATResponse r = m_Modem.sendCommand("AT+CMQTTACCQ?", 3000);
    if (!r)
        return false;
    String line = r.getLineStartingWith("+CMQTTACCQ:");
    if (line.length() == 0)
        return false;
    // +CMQTTACCQ: 0,"clientid"
    int firstQuote = line.indexOf('"');
    int secondQuote = line.indexOf('"', firstQuote + 1);
    if (firstQuote != -1 && secondQuote != -1)
    {
        clientId = line.substring(firstQuote + 1, secondQuote);
        return clientId.length() > 0;
    }
    return false;
}

// TODO collect the borker info
bool MQTT::checkConnectionStatus()
{
    ATResponse r = m_Modem.sendCommand("AT+CMQTTCONNECT?", 3000);
    // r.print(); TODO
    return r.contains("+CMQTTCONNECT: 0,\"tcp:\/\/");
}

MQTTDiscReport MQTT::getConnectionStatus()
{ 
    ATResponse r = m_Modem.sendCommand("AT+CMQTTDISC?");
    return  r.as<MQTTDiscReport>();
}


void MQTT::setCallback(MQTTCallback callback)
{
    messageCallback = callback;
}

bool MQTT::reconnect()
{
    MQTTDiscReport status = getConnectionStatus();
    status.print();
    switch(status.state[0]) {
        case 0: // Already connected
            return true;
            
        case 1: // Disconnected - attempt clean reconnect
            // Send disconnect to clean any stale state
            m_Modem.sendCommand("AT+CMQTTDISC=0,120");
            delay(1000);
            break;
            
        default: // Unknown state
            // Force cleanup and reconnect
            m_Modem.sendCommand("AT+CMQTTDISC=0,120");
            delay(1000);
            break;
    }
    
    return setupMqtt("Reconetion",m_Data.mqtt_server,m_Data.mqtt_port, m_Data.mqtt_username, m_Data.mqtt_password,"TODO");
}

// TODO REVIEW BELOW

bool MQTT::processLine(const String &line)
{
    // Optional: check for timeout
    if (rxState != RxState::IDLE && millis() > rxTimeout)
    {
        Serial.println("RX timeout, resetting state");
        resetRxState();
        return false;
    }

    switch (rxState)
    {
    case RxState::IDLE:
        if (line.startsWith("+CMQTTRXSTART:"))
        {
            handleRxStart(line);
            return true;
        }
        break;

    case RxState::WAITING_FOR_TOPIC:
        if (line.startsWith("+CMQTTRXTOPIC:"))
        {
            handleRxTopic(line);
            return true;
        }
        else
        {
            Serial.println("Unexpected response while waiting for topic");
            resetRxState();
        }
        break;

    case RxState::WAITING_FOR_TOPIC_DATA:
        handleRxTopicData(line);
        return true;

    case RxState::WAITING_FOR_PAYLOAD_HEADER:
        if (line.startsWith("+CMQTTRXPAYLOAD:"))
        {
            handleRxPayloadHeader(line);
            return true;
        }
        else
        {
            Serial.println("Unexpected response while waiting for payload header");
            resetRxState();
        }
        break;

    case RxState::WAITING_FOR_PAYLOAD_DATA:
        handleRxPayloadData(line);
        return true;

    case RxState::WAITING_FOR_END:
        if (line.startsWith("+CMQTTRXEND:"))
        {
            handleRxEnd(line);
            return true;
        }
        else
        {
            Serial.println("Unexpected response while waiting for RX end");
            resetRxState();
        }
        break;
    }
    return false; // linha não consumida
}

// --- Implementação de cada etapa (igual à sua classe antiga) ---

void MQTT::handleRxStart(const String &line)
{
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);

    if (firstComma != -1 && secondComma != -1)
    {
        String topicLenStr = line.substring(firstComma + 1, secondComma);
        String payloadLenStr = line.substring(secondComma + 1);

        expectedTopicLength = topicLenStr.toInt();
        expectedPayloadLength = payloadLenStr.toInt();
        currentRxTopic = "";
        currentRxPayload = "";
        rxState = RxState::WAITING_FOR_TOPIC;
        rxTimeout = millis() + RX_TIMEOUT_MS;
    }
    else
    {
        resetRxState();
    }
}

void MQTT::handleRxTopic(const String &line)
{
    int firstComma = line.indexOf(',');
    if (firstComma != -1)
    {
        int topicLength = line.substring(firstComma + 1).toInt();
        if (topicLength != expectedTopicLength)
        {
            Serial.println("Topic length mismatch!");
        }
        rxState = RxState::WAITING_FOR_TOPIC_DATA;
        rxTimeout = millis() + RX_TIMEOUT_MS;
    }
    else
    {
        resetRxState();
    }
}

void MQTT::handleRxTopicData(const String &line)
{
    currentRxTopic = line;
    currentRxTopic.trim();
    rxState = RxState::WAITING_FOR_PAYLOAD_HEADER;
    rxTimeout = millis() + RX_TIMEOUT_MS;
}

void MQTT::handleRxPayloadHeader(const String &line)
{
    int firstComma = line.indexOf(',');
    if (firstComma != -1)
    {
        currentRxPayloadLength = line.substring(firstComma + 1).toInt();
        currentRxPayload = "";
        rxState = RxState::WAITING_FOR_PAYLOAD_DATA;
        rxTimeout = millis() + RX_TIMEOUT_MS;
        if (currentRxPayloadLength == 0)
        {
            rxState = RxState::WAITING_FOR_END;
        }
    }
    else
    {
        resetRxState();
    }
}

void MQTT::handleRxPayloadData(const String &line)
{
    currentRxPayload += line;
    if ((int)currentRxPayload.length() >= currentRxPayloadLength)
    {
        if ((int)currentRxPayload.length() > currentRxPayloadLength)
            currentRxPayload = currentRxPayload.substring(0, currentRxPayloadLength);
        rxState = RxState::WAITING_FOR_END;
        rxTimeout = millis() + RX_TIMEOUT_MS;
    }
}

void MQTT::handleRxEnd(const String &line)
{
    if (messageCallback)
        messageCallback(currentRxTopic.c_str(), (const unsigned char *)currentRxPayload.c_str(), currentRxPayload.length());
    resetRxState();
}

void MQTT::resetRxState()
{
    rxState = RxState::IDLE;
    currentRxTopic = "";
    currentRxPayload = "";
    expectedTopicLength = 0;
    expectedPayloadLength = 0;
    currentRxPayloadLength = 0;
    rxClientIndex = 0;
    rxTimeout = 0;
}
