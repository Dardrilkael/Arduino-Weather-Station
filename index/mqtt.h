#pragma once

void mqttSubCallback(char* topic, unsigned char* payload, unsigned int length);
class MQTT
{
public:
MQTT();
~MQTT();
bool publish(const char *topic, const char *payload,bool retained= false);
bool connectMqtt(const char *contextName, const char* mqtt_username, const char* mqtt_password, const char* mqtt_topic);
bool setupMqtt(const char *contextName, const char* mqtt_server, int mqtt_port, const char* mqtt_username, const char* mqtt_password, const char* mqtt_topic);
bool loopMqtt();
void setCallback(void (*callback)(char*, unsigned char*, unsigned int));
void setBufferSize(int size);
bool subscribe(const char* mqtt_topic);
bool beginPublish(const char* topic, unsigned int plength, bool retained= false);
int endPublish();
int write(char);
int write(const unsigned char *buffer, int size);
private:
class WiFiClient* m_WifiClient;
class PubSubClient* m_Client;
};