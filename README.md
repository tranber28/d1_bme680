BME680 Intérieur — ESP8266 D1 Mini → MQTT → Home Assistant
Capteur environnemental intérieur basé sur le BME680 (Bosch), monté sur un ESP8266 D1 Mini.
Publie température, humidité, pression, résistance gaz et altitude via MQTT avec auto-discovery Home Assistant.

Matériel
ComposantDétailMicrocontrôleurESP8266 D1 MiniCapteurBosch BME680 (I2C)Alimentation3.3V USB
Câblage I2C
BME680D1 MiniVCC3.3VGNDGNDSDAD2 (GPIO4)SCLD1 (GPIO5)
L'adresse I2C est détectée automatiquement (0x76 ou 0x77).

Fonctionnement
Phase de chauffe (au démarrage)
Le BME680 nécessite une chauffe de sa résistance gaz avant de fournir des valeurs stables.
Au boot, le firmware effectue des lectures toutes les 30 secondes et attend 3 lectures consécutives stables (ΔT < 0.5°C, ΔH < 2%) avant de passer en régime normal.
Régime normal
Une fois stable, le capteur envoie les données toutes les 10 minutes via MQTT.
Reconnexion automatique
WiFi et MQTT sont surveillés à chaque itération du loop(). En cas de déconnexion, le firmware tente de se reconnecter automatiquement sans redémarrer.

Topics MQTT publiés
TopicValeurRetaininterieur/bme680/temperature22.7 (°C)✅interieur/bme680/humidity59 (%)✅interieur/bme680/pressure975.7 (hPa)✅interieur/bme680/gas_resistance65.9 (kΩ)✅interieur/bme680/altitude318 (m)✅interieur/bme680/jsonJSON complet✅interieur/bme680/statusonline / offline (LWT)✅

Note : La pression publiée est la pression station (non ramenée au niveau de la mer).
La correction altitude est effectuée côté serveur dans brain.py.


Home Assistant Auto-Discovery
Au démarrage, le firmware publie automatiquement les configs MQTT Discovery pour :

BME680 Température
BME680 Humidité
BME680 Pression
BME680 Gaz Résistance

Les entités apparaissent automatiquement dans Home Assistant sous le device "BME680 Intérieur".

Configuration
Éditer les constantes en tête de main.cpp :
cppconst char* WIFI_SSID   = "votre_ssid";
const char* WIFI_PASS   = "votre_password";
const char* MQTT_SERVER = "192.168.1.x";   // IP du broker MQTT
const int   MQTT_PORT   = 1883;
Timings modifiables :
cpp#define WARMUP_INTERVAL  30000UL    // 30s entre lectures pendant chauffe
#define READ_INTERVAL    600000UL   // 10 min entre lectures en régime normal
#define SEND_INTERVAL    600000UL   // 10 min entre envois MQTT
#define STABLE_CHECKS    3          // Nombre de lectures stables requises
Altitude de référence locale (pour le calcul d'altitude BME680) :
cpp#define SEA_LEVEL_HPA  1013.25f    // Pression mer de référence (hPa)

Installation (PlatformIO)
bashgit clone https://github.com/ton-user/bme680-interieur
cd bme680-interieur
pio run --target upload
pio device monitor --baud 115200
platformio.ini recommandé
ini[env:d1_mini]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200
lib_deps =
    knolleary/PubSubClient @ ^2.8
    adafruit/Adafruit BME680 Library @ ^2.0.4
    adafruit/Adafruit Unified Sensor @ ^1.1.14

Intégration Coggia / brain.py
Ce capteur s'intègre dans le pipeline Coggia :
BME680 (ESP8266)
    └── MQTT (interieur/bme680/pressure + altitude)
            └── brain.py
                    └── Correction pression → niveau mer
                    └── Corrélation avec OpenMeteo
                    └── Modèles IA (LinearRegression)
                    └── ia/meteo/pressure → Home Assistant
La pression station est automatiquement corrigée vers le niveau de la mer en utilisant l'altitude dynamique publiée par le capteur.
