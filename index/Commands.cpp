#include "commands.h"
#include <Base64.h>
#include "TimeManager.h"
#include "WifiManager.h"

void publishJsonResponse(const char *topic, const JsonObject &response)
{
  char buffer[512];
  serializeJson(response, buffer, sizeof(buffer));
  mqttClient.publish(topic, buffer, false);
}

void sendFileChunks(const char *path, const char *fileMqttTopic, const char *id)
{
  File file = SD.open(path);
  if (!file)
  {
    mqttClient.publish(fileMqttTopic, "{\"error\":\"could not open file\"}");
    return;
  }

  if (file.isDirectory())
  {
    File dir = file;
    dir.rewindDirectory();
    while (true)
    {
      File entry = dir.openNextFile();
      if (!entry)
        break;
      sendFileChunks((String(path) + "/" + entry.name()).c_str(), fileMqttTopic, id);
      entry.close();
    }
    file.close();
    return;
  }

  size_t fileSize = file.size();
  constexpr size_t chunkSize = 256;
  size_t totalChunks = (fileSize + chunkSize - 1) / chunkSize;
  size_t chunkNum = 0;

  // Send start message
  {
    StaticJsonDocument<256> startMsg;
    startMsg["status"] = "start";
    startMsg["filename"] = path;
    startMsg["totalChunks"] = totalChunks;
    startMsg["id"] = id;
    publishJsonResponse(fileMqttTopic, startMsg.as<JsonObject>());
  }

  // Send data chunks
  while (file.available())
  {
    StaticJsonDocument<512> jsonChunk;
    jsonChunk["chunk"] = ++chunkNum;
    jsonChunk["id"] = id;

    char data[chunkSize];
    size_t bytesRead = file.readBytes(data, chunkSize);
    String base64Data = base64::encode((unsigned char *)data, bytesRead);
    jsonChunk["data"] = base64Data;

    publishJsonResponse(fileMqttTopic, jsonChunk.as<JsonObject>());
  }
  file.close();

  // Send end message
  StaticJsonDocument<256> endMsg;
  endMsg["status"] = "end";
  endMsg["filename"] = path;
  endMsg["totalChunks"] = totalChunks;
  endMsg["id"] = id;
  publishJsonResponse(fileMqttTopic, endMsg.as<JsonObject>());
}

void executeCommand(JsonObject &docData, const char *replyTopic)
{
  logDebugln("Executing command");

  const char *id = docData["id"] | "0";
  const char *strCommand = docData["cmd"] | "z";
  char command = strCommand[0];

  auto send = [replyTopic](JsonDocument &doc)
  {
    publishJsonResponse(replyTopic, doc.as<JsonObject>());
  };

  DynamicJsonDocument response(256);
  response["id"] = id;

  switch (command)
  {
  case 'r':
  { // Restart
    response["status"] = "single";
    response["message"] = "Restarting ESP32";
    send(response);
    delay(100);
    flushLog();
    ESP.restart();
    break;
  }

  case 'l':
  { // List directory
    const char *dirPath = docData["dir"] | "/";
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory())
    {
      response["status"] = "error";
      response["error"] = "Could not open directory";
      send(response);
      return;
    }
    response["status"] = "start";
    send(response);

    DynamicJsonDocument chunkResponse(256);
    chunkResponse["id"] = id;
    const char *dirList = "\00";
    for (; (dirList = listDirectory(dir, 128))[0];)
    {
      chunkResponse["data"] = dirList;
      send(chunkResponse);
    }

    response.clear();
    response["id"] = id;
    response["status"] = "end";
    send(response);
    dir.close();
    break;
  }

  case 'g':
  { // Get file as chunks
    const char *filename = docData["fn"] | "";
    sendFileChunks(filename, (String("file") + String(config.mqtt_topic)).c_str(), id);
    break;
  }

  case 'h':
  { // Upload file via HTTP POST
    const char *filename = docData["fn"] | "";
    const char *url = docData["url"] | "0";
    if (!url[0])
    {
      response["error"] = "no destination";
      send(response);
      break;
    }
    File file = SD.open(filename);
    if (!file)
    {
      response["error"] = "could not open file";
      send(response);
      break;
    }
    response["status"] = "single";
    response["sent"] = sendCSVFile(file, url, id);
    send(response);
    break;
  }

  case 'a':
  { // Append to file
    const char *filename = docData["fn"] | "";
    const char *content = docData["content"] | "";
    appendFile(SD, filename, content);
    response["status"] = "single";
    response["message"] = "Content appended";
    send(response);
    break;
  }

  case 'd':
  { // Delete file
    const char *filename = docData["fn"] | "";
    response["status"] = "single";
    response["message"] = SD.remove(filename) ? "File deleted successfully" : "Could not delete file";
    send(response);
    break;
  }

  case 'v':
  { // Firmware version
    response["status"] = "single";
    response["version"] = FIRMWARE_VERSION;
    send(response);
    break;
  }

  case 'c':
  { // Configuration
    response["status"] = "single";
    response["config"] = jsonConfig;
    send(response);
    break;
  }

  case 'u':
  { // OTA update
    const char *url = docData["url"] | "";
    if (!url || !*url)
    {
      response["status"] = "error";
      response["error"] = "No URL provided";
      send(response);
      return;
    }
    response["status"] = "start";
    send(response);
    response["status"] = "prs";
    OTA_Result result = OTA::update(String(url), [&](int progress)
    {
      response["prs"] = progress;
      send(response);
    });

    // Ensure MQTT connection after OTA (download disconnects it)
    if (healthCheck.isWifiConnected && !mqttClient.loopMqtt())
    {
      if (mqttClient.connectMqtt("\n  - MQTT2", config.mqtt_username, config.mqtt_password, config.mqtt_topic))
        mqttClient.subscribe((String("sys") + String(config.mqtt_topic)).c_str());
    }

    response["status"] = result ? "success" : "failure";
    response["message"] = result.error.c_str();
    send(response);

    if (result.success)
    {
      logDebugln("Update successful! Reiniciando...");
      flushLog();
      delay(1500);
      ESP.restart();
    }
    else
    {
      logDebugf("OTA failed: %s — keeping current firmware.\n", result.error.c_str());
    }
    break;
  }

  default:
  {
    response["status"] = "error";
    response["error"] = "Unknown command";
    send(response);
    logDebugln("Unknown command");
    break;
  }
  }
}

void mqttSubCallback(const char *topic, const unsigned char *payload, unsigned int length)
{
  logDebugln("exec MQTT cmd");

  DynamicJsonDocument doc(1024 + 128);
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error)
  {
    logDebug("deserializeJson() failed: ");
    logDebugln(error.c_str());
    return;
  }

  JsonObject docData = doc.as<JsonObject>();
  if (doc.containsKey("data"))
    docData = doc["data"].as<JsonObject>();

  serializeJson(docData, Serial);

  if (docData.containsKey("cmd"))
  {
    const char *cmd = docData["cmd"];
    if (strcmp(cmd, "update") == 0)
      docData["cmd"] = "u";
    executeCommand(docData, sysReportMqttTopic.c_str());
  }
}