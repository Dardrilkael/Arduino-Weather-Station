#pragma once

#include "pch.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SD.h>
#include "data.h"

// Configurações
// const char* serverUrl = "http://181.223.111.61:4500/failures/upload";
const char *serverUrl = "http://metcolab.macae.ufrj.br/admin/admin/failures/upload"; // Altere para seu endpoint
const char *contentType = "text/csv";                                                // ou "application/octet-stream" se necessário
String getContentType(String filename)
{
    if (filename.endsWith(".txt"))
        return "text/csv";
    else if (filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".json"))
        return "application/json";
    else if (filename.endsWith(".jpg"))
        return "image/jpeg";
    else if (filename.endsWith(".png"))
        return "image/png";
    else if (filename.endsWith(".pdf"))
        return "application/pdf";
    else
        return "application/octet-stream"; // default for unknown
}

bool renameFile(File &file, const char *path)
{
    String currentName = file.name();

    // Garante que o path termina com "/"
    String basePath = String(path);
    if (!basePath.endsWith("/"))
    {
        basePath += "/";
    }

    // Constroi caminhos completos
    String fullCurrentName = basePath + currentName;
    String fullNewName = basePath + "@" + currentName;

    file.close(); // Fecha antes de renomear

    if (SD.rename(fullCurrentName.c_str(), fullNewName.c_str()))
    {
        logDebugf("Arquivo renomeado de %s para %s\n", fullCurrentName.c_str(), fullNewName.c_str());
        return true;
    }
    else
    {
        logDebugf("Falha ao renomear o arquivo %s para %s\n", fullCurrentName.c_str(), fullNewName.c_str());
        return false;
    }
}

bool deleteFile(File &file, const char *path)
{
    String currentName = file.name();

    // Garante que o path termina com "/"
    String basePath = String(path);
    if (!basePath.endsWith("/"))
    {
        basePath += "/";
    }

    // Constroi o caminho completo
    String fullFileName = basePath + currentName;

    file.close(); // Fecha antes de deletar

    if (SD.remove(fullFileName.c_str()))
    {
        logDebugf("Arquivo deletado: %s\n", fullFileName.c_str());
        return true;
    }
    else
    {
        logDebugf("Falha ao deletar o arquivo: %s\n", fullFileName.c_str());
        return false;
    }
}

// Envia um arquivo CSV via POST direto do cartão SD
bool sendCSVFile(File &file, const char *url, const char *id)
{
    if (!file)
    {
        logDebugf("Erro ao abrir arquivo %s\n", file.name());
        return false;
    }
    if (file.name()[0] == '@')
        return false;

    if (file.size() == 0)
    {
        logDebugf("Arquivo %s está vazio — envio cancelado\n", file.name());
        file.close();
        return false;
    }

    HTTPClient http;
    logDebugf("Enviando arquivo %s para %s\n", file.name(), url);

    if (!http.begin(url))
    {
        logDebugln("Falha ao iniciar conexão HTTP");
        file.close();
        return false;
    }

    http.addHeader("Content-Type", getContentType(file.name()));
    http.addHeader("Connection", "close");
    http.addHeader("X-Filename", file.name());
    http.addHeader("X-Device-Name", String(config.station_name));
    http.addHeader("X-Cmd-Id", String(id)); // <- Aqui!

    // Envio via stream
    int httpResponseCode = http.sendRequest("POST", &file, file.size());

    logDebugf("Código de resposta HTTP: %d\n", httpResponseCode);

    http.end();

    return (httpResponseCode > 0 && httpResponseCode < 300);
}

bool processFiles(const char *dirPath, const char *todayDateString, int amount)
{
    File dir = SD.open(dirPath);
    if (!dir)
    {
        logDebugln("Falha ao abrir diretório.");
        return false;
    }

    bool success = false;
    int count = 0;

    String todayFileName;
    if (todayDateString) {
        todayFileName = String(todayDateString) + ".txt";
    }

    while (File file = dir.openNextFile())
    {
        if (!file.isDirectory())
        {
            String currentName = file.name();
            logDebugf("Processando arquivo: %s\n", currentName.c_str());

            if (currentName.startsWith("@"))
            {
                logDebugf("Pulando renomeação, o arquivo %s já tem o prefixo '@'.\n", currentName.c_str());
                file.close();
                continue;
            }
            else if (todayDateString && currentName == todayFileName)
            {
                logDebugf("Pulando data de hoje: %s\n", currentName.c_str());
                file.close();
                continue;
            }

            if (sendCSVFile(file, serverUrl, "automatic"))
            {
                deleteFile(file, dirPath);
                // renameFile(file, dirPath);
                success = true;
            }
            
            count++;
            if (count >= amount)
            {
                file.close();
                break;
            }
        }
        file.close();
    }

    dir.close();
    return success;
}
