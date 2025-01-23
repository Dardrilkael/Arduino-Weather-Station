#pragma once

int getWindDir();
void setupSensors();
void anemometerChange();
void pluviometerChange();
void DHTRead(float& hum, float& temp);
void BMPRead(float& press);
void beginBMP();
float find_index_sum_and_distance(unsigned int * data_array, int size);


union  Sensors{
    struct {
        bool div  : 1;
        bool vvt  : 1;
        bool dht  : 1;
        bool bmp  : 1;
        bool aux1 : 1;
        bool aux2 : 1;
        bool aux3 : 1;
        bool aux4 : 1;
    } bits { 0 };
};