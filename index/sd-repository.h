#pragma once
#include "FS.h"
#include "SD.h"
#include <ArduinoJson.h>
#include "data.h"

// Inicia leitura cartão SD
void initSdCard();
// Adicionar novo diretorio
void createDirectory(const char *directory);

// Parse Mqtt connection string
void parseMQTTString(const char *mqttString, char *username, char *password, char *broker, int &port);

// Parse Wifi connection string
void parseWIFIString(const char *wifiString, char *ssid, char *password);

DeserializationError loadJson(fs::FS &fs, const char *filename, StaticJsonDocument<512> &doc);
  // Carrega arquivo de configuração inicial
bool loadConfiguration(fs::FS & fs, const char *filename, Config &config, std::string &configJson);

  // Cria um novo arquivo
void createFile(fs::FS & fs, const char *path, const char *message);

  // Escreve em arquivo
void appendFile(fs::FS & fs, const char *path, const char *message);

  // Mover isso daqui para um caso de uso
void storeMeasurement(String directory, String fileName, const char *payload);

int getDirNameLength(File & dir);

const char *listDirectory(File & dir, size_t limit);

const char *readFileLimited(File & file, size_t limit, bool allign);

void storeLog(const char *payload);
 