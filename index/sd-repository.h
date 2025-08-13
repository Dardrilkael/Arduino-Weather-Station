#pragma once
#include "xtensa/hal.h"
#include "pch.h"
#include <SD.h>
#include <sd_defines.h>
#include <sd_diskio.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <ArduinoJson.h>

const int chipSelectPin = 32;
const int mosiPin = 23;
const int misoPin = 27;
const int clockPin = 25;
const int RETRY_INTERVAL = 5000;




// Inicia leitura cartão SD
void initSdCard()
{
  SPI.begin(clockPin, misoPin, mosiPin);
  while (!SD.begin(chipSelectPin, SPI))
  {
    logDebugf("\n  - Cartão não encontrado. tentando novamente em 3 segundos ...");
    morseCode(LED2, "... -../", 135);
    //int pin, int interval = 2000, const char *pattern = " ... -.. ", int unit = 200
  }
  logDebug("\n  - Leitor de Cartão iniciado com sucesso!.\n");
}

// Adicionar novo diretorio
void createDirectory(const char *directory)
{
  logDebugf("\n  - Tentando Criando novo diretorio: %s.", directory);
  if (!SD.exists(directory))
  {
    if (SD.mkdir(directory))
    {
      logDebugf("\n     - Diretorio criado com sucesso!");
    }
    else
    {
      logDebugf("\n     - Falha ao criar diretorio.");
    }
    return;
  }
  logDebugf("\n     - Diretorio já existe.");
}

// Parse Mqtt connection string
void parseMQTTString(const char *mqttString, char *username, char *password, char *broker, int &port)
{
  if (memcmp(mqttString, "mqtt://", 7) != 0)
  {
    logDebugf("Invalid MQTT string format!\n");
    return;
  }
  int size = strlen(mqttString) + 1;
  char *ptr = new char[size - 7];
  if(!ptr)return;
  strlcpy(ptr, mqttString + 7, size - 7);
  strlcpy(username, strtok(ptr, ":"), 64);
  strlcpy(password, strtok(NULL, "@"), 64);
  strlcpy(broker, strtok(NULL, ":"), 64);
  port = atoi(strtok(NULL, ""));
  delete[] ptr;
}

// Parse Wifi connection string
void parseWIFIString(const char *wifiString, char *ssid, char *password)
{
  int size = strlen(wifiString) + 1;
  char *ptr = new char[size];
  if(!ptr)return;
  strlcpy(ptr, wifiString, size);
  strlcpy(ssid, strtok(ptr, ":"), 64);
  strlcpy(password, strtok(NULL, ""), 64);
  delete[] ptr;
}

// Carrega arquivo de configuração inicial
bool loadConfiguration(fs::FS &fs, const char *filename, Config &config, std::string &configJson)
{

  SPI.begin(clockPin, misoPin, mosiPin);

  static int attemptCount = 0;
  bool success = false;
  logDebugf("\n - Iniciando leitura do arquivo de configuração %s (tentativa: %d)", filename, attemptCount + 1);
  if (SD.begin(chipSelectPin, SPI))
  {
    File file = fs.open(filename);
    StaticJsonDocument<512> doc;
    if (file)
    {
      DeserializationError error = deserializeJson(doc, file);
      if (!error)
      {
        strlcpy(config.station_uid, doc["UID"] | "0", sizeof(config.station_uid));
        strlcpy(config.station_name, doc["SLUG"] | "est000", sizeof(config.station_name));
        strlcpy(config.mqtt_topic, doc["MQTT_TOPIC"] | "unnamed", sizeof(config.mqtt_topic));
        config.interval = doc["INTERVAL"] | 60000;
        parseWIFIString(doc["WIFI"], config.wifi_ssid, config.wifi_password);
        parseMQTTString(doc["MQTT_HOST"], config.mqtt_username, config.mqtt_password, config.mqtt_server, config.mqtt_port);
        file.close();
        success = true;
        serializeJson(doc, configJson);
        logDebugf("\n - Variáveis de ambiente carregadas com sucesso!");
        logDebugf("\n - %s\n", configJson.c_str());
        return true;
      }
      logDebugf("\n - [ ERROR ] Formato inválido (JSON)\n");
      logDebugln(error.c_str());
    }
    logDebugf("\n - [ ERROR ] Arquivo de configuração não encontrado\n");
  }
  else
  {
    logDebugf("\n - [ ERROR ] Cartão SD não encontrado.\n");
  }

  logDebugf("\n - Proxima tentativa de re-leitura em %d segundos ... \n\n\n", (RETRY_INTERVAL / 1000));
  attemptCount++;
  morseCode(LED2,".",RETRY_INTERVAL);
  return false;
}

// Cria um novo arquivo
void createFile(fs::FS &fs, const char *path, const char *message)
{
  logDebugf("Salvando json no cartao SD: %s\n.", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    logDebugln(" - Falha ao encontrar cartão SD.");
    return;
  }
  if (file.print(message))
  {
    logDebugln(" - sucesso.");
  }
  else
  {
    logDebugln("- Falha ao salvar.");
  }
  file.close();
}

// Escreve em arquivo
void appendFile(fs::FS &fs, const char *path, const char *message)
{
  logDebugf(" - Salvando dados no cartao SD: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    logDebugln(" - Falha ao encontrar cartão SD");
    return;
  }

  if (file.print(message))
  {
    logDebugln(" - Nova linha salva com sucesso.");
  }
  else
  {
    logDebugln(" - Falha ao salvar nova linha");
  }

  file.close();
}

// Mover isso daqui para um caso de uso
void storeMeasurement(String directory, String fileName, const char *payload)
{
  String path = directory + "/" + fileName + ".txt";

  if (!SD.exists(directory))
  {
    if (SD.mkdir(directory))
    {
      logDebugln(" - Diretorio criado com sucesso!");
    }
    else
    {
      logDebugln(" - Falha ao criar diretorio de metricas.");
    }
  }
  if (!SD.exists(path))
  {
    const char *header = "timestamp,temperatura,umidade_ar,velocidade_vento,rajada_vento,dir_vento,volume_chuva,pressao,uid,identidade\n";
    appendFile(SD, path.c_str(), header);
  }
  appendFile(SD, path.c_str(), payload);
}

#define BUFFER_SIZE 512
char buffer[BUFFER_SIZE];

int getDirNameLength(File &dir)
{
  int dirNameLength = 0;
  while (true)
  {
    File entry = dir.openNextFile();
    if (!entry)
      break;
    dirNameLength += strlen(entry.name());
  }
  return dirNameLength;
}

const char *listDirectory(File &dir, size_t limit)
{
  buffer[0] = '\0';         // Clear buffer at the beginning of each function call
  size_t writtenLength = 0; // Initialize writtenLength to 0

  while (true)
  {
    File entry = dir.openNextFile();
    if (!entry)
      break;

    size_t entryNameLength = strlen(entry.name());
    if (writtenLength + entryNameLength + 2 > limit)
    {
      dir.seek(dir.position() - entryNameLength); // Move back the file pointer
      return buffer;                              // Return the directory list buffer
    }

    strcat(buffer, entry.name()); // Append entry name to buffer
    strcat(buffer, "\n");         // Append newline character
    entry.close();

    writtenLength += entryNameLength + 1; // Update writtenLength to include the entry name and newline character
  }
  return buffer; // Return the directory list buffer
}

const char *readFileLimited(File &file, size_t limit, bool allign)
{

  size_t bytesRead = file.readBytes(buffer, limit);
  buffer[bytesRead] = '\0';
  if (allign)
  {
    char *lastNewline = strrchr(buffer, '\n');

    if (lastNewline != nullptr)
    {
      *(lastNewline + 1) = '\0';
      file.seek(file.position() - bytesRead + lastNewline - buffer + 1);
    }
    logDebugf(buffer);
  }
  return buffer;
}

// Adiciona uma nova linha de metricas
void storeLog(const char *payload)
{
  String path = "/logs/boot.txt";
  File file = SD.open(path, FILE_APPEND);
  if (file)
  {
    file.print(payload);
  }
  file.close();
}