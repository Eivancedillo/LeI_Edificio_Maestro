#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// --- PINES DEL MAESTRO ---
#define TRIG_PIN 5
#define ECHO_PIN 18
#define IR_PIN 4
#define LDR_PIN 34
#define BOTON_PIN 23
#define BUZZER_PIN 25 // Pin para el transistor del Buzzer

// --- AJUSTES EDITABLES ---
const int umbralLuz = 1000;
const int distanciaMinima = 5;
const int distanciaMaxima = 15;
const unsigned long tiempoEsperaLeds = 3000;
const unsigned long tiempoEsperaEntrada = 3000;

// --- MÚSICA: AXEL F ---
int melodiaFiesta[] = {466, 0, 523, 466, 466, 587, 466, 415, 466, 0, 698, 466, 466, 784, 698, 523, 466, 698, 932, 466, 415, 415, 349, 523, 466};
int duracionNotas[] = {150, 50, 150, 150, 50, 150, 150, 150, 150, 50, 150, 150, 50, 150, 150, 150, 150, 150, 150, 50, 150, 50, 150, 150, 400};
int totalNotas = 25;
int notaActual = 0;
unsigned long tiempoUltimaNota = 0;

unsigned long ultimaVezDetectadoPasillo = 0;
unsigned long ultimaVezDetectadoEntrada = 0;
bool fiestaActiva = false;

uint8_t slaveAddress[] = {0x08, 0xD1, 0xF9, 0xD2, 0x22, 0xF4};

typedef struct struct_message
{
    bool presenciaPasillo;
    bool presenciaEntrada;
    bool fiestaActiva;
} struct_message;

struct_message datosParaEnviar;
esp_now_peer_info_t peerInfo;

void setup()
{
    Serial.begin(115200);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(IR_PIN, INPUT);
    pinMode(BOTON_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);

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

    // 1. LÓGICA DE SENSORES (Pasillo y Entrada)
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
        datosParaEnviar.presenciaPasillo = (tiempoActual - ultimaVezDetectadoPasillo < tiempoEsperaLeds);
    }

    if (digitalRead(IR_PIN) == LOW)
    {
        ultimaVezDetectadoEntrada = tiempoActual;
        datosParaEnviar.presenciaEntrada = true;
    }
    else
    {
        datosParaEnviar.presenciaEntrada = (tiempoActual - ultimaVezDetectadoEntrada < tiempoEsperaEntrada);
    }

    // 2. LÓGICA DE FIESTA
    int valorLuz = analogRead(LDR_PIN);
    if (valorLuz > umbralLuz)
    {
        fiestaActiva = false;
    }
    else if (digitalRead(BOTON_PIN) == LOW)
    {
        fiestaActiva = true;
    }
    datosParaEnviar.fiestaActiva = fiestaActiva;

    // 3. TOCAR MÚSICA (Si la fiesta está activa)
    if (fiestaActiva)
    {
        if (tiempoActual - tiempoUltimaNota >= duracionNotas[notaActual])
        {
            if (melodiaFiesta[notaActual] > 0)
            {
                tone(BUZZER_PIN, melodiaFiesta[notaActual], duracionNotas[notaActual] - 20);
            }
            else
            {
                noTone(BUZZER_PIN);
            }
            notaActual++;
            if (notaActual >= totalNotas)
                notaActual = 0;
            tiempoUltimaNota = tiempoActual;
        }
    }
    else
    {
        noTone(BUZZER_PIN);
        notaActual = 0;
    }

    esp_now_send(slaveAddress, (uint8_t *)&datosParaEnviar, sizeof(datosParaEnviar));
    delay(20);
}