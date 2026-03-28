#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME680.h>

// ── WiFi / MQTT ──────────────────────────────────────────────────────────────
const char* WIFI_SSID   = "xxxxx";
const char* WIFI_PASS   = "xxxxx";
const char* MQTT_SERVER = "xxxxx";
const int   MQTT_PORT   = 1883;
const char* MQTT_ID     = "bme680-interieur";

// ── Topics ───────────────────────────────────────────────────────────────────
const char* TOPIC_TEMP   = "interieur/bme680/temperature";
const char* TOPIC_HUM    = "interieur/bme680/humidity";
const char* TOPIC_PRES   = "interieur/bme680/pressure";
const char* TOPIC_GAS    = "interieur/bme680/gas_resistance";
const char* TOPIC_ALT    = "interieur/bme680/altitude";
const char* TOPIC_JSON   = "interieur/bme680/json";
const char* TOPIC_STATUS = "interieur/bme680/status";

// ── Timings ──────────────────────────────────────────────────────────────────
#define WARMUP_INTERVAL  30000UL    // 30s entre lectures pendant chauffe
#define READ_INTERVAL    600000UL   // 10 min entre lectures en régime normal
#define SEND_INTERVAL    600000UL   // 10 min entre envois MQTT

// ── Stabilisation ────────────────────────────────────────────────────────────
#define STABLE_CHECKS    3
#define STABLE_TOL_T     0.5f
#define STABLE_TOL_H     2.0f

// ── Sanity check ─────────────────────────────────────────────────────────────
#define TEMP_MIN   -10.0f
#define TEMP_MAX    50.0f
#define HUM_MIN      0.0f
#define HUM_MAX    100.0f
#define PRES_MIN   800.0f
#define PRES_MAX  1100.0f

// ── Altitude de référence locale (m) ─────────────────────────────────────────
#define SEA_LEVEL_HPA  1013.25f

// ─────────────────────────────────────────────────────────────────────────────

Adafruit_BME680 bme;
WiFiClient      espClient;
PubSubClient    mqtt(espClient);

bool     isStable    = false;
uint32_t lastRead    = 0;
uint32_t lastSend    = 0;
uint8_t  stableCount = 0;

float prevTemp = 0.0f;
float prevHum  = 0.0f;

// ── WiFi ─────────────────────────────────────────────────────────────────────
void connectWifi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("\nWiFi [%s]...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(" OK %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(" ECHEC WiFi, reboot...");
        ESP.restart();
    }
}

// ── MQTT ─────────────────────────────────────────────────────────────────────
void connectMqtt() {
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setKeepAlive(60);
    mqtt.setBufferSize(512);  // défaut 256 — trop petit pour les payloads autodiscovery

    uint8_t attempts = 0;
    while (!mqtt.connected() && attempts < 10) {
        Serial.print("MQTT...");
        if (mqtt.connect(MQTT_ID, nullptr, nullptr, TOPIC_STATUS, 0, true, "offline")) {
            Serial.println(" OK");
            mqtt.publish(TOPIC_STATUS, "online", true);
        } else {
            Serial.printf(" echec rc=%d\n", mqtt.state());
            delay(2000);
        }
        attempts++;
    }
}

// ── Home Assistant Auto-Discovery ─────────────────────────────────────────────
void sendAutoDiscovery() {
    // Payload device commun (réutilisé dans chaque config)
    // On envoie 4 capteurs : temperature, humidity, pressure, gas_resistance
    
    struct {
        const char* suffix;
        const char* name;
        const char* topic;
        const char* unit;
        const char* device_class; // nullptr si pas de device_class standard
    } sensors[] = {
        { "temperature",   "BME680 Température",    TOPIC_TEMP, "°C",  "temperature"  },
        { "humidity",      "BME680 Humidité",       TOPIC_HUM,  "%",   "humidity"     },
        { "pressure",      "BME680 Pression",       TOPIC_PRES, "hPa", "pressure"     },
        { "gas_resistance","BME680 Gaz Résistance", TOPIC_GAS,  "kΩ",  nullptr        },
    };

    char config_topic[128];
    char payload[512];

    for (auto& s : sensors) {
        snprintf(config_topic, sizeof(config_topic),
            "homeassistant/sensor/bme680_interieur/%s/config", s.suffix);

        if (s.device_class) {
            snprintf(payload, sizeof(payload),
                "{"
                "\"name\":\"%s\","
                "\"unique_id\":\"bme680_interieur_%s\","
                "\"state_topic\":\"%s\","
                "\"unit_of_measurement\":\"%s\","
                "\"device_class\":\"%s\","
                "\"expire_after\":1800,"
                "\"device\":{"
                  "\"identifiers\":[\"bme680_interieur\"],"
                  "\"name\":\"BME680 Intérieur\","
                  "\"model\":\"BME680\","
                  "\"manufacturer\":\"Bosch\""
                "}"
                "}",
                s.name, s.suffix, s.topic, s.unit, s.device_class);
        } else {
            snprintf(payload, sizeof(payload),
                "{"
                "\"name\":\"%s\","
                "\"unique_id\":\"bme680_interieur_%s\","
                "\"state_topic\":\"%s\","
                "\"unit_of_measurement\":\"%s\","
                "\"expire_after\":1800,"
                "\"device\":{"
                  "\"identifiers\":[\"bme680_interieur\"],"
                  "\"name\":\"BME680 Intérieur\","
                  "\"model\":\"BME680\","
                  "\"manufacturer\":\"Bosch\""
                "}"
                "}",
                s.name, s.suffix, s.topic, s.unit);
        }

        bool ok = mqtt.publish(config_topic, payload, true);
        Serial.printf("  autodiscovery %s -> %s\n", s.suffix, ok ? "OK" : "ECHEC");
    }
}

// ── Validation des valeurs ────────────────────────────────────────────────────
bool valuesOk(float temp, float hum, float pres) {
    return (temp >= TEMP_MIN && temp <= TEMP_MAX)
        && (hum  >= HUM_MIN  && hum  <= HUM_MAX)
        && (pres >= PRES_MIN && pres <= PRES_MAX);
}

// ── Publication MQTT ──────────────────────────────────────────────────────────
void publishData(float temp, float hum, float pres, float gas, float alt) {
    char buf[32];

    dtostrf(temp, 5, 1, buf); mqtt.publish(TOPIC_TEMP, buf, true);
    dtostrf(hum,  4, 0, buf); mqtt.publish(TOPIC_HUM,  buf, true);
    dtostrf(pres, 7, 1, buf); mqtt.publish(TOPIC_PRES, buf, true);
    dtostrf(gas,  7, 1, buf); mqtt.publish(TOPIC_GAS,  buf, true);
    dtostrf(alt,  6, 0, buf); mqtt.publish(TOPIC_ALT,  buf, true);

    char json[256];
    snprintf(json, sizeof(json),
        "{\"temperature\":%.1f,\"humidity\":%.0f,\"pressure\":%.1f"
        ",\"gas_resistance\":%.1f,\"altitude\":%.0f}",
        temp, hum, pres, gas, alt);
    mqtt.publish(TOPIC_JSON, json, true);

    Serial.printf(">>> MQTT envoye : T=%.1f H=%.0f P=%.1f G=%.1f Alt=%.0f\n",
                  temp, hum, pres, gas, alt);
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n== BME680 Interieur ==");

    Wire.begin(4, 5);      // SDA=GPIO4(D2), SCL=GPIO5(D1)
    Wire.setClock(100000); // 100kHz — plus stable que 400kHz pour le BME680

    if (!bme.begin(0x76) && !bme.begin(0x77)) {
        Serial.println("BME680 NON TROUVE ! Vérifiez le câblage I2C.");
        // On continue quand même, loop() gérera l'erreur de lecture
    } else {
        Serial.println("BME680 OK");
    }

    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 300);  // 320°C pendant 300ms (plus stable)

    connectWifi();
    connectMqtt();
    sendAutoDiscovery();

    Serial.println("Phase de chauffe / stabilisation (lectures toutes les 30s)...");

    // Forcer une première lecture immédiate dès le loop()
    lastRead = millis() - WARMUP_INTERVAL;
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    // Reconnexions
    if (WiFi.status() != WL_CONNECTED) connectWifi();
    if (!mqtt.connected())             connectMqtt();

    mqtt.loop();

    uint32_t now      = millis();
    uint32_t interval = isStable ? READ_INTERVAL : WARMUP_INTERVAL;

    if (now - lastRead < interval) return;
    lastRead = now;

    // ── Lecture BME680 (non-bloquant pour ne pas déclencher le watchdog) ────────
    unsigned long endTime = bme.beginReading();
    if (endTime == 0) {
        Serial.println("Erreur beginReading — retry dans 5s");
        lastRead = now - interval + 5000;
        return;
    }
    // Attente active avec yield() pour nourrir le watchdog ESP8266
    while (millis() < endTime) {
        yield();
        delay(10);
    }
    if (!bme.endReading()) {
        Serial.println("Erreur endReading — retry dans 5s");
        lastRead = now - interval + 5000;
        return;
    }

    float temp = bme.temperature;
    float hum  = bme.humidity;
    float pres = bme.pressure / 100.0f;
    float gas  = bme.gas_resistance / 1000.0f;
    float alt  = bme.readAltitude(SEA_LEVEL_HPA);

    Serial.printf("[%s] T=%.1f°C H=%.0f%% P=%.1fhPa G=%.1fkΩ Alt=%.0fm\n",
                  isStable ? "STABLE" : "chauffe", temp, hum, pres, gas, alt);

    // ── Détection stabilisation ───────────────────────────────────────────────
    if (!isStable) {
        float diffT = fabsf(temp - prevTemp);
        float diffH = fabsf(hum  - prevHum);

        if (diffT < STABLE_TOL_T && diffH < STABLE_TOL_H) {
            stableCount++;
            Serial.printf("  stable %d/%d (dT=%.2f dH=%.2f)\n",
                          stableCount, STABLE_CHECKS, diffT, diffH);
        } else {
            stableCount = 0;
            Serial.printf("  instable (dT=%.2f dH=%.2f)\n", diffT, diffH);
        }

        if (stableCount >= STABLE_CHECKS) {
            isStable = true;
            Serial.println("*** CAPTEUR STABLE — passage en régime normal ***");
            // Envoi immédiat dès que stable
            lastSend = now - SEND_INTERVAL;
        }
    }

    prevTemp = temp;
    prevHum  = hum;

    // ── Envoi MQTT (seulement si stable) ─────────────────────────────────────
    if (!isStable) return;

    if (now - lastSend < SEND_INTERVAL) return;
    lastSend = now;

    if (!valuesOk(temp, hum, pres)) {
        Serial.printf("Valeurs hors limites ! T=%.1f H=%.0f P=%.1f — skip\n",
                      temp, hum, pres);
        return;
    }

    publishData(temp, hum, pres, gas, alt);
}
