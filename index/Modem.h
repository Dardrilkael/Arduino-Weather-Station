#include <Arduino.h>
#include "ATResponse.h"

#include "ModemAT.h"
#include "MQTT.h"
#include "httpClient.h"
#include "NTP.h"
#include "settings.h"

ModemAT modem(16, 17, 115200);
MQTT mqttClient(modem);
HttpClient http(modem);
NTP ntp(modem, "pool.ntp.org", 0);
void sendHTTPPost();

#include <FS.h>
#include <SD.h>
#include <SPI.h>
const int chipSelectPin = 32;
const int mosiPin = 23;
const int misoPin = 27;
const int clockPin = 25;
// -------------------- Setup --------------------

void listDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) return;
   
    for (uint8_t i = 0; i < numTabs; i++)
        Serial.print('\t');
    Serial.print(entry.name());
    Serial.print("\t\t");
    Serial.println(entry.size(), DEC);
    entry.close();
  }
}

void setup()
{

    SPI.begin(clockPin, misoPin, mosiPin);
    while (!SD.begin(chipSelectPin, SPI))
    {
        Serial.printf("\n  - Cartão não encontrado. tentando novamente em 3 segundos ...");
        delay(3000);
    }

    Serial.begin(115200);
    modem.begin();
    delay(1000);
    Serial.println("Ready. Type AT commands in Serial Monitor.");
    delay(400);

start:
    delay(300);
    auto resp = modem.checkSim();
    Serial.printf("O modem %s foi inicializado\n", resp ? "" : "nao");
    if (!resp)
        goto start;

     modem.setupNetwork(apn);

    IPAddressView IP = modem.query<IPAddressView>("AT+CGPADDR=1");
    Serial.printf("The newly found ip is: %s\n", IP.ip);

    SignalQuality quality = modem.query<SignalQuality>("AT+CSQ");
    quality.print();

    mqttClient.setupMqtt("brokerIP", brokerIP, brokerPort, mqttUser, mqttPass, subTopic);
    delay(400);
    mqttClient.subscribe(subTopic);

    modem.addURCHandler([&](const String &line) -> bool
                        { return mqttClient.processLine(line); });

    modem.addURCHandler([&](const String &line) -> bool
                        {return http.processLine(line); });

    mqttClient.setCallback([](const char *topic, const unsigned char *payload, int length)
                           {
        Serial.print("📩 MQTT message received!\nTopic: ");
        Serial.println(topic);
        Serial.print("Payload: ");

        for (int i = 0; i < length; i++) {
            Serial.print((char)payload[i]);
        }
        Serial.println(); });

    // if (ntp.syncTime()) {
    // Serial.println("✓ Time synchronized");
    // } else {
    //    Serial.println("✗ NTP sync failed");
    //}
}

// -------------------- Loop --------------------
bool p0 = false;
bool p = false;
bool p2 = false;
bool p3 = false;
String input2 = "";
File file;
String input = ""; // move outside loop
void loop()
{
    modem.pollURC();

    while (Serial.available())
    {
        char c = Serial.read();
        if (p || p2 || p0 || p3)
            input2 += c;
        if (c == '!')
            p = true;
        else if (c == '@')
            p2 = true;
        else if (c == '#')
            p0 = true;
        else if (c=='$')
            p3 = true;
        else
            input += c;
    }

    if (input.length() > 0)
    {
        ATResponse response = modem.sendCommand(input, 1000);
        response.print();
        input = "";
    }

    static unsigned long lastTime = millis(); // use unsigned long
    unsigned long now = millis();

    if (now - lastTime > 20000)
    {
            lastTime = now;

            mqttClient.reconnect();
            Serial.printf("The timestamp is %i\n", ntp.getCurrentTime().toEpoch());

    }
    if(p3)
    {
        p3 = false;
        input2.trim();
        File f = SD.open(input2);
        listDirectory(f,0);
        input2 = "";

    }
    if (p)
    {
        p = false;

        http.begin("http://example.com/");
        http.get([](int code, const unsigned char *data, unsigned int len, bool finalChunk)
                 {
        if (data == nullptr) {
            Serial.print("HTTP status: ");
            Serial.println(code);
        } else {
            Serial.print("Chunk (len=");
            Serial.print(len);
            Serial.print(") : ");
            // imprimir bytes
            for (int i = 0; i < len; ++i) Serial.write(data[i]);
            Serial.println();
            if (finalChunk) Serial.println(">> final chunk");
        } });
        http.end();

    }
    else if (p0)
    {
        p0 = false;
        Serial.println("Looping");
        Serial.printf("Messeage was %s published\n", mqttClient.publish("abc", input2.c_str()) ? "" : "not");
        input2 = "";
    }
    else if (p2)
    {
        p2 = false;
        file = SD.open(input2.c_str(), FILE_READ);
        if (!file)
        {
            Serial.print("Erro ao abrir arquivo:");
            Serial.println(input2);
            return;
        }

        
        http.setURL("http://metcolab.macae.ufrj.br/admin/admin/failures/upload");
        http.addHeader("Content-Type", "text/csv");
        http.addHeader("Connection", "close");
        http.addHeader("X-Filename",file.name());
        http.addHeader("X-Device-Name", "Station445");
        http.addHeader("X-Cmd-Id", "123");


        http.postByParts([](size_t offset, uint8_t *buf, size_t len) -> int {    
                    int n = file.read(buf,len);
                    return  n;       
                }, (size_t)file.size(),65000, httpCallback);

       // http.end();
        input2 = "";
    }
}
