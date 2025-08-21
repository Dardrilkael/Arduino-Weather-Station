#pragma once

#include "pch.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SD.h>
#include "data.h"

// --------------------------------------
// Configurações
// const char* serverUrl = "http://181.223.111.61:4500/failures/upload";
const char *serverUrl = "http://metcolab.macae.ufrj.br/admin/admin/failures/upload";
// Get content type based on file extension
String getContentType(const String &filename) {
    if (filename.endsWith(".txt"))   return "text/csv";
    if (filename.endsWith(".html"))  return "text/html";
    if (filename.endsWith(".json"))  return "application/json";
    if (filename.endsWith(".jpg"))   return "image/jpeg";
    if (filename.endsWith(".png"))   return "image/png";
    if (filename.endsWith(".pdf"))   return "application/pdf";
    return "application/octet-stream"; // default
}

// --------------------------------------
// Utility functions
// --------------------------------------
String makeFullPath(const String &path, const String &filename) {
    String base = path;
    if (!base.endsWith("/")) base += "/";
    return base + filename;
}

bool renameFile(const String &path, const String &filename) {
    String oldPath = makeFullPath(path, filename);
    String newPath = makeFullPath(path, "@" + filename);

    if (SD.rename(oldPath.c_str(), newPath.c_str())) {
        logDebugf("File renamed: %s -> %s\n", oldPath.c_str(), newPath.c_str());
        return true;
    }
    logDebugf("Failed to rename file: %s\n", oldPath.c_str());
    return false;
}

bool deleteFile(const String &path, const String &filename) {
    String fullPath = makeFullPath(path, filename);

    if (SD.remove(fullPath.c_str())) {
        logDebugf("File deleted: %s\n", fullPath.c_str());
        return true;
    }
    logDebugf("Failed to delete file: %s\n", fullPath.c_str());
    return false;
}

// --------------------------------------
// Send file via HTTP POST
// --------------------------------------
bool sendCSVFile(File &file, const char *url, const char *id) {
    if (!file || file.size() == 0 || file.name()[0] == '@') {
        if (file) file.close();
        return false;
    }

    HTTPClient http;
    if (!http.begin(url)) {
        logDebugln("Failed to start HTTP connection");
        file.close();
        return false;
    }

    http.addHeader("Content-Type", getContentType(file.name()));
    http.addHeader("Connection", "close");
    http.addHeader("X-Filename", file.name());
    http.addHeader("X-Device-Name", String(config.station_name));
    http.addHeader("X-Cmd-Id", String(id));

    logDebugf("Uploading file %s to %s\n", file.name(), url);
    int responseCode = http.sendRequest("POST", &file, file.size());
    logDebugf("HTTP response: %d\n", responseCode);

    file.close();
    http.end();

    return responseCode > 0 && responseCode < 300;
}

// --------------------------------------
// Process files in a directory
// --------------------------------------
bool processFiles(const char *dirPath, const char *todayDateString, int amount) {
    File dir = SD.open(dirPath);
    if (!dir) {
        logDebugln("Failed to open directory.");
        return false;
    }

    bool success = false;
    int count = 0;
    String todayFile = todayDateString ? String(todayDateString) + ".txt" : "";

    while (File file = dir.openNextFile()) {
        if (file.isDirectory()) {
            file.close();
            continue;
        }

        // Extract file name only (remove path)
        String name = file.name();
        int lastSlash = name.lastIndexOf('/');
        if (lastSlash >= 0) name = name.substring(lastSlash + 1);

        logDebugf("Processing file: %s\n", name.c_str());

        // Skip files with '@' prefix or today's file
        if (name.startsWith("@") || ( name == todayFile)) {
            logDebugf("Skipping file: %s\n", name.c_str());
            file.close();
            continue;
        }

        // Send file
        if (sendCSVFile(file, serverUrl, "automatic")) {
            deleteFile(dirPath, name);
            // renameFile(dirPath, name); // Uncomment if needed
            success = true;
        }

        count++;
        if (count >= amount) break;
    }

    dir.close();
    return success;
}
