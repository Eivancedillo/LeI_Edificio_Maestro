#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>           // ¡Nuevo: Para el bus I2C!
#include <Adafruit_AHTX0.h> // ¡Nuevo: Librería del Clima!

// --- PINES DE LA PANTALLA TFT ---
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 4

// --- PINES DEL MAESTRO ---
#define TRIG_PIN 5
#define ECHO_PIN 12
#define IR_PIN 19
#define LDR_PIN 34
#define BOTON_PIN 13
#define BUZZER_PIN 25

// --- PINES TOUCH DEL ELEVADOR ---
#define PIN_TOUCH1 36
#define PIN_TOUCH2 39

// --- AJUSTES EDITABLES ---
const int umbralLuz = 1000;
const int distanciaMinima = 5;
const int distanciaMaxima = 15;
const unsigned long tiempoEsperaLeds = 3000;
const unsigned long tiempoEsperaEntrada = 3000;

// --- AJUSTES DE CLIMA ---
Adafruit_AHTX0 aht;
float tempUmbral = 30.0;              // A esta temperatura manda encender
unsigned long ultimaLecturaClima = 0; // Reloj para no saturar el sensor

// --- MÚSICA: AXEL F ---
int melodiaFiesta[] = {466, 0, 523, 466, 466, 587, 466, 415, 466, 0, 698, 466, 466, 784, 698, 523, 466, 698, 932, 466, 415, 415, 349, 523, 466};
int duracionNotas[] = {150, 50, 150, 150, 50, 150, 150, 150, 150, 50, 150, 150, 50, 150, 150, 150, 150, 150, 150, 50, 150, 50, 150, 150, 400};
int totalNotas = 25;
int notaActual = 0;
unsigned long tiempoUltimaNota = 0;

unsigned long ultimaVezDetectadoPasillo = 0;
unsigned long ultimaVezDetectadoEntrada = 0;
bool fiestaActiva = false;

// MAC del Esclavo
uint8_t slaveAddress[] = {0x08, 0xD1, 0xF9, 0xD2, 0x22, 0xF4};

// Objeto de la pantalla
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// --- DICCIONARIO ACTUALIZADO ---
typedef struct struct_message
{
    bool presenciaPasillo;
    bool presenciaEntrada;
    bool fiestaActiva;
    bool touchPiso1;
    bool touchPiso2;
    bool ventiladorActivo; // ¡Nuevo dato para el clima!
} struct_message;

struct_message datosParaEnviar;
esp_now_peer_info_t peerInfo;

void setup()
{
    Serial.begin(115200);

    // Iniciar sensor de Clima (Sin while(1) para que no congele el edificio si falla)
    if (!aht.begin())
    {
        Serial.println("Advertencia: Sensor AHT no detectado.");
    }

    // --- SETUP DE LA PANTALLA ---
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(ILI9341_BLACK);

    tft.setCursor(10, 30);
    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(3);
    tft.println("Edificio inteligente");

    tft.setCursor(10, 65);
    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(1);
    tft.println("Lenguajes e Interdaces");

    tft.setCursor(10, 100);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.println("Sistema en Linea");
    // ----------------------------

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(IR_PIN, INPUT);
    pinMode(BOTON_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);

    // Pines de los Touch
    pinMode(PIN_TOUCH1, INPUT);
    pinMode(PIN_TOUCH2, INPUT);

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

    // 1. PASILLO
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

    // 2. ENTRADA
    if (digitalRead(IR_PIN) == LOW)
    {
        ultimaVezDetectadoEntrada = tiempoActual;
        datosParaEnviar.presenciaEntrada = true;
    }
    else
    {
        datosParaEnviar.presenciaEntrada = (tiempoActual - ultimaVezDetectadoEntrada < tiempoEsperaEntrada);
    }

    // 3. FIESTA
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

    // 4. MÚSICA
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

    // 5. ELEVADOR
    datosParaEnviar.touchPiso1 = digitalRead(PIN_TOUCH1);
    datosParaEnviar.touchPiso2 = digitalRead(PIN_TOUCH2);

    // 6. LECTURA DE CLIMA (Cada 2 segundos)
    if (tiempoActual - ultimaLecturaClima >= 2000)
    {
        sensors_event_t humedad, temperatura;
        aht.getEvent(&humedad, &temperatura);

        if (temperatura.temperature >= tempUmbral)
        {
            datosParaEnviar.ventiladorActivo = true;
        }
        else
        {
            datosParaEnviar.ventiladorActivo = false;
        }
        ultimaLecturaClima = tiempoActual;
    }

    // Enviar paquete completo por ESP-NOW
    esp_now_send(slaveAddress, (uint8_t *)&datosParaEnviar, sizeof(datosParaEnviar));
    delay(20);
}