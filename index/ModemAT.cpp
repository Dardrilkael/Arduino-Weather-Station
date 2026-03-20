#include "ModemAT.h"

ModemAT::ModemAT(int rx, int tx, unsigned long baudRate)
    : m_Serial(1), m_RxPin(rx), m_TxPin(tx), m_BaudRate(baudRate)
{
}

void ModemAT::begin()
{
    m_Serial.begin(m_BaudRate, SERIAL_8N1, m_RxPin, m_TxPin);
}

void ModemAT::end()
{
    m_Serial.end();
}

bool ModemAT::setupNetwork(const char *apn, const char *user, const char *pass)
{
    // 1️⃣ Attach to the packet domain
    ATResponse resp = sendCommand("AT+CGATT=1", 10000);
    if (!resp)
    {
        Serial.println("Failed to attach to packet service");
        return false;
    }
    resp.print();

    // 2️⃣ Define PDP context (APN)
    String cmd = "AT+CGDCONT=1,\"IP\",\"";
    cmd += apn;
    cmd += "\"";
    resp = sendCommand(cmd.c_str());
    if (!resp)
    {
        Serial.println("Failed to set PDP context (APN)");
        return false;
    }
    resp.print();

    // 3️⃣ (Optional) set authentication if required
    // Only send if username or password are provided
    if ((user && strlen(user) > 0) || (pass && strlen(pass) > 0))
    {
        String authCmd = "AT+CGAUTH=1,1,\"";
        authCmd += user ? user : "";
        authCmd += "\",\"";
        authCmd += pass ? pass : "";
        authCmd += "\"";
        resp = sendCommand(authCmd.c_str());
        if (!resp)
        {
            Serial.println("Failed to set authentication");
            return false;
        }
    }
    resp.print();

    // 4️⃣ Activate PDP context
    resp = sendCommand("AT+CGACT=1,1", 15000);
    if (!resp)
    {
        Serial.println("Failed to activate PDP context");
        return false;
    }
    resp.print();

    Serial.println("Network setup successful!");
    return true;
}

String ModemAT::getResponse() const
{
    return m_Buffer;
}
bool ModemAT::writeData(uint8_t* data,size_t size, unsigned long timeout)
{
    //pollURC();
    //m_Buffer.clear();
    m_Serial.write(data,size);
    return true;
}

ATResponse ModemAT::sendCommand(const char *command, unsigned long timeout, unsigned long quietTimeout,std::function<bool(const String&)> stopFn )
{
    return sendCommand(String(command), timeout, quietTimeout,stopFn);
}
ATResponse ModemAT::sendCommand(const String &command, unsigned long timeout, unsigned long quietTimeout, std::function<bool(const String&)> stopFn )
{
    pollURC();
    m_Buffer.clear();
    m_Serial.println(command);
    unsigned long start = millis();

    unsigned long lastRead = millis();

    while (millis() - start < timeout)
    {
        // Read all available data
        while (m_Serial.available())
        {
            char c = m_Serial.read();
            m_Buffer += c;
            if (quietTimeout == 3001 || quietTimeout == 9002 || timeout==2234)
                Serial.printf("%02x ", (uint8_t)c);
            lastRead = millis(); // reset quiet timer when we get data
        }


        if (stopFn && stopFn(m_Buffer)) {
            Serial.println("\n[Stopped by custom stopFn]");
            break;
        }
        // Serial.println();
        //  If we see clear terminators — break early
        // TODO verify if +CMQTTCONNECT: 0,0\r\n should have the \r\n and add maybe add tehe sub version
        if (!stopFn && (  // TODO may need to remove this line in order to prevent breaking before time
            m_Buffer.endsWith("OK\r\n\r") ||
            m_Buffer.endsWith("DOWNLOAD\r\n\r") ||
            m_Buffer.endsWith("ERROR\r\n\r") ||
            m_Buffer.endsWith("NO CARRIER\r\n") ||
            m_Buffer.endsWith("+CMQTTCONNECT: 0,0\r\n") ||
            m_Buffer.endsWith("READY\r\n") ||
            m_Buffer.endsWith("+CMQTTSTART: 0") ||
            m_Buffer.endsWith(">") ||
            (m_Buffer.endsWith("\r\n") && m_Buffer.indexOf("+HTTPACTION:") != -1)))
        {
            Serial.println("\n[Response terminated by default keyword]");
            break;
        }

        // If nothing new has arrived for some time → assume end of response
        if (millis() - lastRead > quietTimeout && m_Buffer.length() > 0)
        {
            //TODO Serial.println("\n[Response timeout - no more data]");
            break;
        }

        yield();  // let background tasks run (important for ESP)
        delay(3); // short delay prevents busy-looping
    }

    return ATResponse(std::move(m_Buffer), command);
}

bool ModemAT::checkSim()
{
    ATResponse resp = sendCommand("AT+CPIN?", 10000);
    return resp.contains("READY");
}

void ModemAT::clearSerialBuffer()
{
    while (getSerial().available())
    {
        getSerial().read();
        delay(1);
    }
}

void ModemAT::handleURC(const String &line)
{

    for (auto &handler : urcHandlers)
    {
        if (handler(line))
        {
            // mensagem consumida, não repassa
            return;
        }
    }
    // ninguém consumiu → log default
    Serial.println(line);
}

void ModemAT::pollURC()
{
    while (m_Serial.available())
    {
        char c = m_Serial.read();
        m_URCBuffer += c;
        if (c == '\n')
        {
            handleURC(m_URCBuffer);
            m_URCBuffer = "";
        }
    }
}

void ModemAT::addURCHandler(URCHandler h)
{
    urcHandlers.push_back(h);
}

void ModemAT::writeRaw(const uint8_t *buf, size_t len)
{
    m_Serial.write(buf, len);
}
