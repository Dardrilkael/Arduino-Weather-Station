#include "OTA.h"
#include <HTTPClient.h>
#include <Update.h>
#include "pch.h"
#include <WiFi.h> 
bool OTA::update(const String& url) {
    Serial.println("Starting firmware update...");

    HTTPClient http;

    // Begin the HTTP connection
    if (!http.begin(url)) {
        Serial.println("Unable to connect to the server.");
        return false;
    }

    // Send the request and check the response
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP GET failed: %s(%d)\n", http.errorToString(httpCode).c_str(),httpCode);
        http.end();
        return false;
    }

    // Get the size of the content to download
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("Invalid content length.");
        http.end();
        return false;
    }

    Serial.printf("Downloading firmware binary (%d bytes)...\n", contentLength);

    // Start the update process
    if (Update.begin(contentLength)) {
        Serial.println("Begin update");

        // Create a stream to read the firmware binary
        WiFiClient* stream = http.getStreamPtr();
        if (Update.writeStream(*stream)) {
            Serial.println("Writing stream...");

            // Finish the update process
            if (Update.end()) {
                Serial.println("Download completed successfully.");
                http.end();
                return true; // Successful update
            } else {
                Serial.println("Update could not be completed.");
            }
        } else {
            Serial.println("Failed to write the stream.");
        }
    } else {
        Serial.println("Failed to begin the update.");
    }

    // If any step failed, clean up and return false
    http.end();
    return false;
}
