// HttpClient.h - HTTP client implementation refactored to use ATResponse
#pragma once

#include "ModemAT.h"
#include <Arduino.h>
#include <vector>

typedef int (*HTTPPostProvider)(size_t offset, uint8_t *buffer, size_t maxSize);
typedef void (*HTTPChunkCallback)(int httpCode, const unsigned char *data, size_t length, bool finalChunk);

int postProviderExample(size_t offset, uint8_t *buf, size_t len);
void httpCallback(int code, const unsigned char *data, size_t len, bool finalChunk);

struct Header
{
  String key;
  String value;
};

class HttpClient
{
public:
  HttpClient(ModemAT &modemRef);

  bool begin(const String &url);
  bool get(HTTPChunkCallback cb);
  bool postByParts(HTTPPostProvider provider, size_t totalSize, size_t chunkSize, HTTPChunkCallback cb);
  bool post(HTTPPostProvider provider, size_t totalSize, HTTPChunkCallback cb, int offset = 0);
  bool processLine(const String &line);
  void end();
  void cleanupHTTP();
  void sendHTTPPost();

  void addHeader(const String &key, const String &value)
  {
    _headers.push_back({key, value});
  }
  void setURL(const String& url)
  {
    m_URL = url;
  }

private:
  ATResponse _terminate();
  ModemAT &m_Modem;
  std::vector<Header> _headers;
  String m_URL;

  bool initializeHTTPService();
  bool parseHTTPAction(const String &line, int &httpCode, int &dataLen);
  bool _httpRead(HTTPChunkCallback cb, const HTTPAction &result);
};

