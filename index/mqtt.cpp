#include "mqtt.h"

#include "pch.h"
MQTT::MQTT()
{
  m_WifiClient = new WiFiClient();
  m_Client = new PubSubClient(*m_WifiClient);
}
MQTT::~MQTT()
{
  delete m_WifiClient;
  delete m_Client;
}
bool MQTT::publish(const char *topic, const char *payload, bool retained)
{
  bool sent = (m_Client->publish(topic, payload, retained));
  if (sent)
  {
    logDebug("  - MQTT broker: Message publised [");
    logDebug(topic);
    logDebugln("]: ");
    logDebugln(payload);
  }
  else
  {
    logDebugln("  - MQTT: Não foi possivel enviar");
  }
  return sent;
}

bool MQTT::connectMqtt(const char *contextName, const char *mqtt_username, const char *mqtt_password, const char *mqtt_topic)
{
  if (!m_Client->connected())
  {
    logDebugf("%s: Tentando nova conexão...", contextName);
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (m_Client->connect(clientId.c_str(), mqtt_username, mqtt_password))
    {
      logDebugf("%s: Reconectado", contextName);
      return true;
    }
    logDebug("failed, rc=");
    logDebug(m_Client->state());
    return false;
  }
  logDebugf("%s: Conectado [ %s ]", contextName, mqtt_topic);
  return true;
}

bool MQTT::setupMqtt(const char *contextName, const char *mqtt_server, int mqtt_port, const char *mqtt_username, const char *mqtt_password, const char *mqtt_topic)
{
  logDebugf("\n%s: Estabelecendo conexão inicial\n", contextName);
  logDebugln(mqtt_server);
  logDebugln(mqtt_port);
  logDebugln(mqtt_username);
  logDebugln(mqtt_password);
  logDebugln(mqtt_topic);
  m_Client->setServer(mqtt_server, mqtt_port);
  return connectMqtt(contextName, mqtt_username, mqtt_password, mqtt_topic);
}

bool MQTT::loopMqtt()
{
  return m_Client->loop();
}

void MQTT::setBufferSize(int size)
{
  m_Client->setBufferSize(size);
}

bool MQTT::subscribe(const char *mqtt_topic)
{
  logDebugf("Subscribed to %s\n", mqtt_topic);
  return m_Client->subscribe(mqtt_topic);
}

void MQTT::setCallback(void (*callback)(char *, unsigned char *, unsigned int))
{
  m_Client->setCallback(callback);
}

bool MQTT::beginPublish(const char *topic, unsigned int plength, bool retained)
{
  return m_Client->beginPublish(topic, plength, retained);
}
int MQTT::endPublish()
{
  return m_Client->endPublish();
}
int MQTT::write(char c)
{
  return m_Client->write((c));
}
int MQTT::write(const unsigned char *buffer, int size)
{
  return m_Client->write(buffer, size);
}
