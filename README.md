# Arduino Weather Station

This project uses an ESP32 to measure various weather parameters and send the data via MQTT. The parameters measured include:

- Wind Speed
- Wind Gust
- Humidity
- Temperature
- Wind Direction
- Rain
- Atmospheric Pressure

## Components

- ESP32
- Wind Speed Sensor
- Wind Gust Sensor
- Humidity Sensor
- Temperature Sensor
- Wind Direction Sensor
- Rain Sensor
- Atmospheric Pressure Sensor

## Setup

1. Connect the sensors to the ESP32 according to the wiring diagram.
2. Configure the MQTT settings in the configt.txt.
3. Upload the code to the ESP32.
4. Monitor the weather data via the MQTT broker.

## Code

The code for this project can be found in the `src` directory.


## License

This project is licensed under the MIT License.