#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <TinyGPSPlus.h> // <--- LIBRERÍA GPS

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
#define PIN_AGUA 35

// --- PINES TOUCH DEL ELEVADOR ---
#define PIN_TOUCH1 36
#define PIN_TOUCH2 39

// --- PINES DEL ESTACIONAMIENTO VIP ---
#define PIN_HALL 27
#define RXD2 16
#define TXD2 17
#define RX_GSM 32
#define TX_GSM 33

// --- AJUSTES GENERALES (Pasillo, Entrada, Fiesta) ---
const int umbralLuz = 1000;
const int distanciaMinima = 5;
const int distanciaMaxima = 15;
const unsigned long tiempoEsperaLeds = 3000;
const unsigned long tiempoEsperaEntrada = 3000;

unsigned long ultimaVezDetectadoPasillo = 0;
unsigned long ultimaVezDetectadoEntrada = 0;
bool fiestaActiva = false;

// --- FILTRO ANTIFANTASMAS (ULTRASONIDO) ---
int lecturasPositivas = 0;
const int lecturasRequeridas = 3;

// --- AJUSTES DE CLIMA (AHT20) ---
Adafruit_AHTX0 aht;
float tempUmbral = 30.0;
unsigned long ultimaLecturaClima = 0;

// --- AJUSTES DE SISMO (MPU-6050) ---
const int MPU = 0x68;
int16_t AcX, AcY, AcZ;
long baseAcX = 0, baseAcY = 0, baseAcZ = 0;
int umbralSismo = 8000;
unsigned long ultimoMilisSensor = 0;
bool sismoActivo = false;
int repeticionesSismo = 0;

// --- AJUSTES DE LLUVIA ---
int umbralLluvia = 500;
unsigned long ultimoMilisAgua = 0;
bool lluviaActiva = false;

// --- VARIABLES DEL ESTACIONAMIENTO ---
String numeroDestino = "+524747375924"; // <--- ¡PON TU NÚMERO AQUÍ!
TinyGPSPlus gps;
bool cocheEstacionado = false;
bool mensajeEnviado = false;
bool gsmListo = false;
bool gpsListo = false;
bool sistemaArmado = false;
unsigned long ultimoMilisCheckGSM = 0;

// --- REPRODUCTORES DE AUDIO (Axel F, Sismo, Lluvia) ---
int melodiaFiesta[] = {466, 0, 523, 466, 466, 587, 466, 415, 466, 0, 698, 466, 466, 784, 698, 523, 466, 698, 932, 466, 415, 415, 349, 523, 466};
int duracionNotas[] = {150, 50, 150, 150, 50, 150, 150, 150, 150, 50, 150, 150, 50, 150, 150, 150, 150, 150, 150, 50, 150, 50, 150, 150, 400};
int totalNotas = 25;
int notaActual = 0;
unsigned long tiempoUltimaNota = 0;

int melodiaSismo[] = {300, 325, 350, 375, 400, 425, 450, 475, 500, 525, 550, 575, 600, 625, 650, 675, 700, 725, 750, 0};
int duracionSismo[] = {30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 400};
int totalNotasSismo = 20;
int notaActualSismo = 0;
unsigned long tiempoUltimaNotaSismo = 0;

int melodiaLluvia[] = {1000, 0, 1200, 0};
int duracionLluvia[] = {50, 50, 50, 1000};
int totalNotasLluvia = 4;
int notaActualLluvia = 0;
unsigned long tiempoUltimaNotaLluvia = 0;

// --- ESP-NOW Y PANTALLA ---
uint8_t slaveAddress[] = {0x08, 0xD1, 0xF9, 0xD2, 0x22, 0xF4};
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

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

    // --- SETUP DE COMUNICACIONES ESTACIONAMIENTO ---
    Serial1.begin(9600, SERIAL_8N1, RX_GSM, TX_GSM);
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

    // --- SETUP DE I2C Y SENSORES ---
    Wire.begin();
    if (!aht.begin())
        Serial.println("Aviso: Sensor AHT no detectado.");

    // MPU-6050
    Wire.beginTransmission(MPU);
    Wire.write(0x6B);
    Wire.write(0);
    Wire.endTransmission(true);
    Serial.println("Calibrando Sismo... ¡NO TOQUES!");
    delay(2000);
    for (int i = 0; i < 100; i++)
    {
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
    baseAcX /= 100;
    baseAcY /= 100;
    baseAcZ /= 100;

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

    // --- PINES ---
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(IR_PIN, INPUT);
    pinMode(BOTON_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(PIN_TOUCH1, INPUT);
    pinMode(PIN_TOUCH2, INPUT);
    pinMode(PIN_HALL, INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
        return;
    memcpy(peerInfo.peer_addr, slaveAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("MAESTRO INICIADO: Módulos activos.");
}

void mandarAlertaLlegada()
{
    Serial.println("\n[!] MANDANDO SMS DEL ESTACIONAMIENTO...");
    String linkMaps = "https://maps.google.com/?q=" + String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6);
    String mensajeCompletito = "El coche ha llegado a la maqueta! Ubicacion: " + linkMaps;

    Serial1.println("AT+CMGF=1");
    delay(1000);
    Serial1.print("AT+CMGS=\"");
    Serial1.print(numeroDestino);
    Serial1.println("\"");
    delay(1000);
    Serial1.print(mensajeCompletito);
    delay(500);
    Serial1.write(26);
    Serial.println("✅ ALERTA ENVIADA EXITOSAMENTE.");
}

void loop()
{
    unsigned long tiempoActual = millis();

    // ==========================================
    // MÓDULO 0: ALIMENTAR AL GPS Y GSM (Asíncrono)
    // ==========================================
    while (Serial2.available() > 0)
    {
        gps.encode(Serial2.read());
    }

    if (!sistemaArmado)
    {
        // Verificar GSM cada 3 segundos sin pausar el edificio
        if (!gsmListo && (tiempoActual - ultimoMilisCheckGSM >= 3000))
        {
            ultimoMilisCheckGSM = tiempoActual;
            Serial1.println("AT+CREG?");
            String respuesta = "";
            while (Serial1.available())
                respuesta += (char)Serial1.read();

            if (respuesta.indexOf("0,1") != -1 || respuesta.indexOf("0,5") != -1)
            {
                gsmListo = true;
                Serial.println("✅ Red Celular GSM Lista.");
            }
        }
        // Si hay GSM, verificar si ya hay GPS
        if (gsmListo && !gpsListo)
        {
            if (gps.location.isValid())
            {
                gpsListo = true;
                sistemaArmado = true;
                Serial.println("✅ Coordenadas GPS Listas. ESTACIONAMIENTO ARMADO Y ACTIVO.");
            }
        }
    }

    // ==========================================
    // MÓDULO 1: ESTACIONAMIENTO (Efecto Hall)
    // ==========================================
    if (sistemaArmado)
    {
        int estadoHall = digitalRead(PIN_HALL);
        if (estadoHall == LOW)
        {
            cocheEstacionado = true;
        }
        else
        {
            cocheEstacionado = false;
            mensajeEnviado = false;
        }

        if (cocheEstacionado && !mensajeEnviado)
        {
            Serial.println("🧲 ¡Coche en el estacionamiento!");
            mandarAlertaLlegada();
            mensajeEnviado = true;
        }
    }

    // ==========================================
    // MÓDULO 2: PASILLO (Ultrasonido Antifantasmas)
    // ==========================================
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    int distancia = (pulseIn(ECHO_PIN, HIGH)) * 0.034 / 2;

    if (distancia == 0 || distancia > 400)
        distancia = 999;

    if (distancia >= distanciaMinima && distancia <= distanciaMaxima)
    {
        lecturasPositivas++;
        if (lecturasPositivas >= lecturasRequeridas)
        {
            ultimaVezDetectadoPasillo = tiempoActual;
            datosParaEnviar.presenciaPasillo = true;
        }
    }
    else
    {
        lecturasPositivas = 0;
        datosParaEnviar.presenciaPasillo = (tiempoActual - ultimaVezDetectadoPasillo < tiempoEsperaLeds);
    }

    // ==========================================
    // MÓDULO 3: ENTRADA (IR) Y FIESTA (LDR/Botón)
    // ==========================================
    if (digitalRead(IR_PIN) == LOW)
    {
        ultimaVezDetectadoEntrada = tiempoActual;
        datosParaEnviar.presenciaEntrada = true;
    }
    else
    {
        datosParaEnviar.presenciaEntrada = (tiempoActual - ultimaVezDetectadoEntrada < tiempoEsperaEntrada);
    }

    int valorLuz = analogRead(LDR_PIN);
    if (valorLuz > umbralLuz)
        fiestaActiva = false;
    else if (digitalRead(BOTON_PIN) == LOW)
        fiestaActiva = true;
    datosParaEnviar.fiestaActiva = fiestaActiva;

    // ==========================================
    // MÓDULO 4: ELEVADOR Y CLIMA
    // ==========================================
    datosParaEnviar.touchPiso1 = digitalRead(PIN_TOUCH1);
    datosParaEnviar.touchPiso2 = digitalRead(PIN_TOUCH2);

    if (tiempoActual - ultimaLecturaClima >= 2000)
    {
        sensors_event_t humedad, temperatura;
        aht.getEvent(&humedad, &temperatura);
        datosParaEnviar.ventiladorActivo = (temperatura.temperature >= tempUmbral);
        ultimaLecturaClima = tiempoActual;
    }

    // ==========================================
    // MÓDULO 5: SISMO Y LLUVIA
    // ==========================================
    if (tiempoActual - ultimoMilisSensor >= 50)
    {
        ultimoMilisSensor = tiempoActual;
        Wire.beginTransmission(MPU);
        Wire.write(0x3B);
        Wire.endTransmission(false);
        Wire.requestFrom(MPU, 6, true);
        AcX = Wire.read() << 8 | Wire.read();
        AcY = Wire.read() << 8 | Wire.read();
        AcZ = Wire.read() << 8 | Wire.read();
        long difX = (long)AcX - baseAcX;
        if (difX < 0)
            difX = -difX;
        long difY = (long)AcY - baseAcY;
        if (difY < 0)
            difY = -difY;
        long difZ = (long)AcZ - baseAcZ;
        if (difZ < 0)
            difZ = -difZ;

        if ((difX > umbralSismo || difY > umbralSismo || difZ > umbralSismo) && !sismoActivo)
        {
            sismoActivo = true;
            notaActualSismo = 0;
            repeticionesSismo = 0;
            tiempoUltimaNotaSismo = tiempoActual;
        }
    }

    if (tiempoActual - ultimoMilisAgua >= 500)
    {
        int lecturaAgua = analogRead(PIN_AGUA);
        ultimoMilisAgua = tiempoActual;
        if (lecturaAgua > umbralLluvia)
        {
            if (!lluviaActiva)
            {
                lluviaActiva = true;
                notaActualLluvia = 0;
                tiempoUltimaNotaLluvia = tiempoActual;
            }
        }
        else
        {
            lluviaActiva = false;
        }
    }

    // ==========================================
    // MÓDULO 6: JERARQUÍA DE AUDIO
    // ==========================================
    static bool buzzerSonando = false; // Candado para no saturar el LEDC

    if (sismoActivo)
    {
        buzzerSonando = true;
        if (tiempoActual - tiempoUltimaNotaSismo >= duracionSismo[notaActualSismo])
        {
            if (melodiaSismo[notaActualSismo] > 0)
                tone(BUZZER_PIN, melodiaSismo[notaActualSismo], duracionSismo[notaActualSismo] - 5);
            else
                noTone(BUZZER_PIN);
            notaActualSismo++;
            if (notaActualSismo >= totalNotasSismo)
            {
                notaActualSismo = 0;
                repeticionesSismo++;
                if (repeticionesSismo >= 4)
                {
                    sismoActivo = false;
                    notaActual = 0;
                    notaActualLluvia = 0;
                }
            }
            tiempoUltimaNotaSismo = tiempoActual;
        }
    }
    else if (lluviaActiva)
    {
        buzzerSonando = true;
        if (tiempoActual - tiempoUltimaNotaLluvia >= duracionLluvia[notaActualLluvia])
        {
            if (melodiaLluvia[notaActualLluvia] > 0)
                tone(BUZZER_PIN, melodiaLluvia[notaActualLluvia], duracionLluvia[notaActualLluvia] - 5);
            else
                noTone(BUZZER_PIN);
            notaActualLluvia++;
            if (notaActualLluvia >= totalNotasLluvia)
                notaActualLluvia = 0;
            tiempoUltimaNotaLluvia = tiempoActual;
        }
    }
    else if (fiestaActiva)
    {
        buzzerSonando = true;
        if (tiempoActual - tiempoUltimaNota >= duracionNotas[notaActual])
        {
            if (melodiaFiesta[notaActual] > 0)
                tone(BUZZER_PIN, melodiaFiesta[notaActual], duracionNotas[notaActual] - 20);
            else
                noTone(BUZZER_PIN);
            notaActual++;
            if (notaActual >= totalNotas)
                notaActual = 0;
            tiempoUltimaNota = tiempoActual;
        }
    }
    else
    {
        // AQUÍ ESTÁ LA MAGIA: Solo mandamos "noTone" si realmente estaba sonando
        if (buzzerSonando)
        {
            noTone(BUZZER_PIN);
            buzzerSonando = false;
        }
        notaActual = 0;
        notaActualSismo = 0;
        notaActualLluvia = 0;
    }

    // ==========================================
    // MÓDULO 7: ENVÍO AL ESCLAVO
    // ==========================================
    esp_now_send(slaveAddress, (uint8_t *)&datosParaEnviar, sizeof(datosParaEnviar));
    delay(20);
}