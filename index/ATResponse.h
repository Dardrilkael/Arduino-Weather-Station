#pragma once

enum class ATStatus
{
    UNKNOWN,
    OK,
    WAITING_FOR_DATA,
    MQTT_MSG_SENT,
    WAIT_FOR_POST,
    HTTP_READ_SUCCESS,
    ERROR
};

class ATResponse
{
public:
    String raw;        // full response from modem
    ATStatus status;   // parsed status
    String lines[10];  // split lines (max 10)
    int lineCount = 0; // number of lines

    ATResponse(const String &_raw, const String &command) : raw(_raw)
    {
        parse(command);
    }

    ATResponse(String &&_raw, const String &command) : raw(std::move(_raw)) { parse(command); }

    inline operator bool() const
    {
        return isOK();
    }

    inline bool isOK() const
    {
        return status == ATStatus::OK;
    }

    void parse(const String &command)
    {
        lineCount = 0;
        status = ATStatus::UNKNOWN;

        String temp = raw;
        temp.trim(); // remove leading/trailing whitespace

        int start = 0;
        while (start < temp.length() && lineCount < 10)
        {
            int idx = temp.indexOf("\r\n", start);
            if (idx == -1)
                idx = temp.length();

            String line = temp.substring(start, idx);
            line.trim(); // remove spaces/tabs around line

            // Skip empty lines, lines with only non-printable chars, or the command itself
            bool useful = false;
            for (size_t i = 0; i < line.length(); i++)
            {
                char c = line[i];
                if (c >= 32 && c <= 126)
                { // printable ASCII
                    useful = true;
                    break;
                }
            }

            if (useful && line != command)
            {
                lines[lineCount++] = std::move(line);
            }

            start = idx + 2; // advance even if line skipped
        }

        // Detect status from last line
        if (lineCount > 0)
        {
            String last = lines[lineCount - 1];
            last.toUpperCase();
            if (last == "OK")
                status = ATStatus::OK;
            else if (last.startsWith("ERROR"))
                status = ATStatus::ERROR;
            else if (last.startsWith(">"))
                status = ATStatus::WAITING_FOR_DATA;
            else if (last == ("+CMQTTPUB: 0,0"))
                status = ATStatus::MQTT_MSG_SENT;
            else if (last == ("DOWNLOAD"))
                status = ATStatus::WAIT_FOR_POST;
            else if (last.startsWith("+HTTPACTION"))
                status = ATStatus::HTTP_READ_SUCCESS; // TODO maybe its not it/ i maigh have been an http error
            else if (last.startsWith("+CMQTTSUB: 0,0")) //TODO improve the mqtt sub result handling
                status = ATStatus::OK;
        }
    }

    // Check if any line contains a specific string
    bool isPrompt() const { return lastLine()[0] == '>'; }
    inline const String &lastLine() const { return lines[lineCount - 1]; }
    inline bool endsIn(const String &search) const
    {
        return (lastLine().indexOf(search) != -1);
    }
    bool contains(const String &search) const
    {
        for (int i = 0; i < lineCount; i++)
        {
            if (lines[i].indexOf(search) != -1)
                return true;
        }
        return false;
    }

    // Get line that starts with specific prefix
    String getLineStartingWith(const String &prefix) const
    {
        for (int i = 0; i < lineCount; i++)
        {
            if (lines[i].startsWith(prefix))
                return lines[i];
        }
        return "";
    }

    void print(int num = -1) const
    {

        Serial.printf("\n=== AT Response(%i) ===\n", num);
        for (int i = 0; i < lineCount; i++)
            Serial.println(lines[i]);
        Serial.print("Status: ");
        if (status == ATStatus::OK)
            Serial.println("OK");
        else if (status == ATStatus::ERROR)
            Serial.println("ERROR");
        else if (status == ATStatus::WAITING_FOR_DATA)
            Serial.println("WAITING_FOR_DATA");
        else if (status == ATStatus::MQTT_MSG_SENT)
            Serial.println("MQTT_MSG_SENT");
        else if (status == ATStatus::WAIT_FOR_POST)
            Serial.println("WAIT_FOR_POST");
        else if (status == ATStatus::HTTP_READ_SUCCESS)
            Serial.println("HTTP_READ_SUCCESS");
        else
            Serial.println("UNKNOWN");
        Serial.println("==================\n");
    }

    template <typename T>
    inline T as() const
    {
        return T(*this);
    }
};

// -------------------- Specific Struct Example --------------------

static String extractValue(const ATResponse &resp, const String &key)
{
    String line = resp.getLineStartingWith(key);
    if (line != "")
    {
        int colon = line.indexOf(':');
        if (colon != -1)
        {
            String value = line.substring(colon + 1);
            value.trim();
            return value;
        }
    }
    return "";
}

struct SignalQuality
{
private:
    // const ATResponse &m_Response;
public:
    int rssi;
    int ber;

    // Conversion constructor from ATResponse
    SignalQuality(const ATResponse &resp) /*:m_Response(resp)*/
    {
        rssi = 0;
        ber = 0;
        for (int i = 0; i < resp.lineCount; i++)
        {
            String line = resp.lines[i];
            if (line.startsWith("+CSQ:"))
            {
                line.remove(0, 6); // remove "+CSQ: "
                int comma = line.indexOf(',');
                rssi = line.substring(0, comma).toInt();
                ber = line.substring(comma + 1).toInt();
            }
        }
    }

    void print() const
    {
        Serial.print("RSSI: ");
        Serial.println(rssi);
        Serial.print("BER: ");
        Serial.println(ber);
    }
};

struct CPINStatus
{
    String pinState; // e.g., "READY", "SIM PIN", etc.]
    bool success = false;

    // Conversion constructor from ATResponse
    CPINStatus(const ATResponse &resp)
    {
        pinState = "";
        for (int i = 0; i < resp.lineCount; i++)
        {
            String line = resp.lines[i];
            if (line.startsWith("+CPIN:"))
            {
                line.remove(0, 6); // remove "+CPIN:"
                line.trim();       // remove leading/trailing spaces
                pinState = line;   // store the SIM state
                success = true;
                break;
            }
            else if (line.startsWith("+CME ERROR:"))
            {
                line.remove(0, 11); // remove "+CME ERROR:"
                line.trim();        // remove leading/trailing spaces
                pinState = line;    // store the SIM state
                break;
            }
        }
    }

    void print() const
    {
        Serial.print("SIM Status: ");
        Serial.println(pinState);
    }
};

struct IPAddressView
{
    char ip[32]{0}; // buffer for IP

    IPAddressView(const ATResponse &resp)
    {
        // ensure resp has at least one line
        if (1)
        {
            const char *src = resp.lines[0].c_str() + 12; // skip "+CGPADDR: 1,"
            size_t len = strlen(src);

            // limit copy to fit into ip buffer
            if (len >= sizeof(ip))
                len = sizeof(ip) - 1;

            memcpy(ip, src, len);
            ip[len] = '\0'; // null-terminate

            // remove quotes if present
            if (ip[0] == '"' && ip[len - 1] == '"')
            {
                memmove(ip, ip + 1, len - 2);
                ip[len - 2] = '\0';
            }
        }
        else
        {
            ip[0] = '\0'; // fallback empty
        }
    }
};

struct HTTPAction
{
    int method = -1;
    int code = -1;
    int dataLen = 0;

    bool success = false;

    HTTPAction(const ATResponse &response)
    {
        const String &line = response.lastLine();
        if (line.startsWith("+HTTPACTION:"))
        {
            if (sscanf(line.c_str(), "+HTTPACTION: %d,%d,%d", &method, &code, &dataLen) == 3)
            {
                success = true;
            }
        }
    }
    bool isHttpOK() const { return code >= 200 && code < 300; }
    void print() const
    {
        Serial.println("=== HTTPACTION ===");
        Serial.printf("method  = %d\n", method);
        Serial.printf("code    = %d\n", code);
        Serial.printf("dataLen = %d\n", dataLen);
        Serial.printf("success = %d\n", success);
    }
};

struct HTTPRead
{
    int dataLen = 0;      // +HTTPREAD: <len>
    String data;          // actual data payload
    bool success = false; // parsing succeeded

    HTTPRead(const ATResponse &resp)
    {
        success = false;
        dataLen = 0;
        data = "";

        // Make sure we have at least 3 lines: OK, +HTTPREAD, data
        if (resp.lineCount >= 3)
        {
            const String &okLine = resp.lines[0];   // "OK"
            const String &lenLine = resp.lines[1];  // "+HTTPREAD: <len>"
            const String &dataLine = resp.lines[2]; // actual payload

            if (okLine == "OK" && lenLine.startsWith("+HTTPREAD:"))
            {
                if (sscanf(lenLine.c_str(), "+HTTPREAD: %d", &dataLen) == 1 && dataLen > 0)
                {
                    data = dataLine;

                    // truncate if actual data longer than dataLen
                    if (data.length() > dataLen)
                        data = data.substring(0, dataLen);

                    success = true;
                }
            }
        }
    }

    void print() const
    {
        Serial.println("=== HTTPREAD ===");
        Serial.printf("dataLen = %d\n", dataLen);
        Serial.printf("success = %d\n", success);
        Serial.println("data:");
        Serial.println(data);
    }
};


struct MQTTDiscReport
{
    int count = 0;
    int index[2] = { -1, -1 };
    int state[2] = { -1, -1 };
    bool success = false;

    MQTTDiscReport(const ATResponse &resp)
    {
        for (int i = 0; i < resp.lineCount && count < 2; i++)
        {
            const String &line = resp.lines[i];
            if (line.startsWith("+CMQTTDISC:"))
            {
                int idx, st;
                if (sscanf(line.c_str(), "+CMQTTDISC: %d,%d", &idx, &st) == 2)
                {
                    index[count] = idx;
                    state[count] = st;
                    count++;
                }
            }
        }
        success = (count > 0);
    }

    void print() const
    {
        Serial.println("=== CMQTTDISC REPORT ===");
        Serial.printf("lines found: %d\n", count);
        for (int i = 0; i < count; i++)
        {
            Serial.printf("client %d  -> state=%d\n", index[i], state[i]);
        }
        Serial.printf("success = %d\n", success);
        Serial.println("========================");
    }
};
