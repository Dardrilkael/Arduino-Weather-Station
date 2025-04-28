#pragma once

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SD.h>

// Configurações
const char* serverUrl = "http://192.168.0.225:3000/upload-csv"; // Altere para seu endpoint
const char* contentType = "text/csv"; // ou "application/octet-stream" se necessário


bool renameFile(File& file) {
    // Get the current name of the file
    const char* currentName = file.name();

    // Attempt to rename the file
    const char* newName = (String("@")+currentName).c_str();
    if (SD.rename(currentName,newName )) {
        Serial.printf("Arquivo renomeado de %s para %s\n", currentName, newName);
        return true;
    } else {
        Serial.printf("Falha ao renomear o arquivo %s para %s\n", currentName, newName);
        return false;
    }
}


// Envia um arquivo CSV via POST direto do cartão SD
bool sendCSVFile(File& file)
{
  if (!file) {
    Serial.printf("Erro ao abrir arquivo %s\n", file.name());
    return false;
  }
  if(file.name()[0]=='@')return false;

  HTTPClient http;
  Serial.printf("Enviando arquivo %s para %s\n", file.name(), serverUrl);

  if (!http.begin(serverUrl)) {
    Serial.println("Falha ao iniciar conexão HTTP");
    file.close();
    return false;
  }

  http.addHeader("Content-Type", contentType);
  http.addHeader("Connection", "close");

  // Envio via stream
  int httpResponseCode = http.sendRequest("POST", &file, file.size());

  Serial.printf("Código de resposta HTTP: %d\n", httpResponseCode);

  http.end();

  return httpResponseCode > 0 && httpResponseCode < 300;
}


bool processFiles(const char* dirPath, bool (*function[])(File&), int nOf, int amount = 1) {
    File dir = SD.open(dirPath);
    if (!dir) {
        Serial.println("Falha ao abrir diretório.");
        return false;
    }

    bool success = true;
    int count = 0;
    while (File file = dir.openNextFile()) {
        if (!file.isDirectory()) {
            Serial.printf("Processando arquivo: %s\n", file.name());
            if(count++>=amount)return success;
            
            for (int i = 0; i < nOf; i++) {
                if (!function[i](file)) {
                    Serial.printf("Falha ao enviar o arquivo %s.\n", file.name());
                    success = false;
                    break;
                }
            }
    
        }
        file.close();
    }

    dir.close();
    return success;
}
