// HttpClient.cpp - HTTP client implementation refactored to use ATResponse
#include "HttpClient.h"

#include <string.h>

/*
int postProviderSD(size_t offset, uint8_t *buf, size_t len) {
    if (!file) return 0; // file not open

    // Read directly into the external buffer
    size_t bytesRead = file.read(buf, len);
    return bytesRead; // returns 0 at EOF
}
*/
String insertCounter(const String& name, int x)
{
  Serial.printf("Before: %s | ",name.c_str());
    int dot = name.lastIndexOf('.');
    if (dot == -1) {
        // no extension → just append at end
        return name + "(" + String(x) + ")";
    }

    // example: testando.txt
    String base = name.substring(0, dot);      // "testando"
    String ext  = name.substring(dot);         // ".txt"

    String final =  base + "(" + String(x) + ")" + ext; // "testando(1).txt"
    Serial.printf("After: %s |\n",final.c_str());
    return final;
}


Header* findHeader(std::vector<Header>& _headers,const String& key) {
    for (auto &h : _headers) {
        if (h.key == key)
            return &h;
    }
    return nullptr;
}


int postProviderExample(size_t offset, uint8_t *buf, size_t len)
{

  static const char *payload = "hello from ESP32!";
  size_t payloadLen = strlen(payload);

  if (offset >= payloadLen)
    return 0; // nothing more to send

  size_t chunk = (payloadLen - offset) < len ? (payloadLen - offset) : len;
  memcpy(buf, payload + offset, chunk);
  return chunk;
}
void httpCallback(int code, const unsigned char *data, size_t len, bool finalChunk)
{
  if (!data)
  {
    Serial.print("HTTP status: ");
    Serial.println(code);
  }
  else
  {
    Serial.print("Chunk (len=");
    Serial.print(len);
    Serial.print(") : ");
    for (size_t i = 0; i < len; ++i)
      Serial.write(data[i]);
    Serial.println();
    if (finalChunk)
      Serial.println(">> final chunk");
  }
}

HttpClient::HttpClient(ModemAT &modemRef)
    : m_Modem(modemRef)
{
}

bool HttpClient::begin(const String &url)
{
  initializeHTTPService();
  yield();
  // Set URL
  ATResponse r = m_Modem.sendCommand((String("AT+HTTPPARA=\"URL\",\"") + url + "\"").c_str(), 9000, 9002,  [](const String& buf){Serial.print("Here: ");bool a =  buf.endsWith("OK\r\n");Serial.println(a);return a; });
  r.print(897);
  if (!r)
  {
    Serial.println("✗ Failed to set URL in begin");
    return false;
  }
  return r;
}

bool HttpClient::initializeHTTPService()
{
  // Check if already initialized
  ATResponse r = m_Modem.sendCommand("AT+HTTPINIT?", 2000);
  if (r.contains("+HTTPINIT: 1"))
  {
    Serial.println("✓ HTTP service already initialized");
    return true;
  }

  // Initialize HTTP service
  r = m_Modem.sendCommand("AT+HTTPINIT", 5000);

  if (r)
  {
    Serial.println("✓ HTTP service initialized successfully");
    return true;
  }
  else if (r.endsIn("ERROR"))
  {
    // Try to terminate and restart
    m_Modem.sendCommand("AT+HTTPTERM", 2000);
    r = m_Modem.sendCommand("AT+HTTPINIT", 5000);
    if (r)
    {
      Serial.println("✓ HTTP service reinitialized successfully");
      return true;
    }
  }

  Serial.println("✗ Failed to initialize HTTP service");
  return false;
}

void waitForUser()
{
  while (true)
  {
    if (Serial.available())
    {
      if (Serial.read() == '1')
        break;
    }
    delay(1);
    yield();
  }
}
// typedef void (*HTTPChunkCallback)(int httpCode, const unsigned char* data, size_t length, bool finalChunk);
bool HttpClient::_httpRead(HTTPChunkCallback cb, const HTTPAction &result)
{
  const uint16_t chunkSize = 256;
  int bytesRead = 0;
  while (bytesRead < result.dataLen)
  {
    char cmd[32];
    sprintf(cmd, "AT+HTTPREAD=%lu,%u", bytesRead, chunkSize);
    ATResponse rr = m_Modem.sendCommand(cmd, 1000, 1000);
    HTTPRead rresult = rr.as<HTTPRead>();
    bytesRead += rresult.dataLen;
    if (cb)
    {
      bool last = (bytesRead >= result.dataLen);
      cb(result.code, (const unsigned char *)rresult.data.c_str(), rresult.dataLen, last);
    }
  }

  return true;
}

bool HttpClient::get(HTTPChunkCallback cb)
{

  // Do GET
  ATResponse r = m_Modem.sendCommand("AT+HTTPACTION=0", 60000, 3000);
  HTTPAction result = r.as<HTTPAction>();
  result.print();

  if (!result.isHttpOK())
  {
    Serial.println("HTTPACTION failed");
    return false;
  }

  return _httpRead(cb, result);
}

bool HttpClient::postByParts(HTTPPostProvider provider, size_t totalSize, size_t chunkSize, HTTPChunkCallback cb)
{
    int numOfChunks = totalSize / chunkSize;
    if (totalSize % chunkSize != 0) {
        numOfChunks++;
    }
    if (!m_URL.length())return false;

    size_t offset = 0;

    String name;
    String* originalName;
    if (auto h = findHeader(_headers, "X-Filename")) 
    {
      originalName = &h->value;
      name = h->value;
    }

    for (int i = 0; i < numOfChunks; i++) {
        *originalName = insertCounter(name,i);
        // last chunk may be smaller
        size_t remaining = totalSize - offset;
        size_t thisChunkSize = (remaining > chunkSize) ? chunkSize : remaining;
       


        begin(m_URL);
        bool ok = post(provider, thisChunkSize, cb, offset);
        _terminate();
        yield();
        delay(10);

        if (!ok) {
            Serial.println("Chunk POST failed");
            _headers.clear();
            return false;
        }

        offset += thisChunkSize;
    }
    _headers.clear();
    return true;
}

bool HttpClient::post(HTTPPostProvider provider, size_t totalSize, HTTPChunkCallback cb,int offset)
{

  if (totalSize>153600-1)
  {
    Serial.println("Working it here");
    return false;
  }

  String headersStr;
  int i = 0;
  for (const auto &h : _headers)
  {
    headersStr = h.key + ": " + h.value;
    ATResponse r = m_Modem.sendCommand(("AT+HTTPPARA=\"USERDATA\",\"" + headersStr + "\"").c_str(), 1000);
    Serial.println("AT+HTTPPARA=\"USERDATA\",\"" + headersStr);
    r.print(++i);
    delay(1);
    yield();

  }

  char cmd[64];
  Serial.printf("Total Size is %i\n",totalSize+1);
  sprintf(cmd, "AT+HTTPDATA=%i,%i", totalSize+1, 5000);
  ATResponse r8 = m_Modem.sendCommand(cmd, 1000);
  r8.print(8);
  yield();

  const int chunkSize = 4096;
  uint8_t buffer[chunkSize];
  int chunkNumber = 0;
  int bytesRead;
  int totalBytesRead = 0;

  while (totalBytesRead < totalSize)
  {
    int remainingSize = totalSize-totalBytesRead;
    int thisChunkSize = remainingSize < chunkSize?remainingSize:chunkSize ;
    bytesRead = provider(offset, buffer, thisChunkSize);
    totalBytesRead += bytesRead;

    offset += bytesRead;
    //buffer[bytesRead] = 0;  
    bool r = m_Modem.writeData(buffer, bytesRead, 2000); // send chunk to modem
    Serial.printf("Bytes sent(%i): %i\n",chunkNumber, bytesRead);
    //r.print(chunkNumber);
    chunkNumber++;
  }
  ATResponse rr = m_Modem.sendCommand("", 1000, 1000);
  rr.print(75);


  // 6. Execute POST request
  r8 = m_Modem.sendCommand("AT+HTTPACTION=1", 60000, 3001);
  r8.print(10);
  yield();

  HTTPAction result = r8.as<HTTPAction>();
  result.print();

  if (!result.isHttpOK())
  {
    Serial.println("POST failed");
    return false;
  }

  return _httpRead(cb, result);

}

bool HttpClient::parseHTTPAction(const String &line, int &httpCode, int &dataLen)
{
  int first = line.indexOf(':');
  String payload = line.substring(first + 1);
  payload.trim();
  int c1 = payload.indexOf(',');
  int c2 = payload.indexOf(',', c1 + 1);

  if (c1 != -1 && c2 != -1)
  {
    // int method = payload.substring(0, c1).toInt();
    httpCode = payload.substring(c1 + 1, c2).toInt();
    dataLen = payload.substring(c2 + 1).toInt();
    return true;
  }
  return false;
}

bool HttpClient::processLine(const String &line)
{
  // HTTP is synchronous, so we don't need complex URC handling
  // Only handle DOWNLOAD prompt for POST data
  String s = line;
  s.trim();

  if (s == "DOWNLOAD")
  {
    Serial.println("✓ DOWNLOAD prompt received");
    // This is handled in the post() method directly
    return true;
  }

  return false;
}

ATResponse HttpClient::_terminate()
{
  return m_Modem.sendCommand("AT+HTTPTERM", 2000);
}
void HttpClient::end()
{
  _terminate();
  _headers.clear();
  Serial.println("✓ HTTP service terminated");
}