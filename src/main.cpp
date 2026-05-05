#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>             
#include <Adafruit_AHTX0.h>   

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
#define PIN_AGUA 35    // <--- ¡NUEVO: PIN DEL SENSOR DE LLUVIA!

// --- PINES TOUCH DEL ELEVADOR ---
#define PIN_TOUCH1 36
#define PIN_TOUCH2 39

// --- AJUSTES GENERALES ---
const int umbralLuz = 1000;
const int distanciaMinima = 5;
const int distanciaMaxima = 15;
const unsigned long tiempoEsperaLeds = 3000;
const unsigned long tiempoEsperaEntrada = 3000;

unsigned long ultimaVezDetectadoPasillo = 0;
unsigned long ultimaVezDetectadoEntrada = 0;
bool fiestaActiva = false;

// --- AJUSTES DE CLIMA (AHT20) ---
Adafruit_AHTX0 aht;
float tempUmbral = 30.0; 
unsigned long ultimaLecturaClima = 0; 

// --- AJUSTES DE SISMO (MPU-6050) ---
const int MPU = 0x68; 
int16_t AcX, AcY, AcZ;
long baseAcX = 0, baseAcY = 0, baseAcZ = 0;
int umbralSismo = 3000; 
unsigned long ultimoMilisSensor = 0;   
bool sismoActivo = false;
int repeticionesSismo = 0;

// --- AJUSTES DE LLUVIA ---
int umbralLluvia = 500; 
unsigned long ultimoMilisAgua = 0;
bool lluviaActiva = false;

// --- MÚSICA: AXEL F (FIESTA) ---
int melodiaFiesta[] = {466, 0, 523, 466, 466, 587, 466, 415, 466, 0, 698, 466, 466, 784, 698, 523, 466, 698, 932, 466, 415, 415, 349, 523, 466};
int duracionNotas[] = {150, 50, 150, 150, 50, 150, 150, 150, 150, 50, 150, 150, 50, 150, 150, 150, 150, 150, 150, 50, 150, 50, 150, 150, 400};
int totalNotas = 25;
int notaActual = 0;
unsigned long tiempoUltimaNota = 0;

// --- MÚSICA: ALERTA SÍSMICA ---
int melodiaSismo[] = {300, 325, 350, 375, 400, 425, 450, 475, 500, 525, 550, 575, 600, 625, 650, 675, 700, 725, 750, 0};
int duracionSismo[] = {30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30,  30, 400};
int totalNotasSismo = 20;
int notaActualSismo = 0;
unsigned long tiempoUltimaNotaSismo = 0;

// --- MÚSICA: ALERTA DE LLUVIA ---
int melodiaLluvia[] = {1000, 0, 1200, 0};
int duracionLluvia[] = {50,  50,  50,  1000}; 
int totalNotasLluvia = 4;
int notaActualLluvia = 0;
unsigned long tiempoUltimaNotaLluvia = 0;

// MAC del Esclavo
uint8_t slaveAddress[] = {0x08, 0xD1, 0xF9, 0xD2, 0x22, 0xF4};

// Objeto de la pantalla
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// --- DICCIONARIO ESP-NOW ---
typedef struct struct_message
{
    bool presenciaPasillo;
    bool presenciaEntrada;
    bool fiestaActiva;
    bool touchPiso1; 
    bool touchPiso2; 
    bool ventiladorActivo; 
} struct_message;

struct_message datosParaEnviar;
esp_now_peer_info_t peerInfo;

void setup()
{
    Serial.begin(115200);

    Wire.begin();
    
    if (!aht.begin()) {
        Serial.println("Advertencia: Sensor AHT no detectado.");
    }

    Wire.beginTransmission(MPU);
    Wire.write(0x6B); 
    Wire.write(0);    
    Wire.endTransmission(true);

    Serial.println("Calibrando Sismo... ¡NO TOQUES LA MAQUETA!");
    delay(2000); 

    for (int i = 0; i < 100; i++) {
        Wire.beginTransmission(MPU);
        Wire.write(0x3B); 
        Wire.endTransmission(false);
        Wire.requestFrom(MPU, 6, true);
        
        int16_t tempX = Wire.read() << 8 | Wire.read();
        int16_t tempY = Wire.read() << 8 | Wire.read();
        int16_t tempZ = Wire.read() << 8 | Wire.read();
        
        baseAcX += tempX;
        baseAcY += tempY;
        baseAcZ += tempZ;
        delay(10); 
    }
    baseAcX /= 100; baseAcY /= 100; baseAcZ /= 100;
    Serial.println("Calibración Sísmica Lista.");

    // --- SETUP PANTALLA ---
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

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(IR_PIN, INPUT);
    pinMode(BOTON_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(PIN_TOUCH1, INPUT);
    pinMode(PIN_TOUCH2, INPUT);
    // Pin 35 (Agua) no necesita pinMode al ser analógico puro.

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) return;
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

    if (distancia >= distanciaMinima && distancia <= distanciaMaxima) {
        ultimaVezDetectadoPasillo = tiempoActual;
        datosParaEnviar.presenciaPasillo = true;
    } else {
        datosParaEnviar.presenciaPasillo = (tiempoActual - ultimaVezDetectadoPasillo < tiempoEsperaLeds);
    }

    // 2. ENTRADA
    if (digitalRead(IR_PIN) == LOW) {
        ultimaVezDetectadoEntrada = tiempoActual;
        datosParaEnviar.presenciaEntrada = true;
    } else {
        datosParaEnviar.presenciaEntrada = (tiempoActual - ultimaVezDetectadoEntrada < tiempoEsperaEntrada);
    }

    // 3. FIESTA
    int valorLuz = analogRead(LDR_PIN);
    if (valorLuz > umbralLuz) {
        fiestaActiva = false;
    } else if (digitalRead(BOTON_PIN) == LOW) {
        fiestaActiva = true;
    }
    datosParaEnviar.fiestaActiva = fiestaActiva;

    // 4. ELEVADOR
    datosParaEnviar.touchPiso1 = digitalRead(PIN_TOUCH1);
    datosParaEnviar.touchPiso2 = digitalRead(PIN_TOUCH2);

    // 5. CLIMA
    if (tiempoActual - ultimaLecturaClima >= 2000) {
        sensors_event_t humedad, temperatura;
        aht.getEvent(&humedad, &temperatura);
        
        if (temperatura.temperature >= tempUmbral) {
            datosParaEnviar.ventiladorActivo = true;
        } else {
            datosParaEnviar.ventiladorActivo = false;
        }
        ultimaLecturaClima = tiempoActual;
    }

    // 6. SISMO (Cada 50ms)
    if (tiempoActual - ultimoMilisSensor >= 50) {
        ultimoMilisSensor = tiempoActual;
        Wire.beginTransmission(MPU);
        Wire.write(0x3B);  
        Wire.endTransmission(false);
        Wire.requestFrom(MPU, 6, true); 

        AcX = Wire.read() << 8 | Wire.read();  
        AcY = Wire.read() << 8 | Wire.read();  
        AcZ = Wire.read() << 8 | Wire.read();  

        long difX = (long)AcX - baseAcX; if (difX < 0) difX = -difX; 
        long difY = (long)AcY - baseAcY; if (difY < 0) difY = -difY; 
        long difZ = (long)AcZ - baseAcZ; if (difZ < 0) difZ = -difZ; 

        if ((difX > umbralSismo || difY > umbralSismo || difZ > umbralSismo) && !sismoActivo) {
            sismoActivo = true;
            notaActualSismo = 0;
            repeticionesSismo = 0;
            tiempoUltimaNotaSismo = tiempoActual; 
        }
    }

    // 7. LLUVIA (Cada 500ms)
    if (tiempoActual - ultimoMilisAgua >= 500) {
        int lecturaAgua = analogRead(PIN_AGUA);
        ultimoMilisAgua = tiempoActual;
        
        if (lecturaAgua > umbralLluvia) {
            if (!lluviaActiva) {
                lluviaActiva = true;
                notaActualLluvia = 0;
                tiempoUltimaNotaLluvia = tiempoActual;
            }
        } else {
            lluviaActiva = false;
        }
    }

    // 8. CONTROL MAESTRO DE AUDIO (Jerarquía)
    if (sismoActivo) {
        // PRIORIDAD 1: SISMO
        if (tiempoActual - tiempoUltimaNotaSismo >= duracionSismo[notaActualSismo]) {
            if (melodiaSismo[notaActualSismo] > 0) {
                tone(BUZZER_PIN, melodiaSismo[notaActualSismo], duracionSismo[notaActualSismo] - 5);
            } else {
                noTone(BUZZER_PIN);
            }
            notaActualSismo++;
            if (notaActualSismo >= totalNotasSismo) {
                notaActualSismo = 0; 
                repeticionesSismo++; 
                if (repeticionesSismo >= 4) {
                    sismoActivo = false;
                    notaActual = 0; // Reset fiesta
                    notaActualLluvia = 0; // Reset lluvia
                }
            }
            tiempoUltimaNotaSismo = tiempoActual;
        }
    } 
    else if (lluviaActiva) {
        // PRIORIDAD 2: LLUVIA
        if (tiempoActual - tiempoUltimaNotaLluvia >= duracionLluvia[notaActualLluvia]) {
            if (melodiaLluvia[notaActualLluvia] > 0) {
                tone(BUZZER_PIN, melodiaLluvia[notaActualLluvia], duracionLluvia[notaActualLluvia] - 5);
            } else {
                noTone(BUZZER_PIN);
            }
            notaActualLluvia++;
            if (notaActualLluvia >= totalNotasLluvia) {
                notaActualLluvia = 0; 
            }
            tiempoUltimaNotaLluvia = tiempoActual;
        }
    }
    else if (fiestaActiva) {
        // PRIORIDAD 3: FIESTA (Axel F)
        if (tiempoActual - tiempoUltimaNota >= duracionNotas[notaActual]) {
            if (melodiaFiesta[notaActual] > 0) {
                tone(BUZZER_PIN, melodiaFiesta[notaActual], duracionNotas[notaActual] - 20);
            } else {
                noTone(BUZZER_PIN);
            }
            notaActual++;
            if (notaActual >= totalNotas) notaActual = 0;
            tiempoUltimaNota = tiempoActual;
        }
    } 
    else {
        // SILENCIO TOTAL
        noTone(BUZZER_PIN);
        notaActual = 0;
        notaActualSismo = 0;
        notaActualLluvia = 0;
    }

    // 9. ENVIAR DATOS AL ESCLAVO
    esp_now_send(slaveAddress, (uint8_t *)&datosParaEnviar, sizeof(datosParaEnviar));
    delay(20);
}