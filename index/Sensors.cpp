#include "Sensors.h"
#include "constants.h"
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <cfloat>
#include "pch.h"
#include "esp_attr.h"
// Temperatura e Humidade
DHT dht(DHTPIN, DHTTYPE);

// Pressao
Adafruit_BMP085 bmp;

// Pluviometro
volatile unsigned long lastPVLImpulseTime = 0;
volatile unsigned int rainCounter = 0;

// Anemometro (Velocidade do vento)
volatile int anemometerCounter = 0;
volatile unsigned long lastVVTImpulseTime = 0;
unsigned int gustIndex = 0;
unsigned int previousCounter = 0;
int rps[GUST_ARRAY_SIZE]{0};

void IRAM_ATTR onAnemometerChange()
{
  unsigned long currentMillis = millis();
  unsigned long deltaTime = currentMillis - lastVVTImpulseTime;
  if (deltaTime >= DEBOUNCE_DELAY)
  {
    anemometerCounter++;
    lastVVTImpulseTime = currentMillis;
  }
}

void IRAM_ATTR onPluviometerChange()
{
  unsigned long currentMillis = millis();
  if (currentMillis - lastPVLImpulseTime >= DEBOUNCE_DELAY)
  {
    rainCounter++;
    lastPVLImpulseTime = currentMillis;
  }
}

void Sensors::reset()
{
  noInterrupts();
  rainCounter = 0;
  anemometerCounter = 0;
  interrupts();

  gustIndex = 0;
  previousCounter = 0;
  memset(rps, 0, sizeof(rps));
}

void Sensors::init()
{
  pinMode(PLV_PIN, INPUT_PULLDOWN);
  pinMode(ANEMOMETER_PIN, INPUT_PULLUP);

  noInterrupts(); // Disable interrupts while setting volatile vars
  lastPVLImpulseTime = millis();
  lastVVTImpulseTime = millis();
  interrupts(); // Re-enable interrupts

  attachInterrupt(digitalPinToInterrupt(PLV_PIN), onPluviometerChange, RISING);
  attachInterrupt(digitalPinToInterrupt(ANEMOMETER_PIN), onAnemometerChange, FALLING);

  logDebugln("Iniciando DHT");
  dht.begin();

  logDebugln("Iniciando BMP");
  beginBMP();
}

void Sensors::beginBMP()
{
  bits.bmp = bmp.begin();
  if (!bits.bmp)
  {
    logDebugln("Could not find a valid BMP180 sensor, check wiring!");
  }
}

int Sensors::readWindDirection()
{
  long long val = analogRead(VANE_PIN);
  int closestIndex = 0;
  int closestDifference = std::abs(val - adc[0]);

  for (int i = 1; i < NUMDIRS; i++)
  {
    int difference = std::abs(val - adc[i]);
    if (difference < closestDifference)
    {
      closestDifference = difference;
      closestIndex = i;
    }
  }
  return closestIndex;
}

void Sensors::readDHT(float &hum, float &temp)
{
  hum = dht.readHumidity();
  temp = dht.readTemperature();
  OnDebug(if (isnan(hum) || isnan(temp)) {
    logDebugln("Falha ao ler o sensor DHT!");
  })
}

void Sensors::readBMP(float &press, float &temp)
{
  if (bits.bmp)
  {
    float pressure = bmp.readPressure() / 100.0;
    float temperature = bmp.readTemperature();
    if (isnan(pressure))
    {
      logDebugln("Falha ao ler o sensor BMP180!");
      press = NAN;
      temp = NAN;
      return;
    }
    press = pressure;
    temp = temperature;
  }
  else
  {
    beginBMP();
    press = NAN;
    temp = NAN;
  }
}

void Sensors::updateWindGust(unsigned int now)
{
  static unsigned int lastAssignement = 0;

  int gustInterval = now - lastAssignement;
  if (gustInterval >= 3000)
  {
    lastAssignement = now;
    int snapshot = anemometerCounter; // Safe copy
    int revolutions = snapshot - previousCounter;
    previousCounter = snapshot;

    gustIndex = gustIndex % GUST_ARRAY_SIZE;
    rps[gustIndex++] = revolutions;
  }
}

const Metrics &Sensors::getMeasurements(unsigned long timestamp)
{
  int anemometerSnapshot, rainSnapshot;

  noInterrupts();
  anemometerSnapshot = anemometerCounter;
  rainSnapshot = rainCounter;
  interrupts();

  m_Measurements.timestamp = timestamp;
  m_Measurements.wind_speed = 3.052 * (ANEMOMETER_CIRC * anemometerSnapshot) / (config.interval / 1000.0); // m/s
  m_Measurements.wind_gust = WIND_GUST_FACTOR * ANEMOMETER_CIRC * findMax(rps, sizeof(rps) / sizeof(int));
  m_Measurements.rain_acc = rainSnapshot * VOLUME_PLUVIOMETRO;
  m_Measurements.wind_dir = readWindDirection();

  float temperatura1, temperatura2;
  readDHT(m_Measurements.humidity,temperatura1);
  readBMP(m_Measurements.pressure, temperatura2);


    // Choose temperatura1 if it's valid, otherwise fallback to temperatura2
  if (!isnan(temperatura1)) {
    m_Measurements.temperature = temperatura1;
  } else {
    m_Measurements.temperature = temperatura2;
  }
  

  reset();
  return m_Measurements;
}

int Sensors::findMax(int arr[], int size)
{
  if (size <= 0)
  {
    logDebugln("Array is empty.");
    return 0;
  }
  int max = arr[0];
  for (int i = 1; i < size; i++)
  {
    if (arr[i] > max)
    {
      max = arr[i];
    }
  }
  return max;
}