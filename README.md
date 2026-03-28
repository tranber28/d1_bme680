# BME680 Intérieur

Capteur BME680 (température, humidité, pression, gaz) sur D1 Mini (ESP8266).

## Hardware
- D1 Mini (ESP8266)
- BME680 (I2C: SDA=GPIO4, SCL=GPIO5)

## Fonctionnalités
- Lecture toutes les 10 minutes
- Deep sleep entre les lectures
- Publication MQTT vers broker local

## Installation
```bash
cd ~/projects/bme680_interieur
pio run --target upload
```

## Monitor
```bash
pio device monitor
```
