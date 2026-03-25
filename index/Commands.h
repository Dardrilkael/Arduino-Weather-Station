#pragma once
#include "pch.h"
#include <ArduinoJson.h>
#include "mqtt.h"
#include "data.h"
#include "OTA.h"
#include "sd-repository.h"
#include "httpRecover.h"
#include "bt-integration.h"

// Globals defined in index.ino that commands.cpp needs access to
extern MQTT mqttClient;
extern String sysReportMqttTopic;
extern std::string jsonConfig;
extern HealthCheck healthCheck;

// Publishes a JsonObject serialized to a char buffer via MQTT
void publishJsonResponse(const char *topic, const JsonObject &response);

// Sends a file (or directory recursively) to a given MQTT topic in base64 chunks
void sendFileChunks(const char *path, const char *fileMqttTopic, const char *id);

// Executes a single command from a parsed MQTT JSON payload
void executeCommand(JsonObject &docData, const char *replyTopic);

// MQTT subscription callback — parses payload and dispatches to executeCommand
void mqttSubCallback(const char *topic, const unsigned char *payload, unsigned int length);