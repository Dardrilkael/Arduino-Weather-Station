#include "sensores.h"
#include "constants.h"
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <cfloat>

#define periodMax 2000
unsigned int Periods[periodMax]{~0};
int periodIndex = 0;

//Temperatura e Humidade
DHT dht(DHTPIN, DHTTYPE);

//Pressao
Adafruit_BMP085 bmp;

// Pluviometro
unsigned long lastPVLImpulseTime = 0;
unsigned int rainCounter = 0;

// Anemometro (Velocidade do vento)
float anemometerCounter = 0.0f;
unsigned long lastVVTImpulseTime = 0;
unsigned long smallestDeltatime=4294967295;

Sensors sensors;

void setupSensors(){
  // Inciando DHT
  Serial.println("Iniciando DHT");
  dht.begin();

  // Iniciando BMP
  Serial.println('Iniciando BMP ');
  beginBMP();
}

void beginBMP()
{
  sensors.bits.bmp= bmp.begin();
  if (!sensors.bits.bmp) {
    Serial.println("Could not find a valid BMP180 sensor, check wiring!");
  }
}

// Controllers

int getWindDir() {
  long long val, x, reading;
  val = analogRead(VANE_PIN);
  int closestIndex = 0;
  int closestDifference = std::abs(val - adc[0]);

  for (int i = 1; i < NUMDIRS; i++) {
    int difference = std::abs(val - adc[i]);
    if (difference < closestDifference) {
      closestDifference = difference;
      closestIndex = i;
    }
  }
  return closestIndex;
}

void anemometerChange() {
  unsigned long currentMillis = millis();
  unsigned long deltaTime = currentMillis - lastVVTImpulseTime;
  if (deltaTime >= DEBOUNCE_DELAY) {
    Periods[(periodIndex++)]=deltaTime;
    periodIndex =periodMax%2000;
    smallestDeltatime = min(deltaTime, smallestDeltatime);
    anemometerCounter++;
    lastVVTImpulseTime = currentMillis;
  }
}

void pluviometerChange() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastPVLImpulseTime >= DEBOUNCE_DELAY) {
    rainCounter++;
    lastPVLImpulseTime = currentMillis;
  }
}

//Humidade, Temperatura
void DHTRead(float& hum, float& temp) {
  hum = dht.readHumidity();        // umidade relativa
  temp = dht.readTemperature();  //  temperatura em graus Celsius
  if (isnan(hum) || isnan(temp)){
    Serial.println("Falha ao ler o sensor DHT!");
  }
}

//Pressao
void BMPRead(float& press)
{
  if(sensors.bits.bmp){
    // float temperature = bmp.readTemperature(); // isnan(temperature)
    float pressure = bmp.readPressure() / 100.0; // Convert Pa to hPa
    if (isnan(pressure)) {
      Serial.println("Falha ao ler o sensor BMP180!");
      press = -1;
      return;
    }
    press = pressure;
  }else{
    beginBMP();
  }
}


float find_index_sum_and_distance(unsigned int *data_array, int size) {
  Serial.print("Size: ");Serial.println(size);
    int min_index = 0;
    for (int i = 1; i < size; ++i) {
        if (data_array[i] < data_array[min_index]) {
            min_index = i;
        }
    }
    printf("Index of the minimum element: %d\n", min_index);
    int total_sum = data_array[min_index];
    int left_index = min_index - 1;
    int right_index = min_index + 1;
    int result = 0;
    int distance = 1;

    while (total_sum < 2000 && (left_index >= 0 || right_index < size)) {
        if (left_index >= 0) {
            total_sum += data_array[left_index];
            result += 1.0 / data_array[left_index];
            distance += 1;
            --left_index;
        }

        if (right_index < size) {
            total_sum += data_array[right_index];
            result += 1.0 / data_array[right_index];
            ++right_index;
            ++distance;
        }
    }

    return  (float)distance / (float)total_sum;

}