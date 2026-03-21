#pragma once
#include "data.h"

class Sensors
{
public:
    struct
    {
        bool bmp = false;
        // Add other sensor status flags here if needed
    } bits;

    Sensors() = default;

    int readWindDirection();
    void init();
    void readDHT(float &hum, float &temp);
    void readBMP(float &press, float &temp);
    void beginBMP();
    void updateWindGust(unsigned long now); // Fix #2: was unsigned int — truncates millis()
    void reset();
    const Metrics &getMeasurements(unsigned long timestamp);

private:
    Metrics m_Measurements;
    int findMax(int arr[], int size);
};