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
    void readBMP(float &press);
    void beginBMP();
    void updateWindGust(unsigned int now);
    void reset();
    const Metrics &getMeasurements(unsigned long timestamp);

private:
    Metrics m_Measurements;
    int findMax(int arr[], int size);
};
