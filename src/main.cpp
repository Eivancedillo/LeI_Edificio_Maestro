#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// --- PINES DEL SENSOR ULTRASÓNICO ---
#define TRIG_PIN 5
#define ECHO_PIN 18

// --- AJUSTES EDITABLES (RANGO DE DETECCIÓN Y TIEMPO) ---
const int distanciaMinima = 5;
const int distanciaMaxima = 15;

// NUEVO: Tiempo que los LEDs siguen prendidos después de dejar de detectar (en milisegundos)
const unsigned long tiempoEsperaLeds = 3000;

// Variable para recordar la última vez que vimos a alguien
unsigned long ultimaVezDetectado = 0;

// MAC del Esclavo
uint8_t slaveAddress[] = {0x08, 0xD1, 0xF9, 0xD2, 0x22, 0xF4};

// Nuestra caja de datos
typedef struct struct_message
{
    bool presenciaPasillo;
} struct_message;

struct_message datosParaEnviar;
esp_now_peer_info_t peerInfo;

void setup()
{
    Serial.begin(115200);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
        return;

    memcpy(peerInfo.peer_addr, slaveAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void loop()
{
    // 1. Mandar el pulso de sonido
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // 2. Leer cuánto tardó en regresar el eco
    long duracion = pulseIn(ECHO_PIN, HIGH);

    // 3. Convertir a centímetros
    int distancia = duracion * 0.034 / 2;

    // 4. Capturar el "reloj" actual del ESP32
    unsigned long tiempoActual = millis();

    // 5. LA LÓGICA DE TIEMPO
    if (distancia >= distanciaMinima && distancia <= distanciaMaxima)
    {
        // Hay alguien ahí. Guardamos la hora exacta y prendemos luces.
        ultimaVezDetectado = tiempoActual;
        datosParaEnviar.presenciaPasillo = true;
    }
    else
    {
        // No hay nadie en este instante. ¿Pasaron ya los 3 segundos?
        if (tiempoActual - ultimaVezDetectado < tiempoEsperaLeds)
        {
            // Aún no pasan los 3 segundos, mantenemos las luces prendidas.
            datosParaEnviar.presenciaPasillo = true;
        }
        else
        {
            // Ya pasaron los 3 segundos (o más) sin ver a nadie, las apagamos.
            datosParaEnviar.presenciaPasillo = false;
        }
    }

    // 6. Enviar el dato al Esclavo
    esp_now_send(slaveAddress, (uint8_t *)&datosParaEnviar, sizeof(datosParaEnviar));

    // Pequeña pausa para estabilizar las lecturas
    delay(100);
}