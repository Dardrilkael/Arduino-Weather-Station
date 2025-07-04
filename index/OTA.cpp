#include "OTA.h"
#include <HTTPClient.h>
#include <Update.h>
#include "pch.h"
#include <WiFi.h>
bool OTA::update(const String &url)
{
    logDebugln("Starting firmware update...");

    HTTPClient http;

    // Begin the HTTP connection
    if (!http.begin(url))
    {
        logDebugln("Unable to connect to the server.");
        return false;
    }

    // Send the request and check the response
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        logDebugf("HTTP GET failed: %s(%d)\n", http.errorToString(httpCode).c_str(), httpCode);
        http.end();
        return false;
    }

    // Get the size of the content to download
    int contentLength = http.getSize();
    if (contentLength <= 0)
    {
        logDebugln("Invalid content length.");
        http.end();
        return false;
    }

    logDebugf("Downloading firmware binary (%d bytes)...\n", contentLength);

    // Start the update process
    if (Update.begin(contentLength))
    {
        logDebugln("Begin update");

        // Create a stream to read the firmware binary
        WiFiClient *stream = http.getStreamPtr();
        if (Update.writeStream(*stream))
        {
            logDebugln("Writing stream...");

            // Finish the update process
            if (Update.end())
            {
                logDebugln("Download completed successfully.");
                http.end();
                return true; // Successful update
            }
            else
            {
                logDebugln("Update could not be completed.");
            }
        }
        else
        {
            logDebugln("Failed to write the stream.");
        }
    }
    else
    {
        logDebugln("Failed to begin the update.");
    }

    // If any step failed, clean up and return false
    http.end();
    return false;
}
