#include "OTA.h"
#include <HTTPClient.h>
#include <Update.h>
#include "pch.h"
#include <WiFi.h>




OTA_Result OTA::update(const String &url,std::function<void(int)> onProgress)
{
    logDebugln("Starting firmware update...");

    HTTPClient http;

    if (!http.begin(url))
    {
        logDebugln("Unable to connect to the server.");
        return OTA_Result(false,"nch1");
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        logDebugf("HTTP GET failed: %s(%d)\n", http.errorToString(httpCode).c_str(), httpCode);
        http.end();
        return OTA_Result(false,"nch2");
    }

    int contentLength = http.getSize();
    if (contentLength <= 0)
    {
        logDebugln("Invalid content length.");
        http.end();
        return OTA_Result(false,"nch3");
    }

    logDebugf("Downloading firmware binary (%d bytes)...\n", contentLength);

    if (!Update.begin(contentLength))
    {
        logDebugln("Failed to begin update.");
        http.end();
        return OTA_Result(false,"ns1");
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t written = 0;
    const size_t bufSize = 512;
    uint8_t buf[bufSize];
    int lastPercentSent = 0;
    while (http.connected() && written < (size_t)contentLength)
    {
        size_t avail = stream->available();
        if (avail)
        {
            size_t toRead = avail > bufSize ? bufSize : avail;
            size_t readBytes = stream->readBytes(buf, toRead);
            if (readBytes == 0)
            {
                logDebugln("Read 0 bytes from stream, breaking loop");
                break;
            }

            size_t writtenNow = Update.write(buf, readBytes);
            if (writtenNow != readBytes)
            {
                logDebugln("Write failed");
                std::string errorMsg = Update.errorString();
                Update.abort();
                http.end();
                return OTA_Result(false, errorMsg);
            }

            written += writtenNow;

            int percent = (written * 100) / contentLength;
            if (percent - lastPercentSent >= 10) {
                onProgress(percent);
                lastPercentSent = percent;
            }

        }
        else
        {
            delay(10); // pequeno delay para evitar CPU 100%
        }
    }

    if (!Update.end())
    {
        logDebugf("Update could not be completed. Error: %s\n", Update.errorString());
        http.end();
        return OTA_Result(false, Update.errorString())  ;
    }

    logDebugln("Download completed successfully.");
    http.end();
    return OTA_Result(true, "");
}
