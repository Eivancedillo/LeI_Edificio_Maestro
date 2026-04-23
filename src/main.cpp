#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// --- PINES DEL MAESTRO ---
#define TRIG_PIN 5
#define ECHO_PIN 18
#define IR_PIN 4 // Pin actualizado como acordamos

// --- AJUSTES EDITABLES ---
const int distanciaMinima = 5;
const int distanciaMaxima = 15;

// Tiempos de espera (en milisegundos)
const unsigned long tiempoEsperaLeds = 3000;
const unsigned long tiempoEsperaEntrada = 3000; // 3 seg para las puertas y foco

// Variables de memoria de tiempo
unsigned long ultimaVezDetectadoPasillo = 0;
unsigned long ultimaVezDetectadoEntrada = 0;

// MAC del Esclavo
uint8_t slaveAddress[] = {0x08, 0xD1, 0xF9, 0xD2, 0x22, 0xF4};

typedef struct struct_message
{
    bool presenciaPasillo;
    bool presenciaEntrada;
} struct_message;

struct_message datosParaEnviar;
esp_now_peer_info_t peerInfo;

void setup()
{
    Serial.begin(115200);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(IR_PIN, INPUT);

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
    unsigned long tiempoActual = millis();

    // ==========================================
    // 1. LÓGICA DEL PASILLO (Ultrasónico)
    // ==========================================
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    int distancia = (pulseIn(ECHO_PIN, HIGH)) * 0.034 / 2;

    if (distancia >= distanciaMinima && distancia <= distanciaMaxima)
    {
        ultimaVezDetectadoPasillo = tiempoActual;
        datosParaEnviar.presenciaPasillo = true;
    }
    else
    {
        if (tiempoActual - ultimaVezDetectadoPasillo < tiempoEsperaLeds)
        {
            datosParaEnviar.presenciaPasillo = true;
        }
        else
        {
            datosParaEnviar.presenciaPasillo = false;
        }
    }

    // ==========================================
    // 2. LÓGICA DE LA ENTRADA (Infrarrojo + Temporizador)
    // ==========================================
    if (digitalRead(IR_PIN) == LOW)
    { // Detecta algo
        ultimaVezDetectadoEntrada = tiempoActual;
        datosParaEnviar.presenciaEntrada = true;
    }
    else
    {
        // No detecta nada ahora, verificamos si aún estamos en el tiempo de espera
        if (tiempoActual - ultimaVezDetectadoEntrada < tiempoEsperaEntrada)
        {
            datosParaEnviar.presenciaEntrada = true;
        }
        else
        {
            datosParaEnviar.presenciaEntrada = false;
        }
    }

    // ==========================================
    // 3. ENVIAR TODO AL ESCLAVO
    // ==========================================
    esp_now_send(slaveAddress, (uint8_t *)&datosParaEnviar, sizeof(datosParaEnviar));

    delay(100);
}