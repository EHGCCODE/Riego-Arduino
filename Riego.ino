#include <WiFi.h>
#include <HTTPClient.h>
#include <ThingerESP32.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>

// --- Credenciales Wi-Fi y Thinger.io ---
#define WIFI_SSID          "Iot"
#define WIFI_PASSWORD      "RiegoIOT"
#define USERNAME           "gabozin"
#define DEVICE_ID          "ESP32"
#define DEVICE_CREDENTIAL  "ESP32RIEGO"

// --- URL servidor Flask ---
const char* SERVER_URL = "http://34.67.216.195:8000/update";
const char* SISTEMA_URL = "http://34.67.216.195:8000/sistema";
const char* RIEGO_AUTO_URL = "http://34.67.216.195:8000/riego-auto";
const char* RIEGO_MANUAL_URL = "http://34.67.216.195:8000/riego-manual";


// --- Pines ---
const int PIN_SENSOR_SUELO   = 34;
const int PIN_RELE           = 15;
const int PIN_DHT            = 4;
const int FLOW_SENSOR_PIN1   = 32;
const int FLOW_SENSOR_PIN2   = 33;

// --- Sensores ---
#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);
BH1750 lightMeter;

// --- Umbrales calibrables ---
const int VALOR_AIRE           = 3500;
const int VALOR_AGUA           = 1500;
const int UMBRAL_RIEGO_BAJO    = 45; 
const int UMBRAL_RIEGO_ALTO    = 75; 
const uint16_t UMBRAL_LUX_SOL  = 1000;

// --- Variables globales ---
int humedadBruta = 0;
int humedadPorcentaje = 0;
uint16_t luzLux = 0;
float temperatura = 0;
float humedadAtmosferica = 0;
bool regando = false;
bool solicitudRiegoManual = false;

// --- Flujo de agua ---
volatile unsigned long pulseCount1 = 0;
volatile unsigned long pulseCount2 = 0;
const float CALIBRATION_FACTOR = 450.0;

// --- Thinger.io ---
ThingerESP32 thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

// --- Interrupciones flujo ---
void IRAM_ATTR flowSensor1Interrupt() { pulseCount1++; }
void IRAM_ATTR flowSensor2Interrupt() { pulseCount2++; }

// --- Lectura humedad suelo ---
int leerHumedadPorcentaje() {
    long suma = 0;
    for (int i = 0; i < 10; i++) {
        suma += analogRead(PIN_SENSOR_SUELO);
        delay(10);
    }
    humedadBruta = suma / 10;
    int pct = map(humedadBruta, VALOR_AIRE, VALOR_AGUA, 0, 100);
    return constrain(pct, 0, 100);
}

// --- Detectar sol ---
bool haySolPorLux() {
    luzLux = lightMeter.readLightLevel();
    return (luzLux >= UMBRAL_LUX_SOL);
}

// --- Control de riego ---
void iniciarRiego() {
    if (!regando) {
        digitalWrite(PIN_RELE, HIGH);
        regando = true;
        Serial.println("RIEGO ACTIVADO.");
    }
}
void detenerRiego() {
    if (regando) {
        digitalWrite(PIN_RELE, LOW);
        regando = false;
        Serial.println("RIEGO DESACTIVADO.");
        pulseCount1 = pulseCount2 = 0;
    }
}

// --- Consultar si el sistema está activo ---
bool sistemaActivoRemoto() {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.begin(SISTEMA_URL);
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        http.end();
        return payload.indexOf("\"activo\":true") != -1;
    }
    http.end();
    return false;
}

// --- Consultar si riego manual está activo ---
bool riegoManualActivoRemoto() {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.begin(RIEGOMANUAL_URL);
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        http.end();
        return payload.indexOf("\"activar\":true") != -1;
    }
    http.end();
    return false;
}

// --- Consultar si riego automático está activo ---
bool riegoAutoActivoRemoto() {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.begin(RIEGO_AUTO_URL);
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        http.end();
        return payload.indexOf("\"auto\":true") != -1;
    }
    http.end();
    return false;
}

// --- Envío a servidor Flask ---
void enviarDatosAServidor() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    String payload = "{";
    payload += "\"temperatura\":" + String(temperatura, 1) + ",";
    payload += "\"humedad_suelo\":" + String(humedadPorcentaje) + ",";
    payload += "\"humedad_ambiental\":" + String(humedadAtmosferica, 1) + ",";
    payload += "\"luminosidad\":" + String(luzLux) + ",";
    payload += "\"medidor1\":" + String(pulseCount1 / CALIBRATION_FACTOR, 2) + ",";
    payload += "\"medidor2\":" + String(pulseCount2 / CALIBRATION_FACTOR, 2) + ",";
    payload += "\"activaciones\":" + String(regando ? 1 : 0);
    payload += "}";

    int code = http.POST(payload);
    if (code > 0) {
        Serial.printf("POST %d → %s\n", code, http.getString().c_str());
    } else {
        Serial.printf("Error en POST: %s\n", http.errorToString(code).c_str());
    }
    http.end();
}

// --- SETUP ---
void setup() {
    Serial.begin(115200);

    pinMode(PIN_RELE, OUTPUT);
    digitalWrite(PIN_RELE, LOW); 
    pinMode(FLOW_SENSOR_PIN1, INPUT_PULLUP);
    pinMode(FLOW_SENSOR_PIN2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN1), flowSensor1Interrupt, RISING);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN2), flowSensor2Interrupt, RISING);

    dht.begin();
    Wire.begin(21, 22);
    if (!lightMeter.begin()) {
        Serial.println("Error iniciando BH1750");
    }

    thing.add_wifi(WIFI_SSID, WIFI_PASSWORD);

    thing["humedad_suelo"]    >> outputValue(humedadPorcentaje);
    thing["humedad_ambiente"] >> outputValue(humedadAtmosferica);
    thing["temperatura"]      >> outputValue(temperatura);
    thing["luz_lux"]          >> outputValue(luzLux);
    thing["estado_riego"]     >> outputValue(regando);
    thing["litros_sensor1"]   >> [](pson& out){ out = pulseCount1 / CALIBRATION_FACTOR; };
    thing["litros_sensor2"]   >> [](pson& out){ out = pulseCount2 / CALIBRATION_FACTOR; };
    thing["riego_manual"] << [](pson& in){
        if ((bool)in) { 
            solicitudRiegoManual = true;
            Serial.println("Solicitud riego manual aceptada.");
        } else { 
            detenerRiego();
            Serial.println("Riego manual desactivado desde Thinger.io.");
        }
    };

    Serial.println("Setup completado.");
}

// --- LOOP ---
void loop() {
    thing.handle();
    humedadPorcentaje  = leerHumedadPorcentaje();
    humedadAtmosferica = dht.readHumidity();
    temperatura        = dht.readTemperature();
    bool sol = haySolPorLux();

    bool sistemaActivo = sistemaActivoRemoto();
    bool manualActivo = riegoManualActivoRemoto();
    bool autoActivo = riegoAutoActivoRemoto();

    // --- Control de Riego ---
    if (sistemaActivo) {
        if (manualActivo) { 
            iniciarRiego();
            Serial.println("Riego manual activo por UI.");
        } else if (autoActivo) { 
            if (!regando && humedadPorcentaje < UMBRAL_RIEGO_BAJO && !sol) {
                Serial.println("Inicio riego automático (humedad baja y sin sol).");
                iniciarRiego();
            } else if (regando && (humedadPorcentaje >= UMBRAL_RIEGO_ALTO || sol)) {
                if (humedadPorcentaje >= UMBRAL_RIEGO_ALTO) Serial.println("Detener por humedad alta.");
                if (sol) Serial.println("Detener por luz solar.");
                detenerRiego();
            }
        } else { 
            detenerRiego();
            Serial.println("Riego inactivo (ni manual ni automático).");
        }
    } else { // System is inactive
        detenerRiego();
        Serial.println("Sistema desactivado: no se permite riego.");
    }
    
    if(solicitudRiegoManual && !regando) { 
        iniciarRiego();
        solicitudRiegoManual = false; 
    }


    enviarDatosAServidor();
    delay(10000); // Espera 10 segundos para evitar saturar el servidor
}
