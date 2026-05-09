#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <TinyGPSPlus.h>

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
#define PIN_IR_PLUMA 26

// --- PINES TOUCH DEL ELEVADOR ---
#define PIN_TOUCH1 36
#define PIN_TOUCH2 39

// --- PINES DEL ESTACIONAMIENTO VIP ---
#define PIN_HALL 27
#define RXD2 16
#define TXD2 17
#define RX_GSM 32
#define TX_GSM 33

// --- AJUSTES GENERALES ---
const int umbralLuz = 1000;
const int distanciaMinima = 5;
const int distanciaMaxima = 15;
const unsigned long tiempoEsperaLeds = 3000;
const unsigned long tiempoEsperaEntrada = 3000;
unsigned long millisPluma = 0;
const unsigned long TIEMPO_ESPERA_PLUMA = 3000;

unsigned long ultimaVezDetectadoPasillo = 0;
unsigned long ultimaVezDetectadoEntrada = 0;
bool fiestaActiva = false;

// --- FILTRO ANTIFANTASMAS ---
int lecturasPositivas = 0;
const int lecturasRequeridas = 3;

// --- AJUSTES DE CLIMA ---
Adafruit_AHTX0 aht;
float tempUmbral = 30.0;
float tempActual = 0.0;
unsigned long ultimaLecturaClima = 0;

// --- AJUSTES DE SISMO ---
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
String numeroDestino = "+524747375924";
TinyGPSPlus gps;
bool cocheEstacionado = false;
bool mensajeEnviado = false;
bool gsmListo = false;
bool gpsListo = false;
bool sistemaArmado = false;
unsigned long ultimoMilisCheckGSM = 0;

// --- AUDIO ---
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
    bool abrirPluma;
} struct_message;

struct_message datosParaEnviar;
esp_now_peer_info_t peerInfo;

// --- GEMELO DIGITAL DEL ELEVADOR (NUEVO) ---
bool sim_elevadorEnPiso1 = true;
bool sim_elevadorEnViaje = false;
bool sim_esperando = false;
unsigned long sim_tiempoApertura = 0;
unsigned long sim_tiempoViaje = 0;
const unsigned long TIEMPO_ESPERA_ELEVADOR = 3000;
const unsigned long TIEMPO_TRAYECTO = 7000; // <--- Cambia esto a los segundos reales que tarda tu motor en subir

// --- MEMORIA DE LA PANTALLA TFT ---
bool tft_sismo = false;
bool tft_lluvia = false;
bool tft_pasillo = false;
bool tft_entrada = false;
bool tft_fiesta = false;
bool tft_pluma = false;
bool tft_coche = false;
float tft_temp = -100.0;
int tft_estadoSenal = -1;
int tft_elevador = -1;

void actualizarFilaTFT(int y, String nombre, String estado, uint16_t colorEstado)
{
    tft.setTextSize(2);
    tft.fillRect(130, y, 190, 16, ILI9341_BLACK);
    tft.setCursor(10, y);
    tft.setTextColor(ILI9341_WHITE);
    tft.print(nombre);
    tft.setCursor(130, y);
    tft.setTextColor(colorEstado);
    tft.print(estado);
}

void setup()
{
    Serial.begin(115200);

    Serial1.begin(9600, SERIAL_8N1, RX_GSM, TX_GSM);
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

    Wire.begin();
    if (!aht.begin())
        Serial.println("Aviso: Sensor AHT no detectado.");

    Wire.beginTransmission(MPU);
    Wire.write(0x6B);
    Wire.write(0);
    Wire.endTransmission(true);
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

    // --- DISEÑO DE PANTALLA AJUSTADO PARA 10 RENGLONES ---
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(ILI9341_BLACK);

    tft.fillRect(0, 0, 320, 26, ILI9341_NAVY);
    tft.setCursor(20, 5);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.println("PANEL TECNM LAGOS");

    actualizarFilaTFT(34, "Senal:", "BUSCANDO RED", ILI9341_ORANGE);
    actualizarFilaTFT(54, "Sismo:", "SEGURO", ILI9341_GREEN);
    actualizarFilaTFT(74, "Lluvia:", "DESPEJADO", ILI9341_GREEN);
    actualizarFilaTFT(94, "Clima:", "Calculando...", ILI9341_CYAN);
    actualizarFilaTFT(114, "Pasillo:", "VACIO", ILI9341_GREEN);
    actualizarFilaTFT(134, "Puertas:", "CERRADAS", ILI9341_GREEN);
    actualizarFilaTFT(154, "Pluma:", "ABAJO", ILI9341_GREEN);
    actualizarFilaTFT(174, "Parking:", "LIBRE", ILI9341_GREEN);
    actualizarFilaTFT(194, "Fiesta:", "OFF", ILI9341_LIGHTGREY);
    actualizarFilaTFT(214, "Elevador:", "PISO 1 (OFF)", ILI9341_LIGHTGREY);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(IR_PIN, INPUT);
    pinMode(BOTON_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(PIN_TOUCH1, INPUT);
    pinMode(PIN_TOUCH2, INPUT);
    pinMode(PIN_HALL, INPUT_PULLUP);
    pinMode(PIN_IR_PLUMA, INPUT);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
        return;
    memcpy(peerInfo.peer_addr, slaveAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void mandarAlertaLlegada()
{
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
}

void loop()
{
    unsigned long tiempoActual = millis();

    while (Serial2.available() > 0)
        gps.encode(Serial2.read());

    if (!sistemaArmado)
    {
        if (!gsmListo && (tiempoActual - ultimoMilisCheckGSM >= 3000))
        {
            ultimoMilisCheckGSM = tiempoActual;
            Serial1.println("AT+CREG?");
            String respuesta = "";
            while (Serial1.available())
                respuesta += (char)Serial1.read();
            if (respuesta.indexOf("0,1") != -1 || respuesta.indexOf("0,5") != -1)
                gsmListo = true;
        }
        if (gsmListo && !gpsListo)
        {
            if (gps.location.isValid())
            {
                gpsListo = true;
                sistemaArmado = true;
            }
        }
    }

    if (sistemaArmado)
    {
        if (digitalRead(PIN_HALL) == LOW)
            cocheEstacionado = true;
        else
        {
            cocheEstacionado = false;
            mensajeEnviado = false;
        }
        if (cocheEstacionado && !mensajeEnviado)
        {
            mandarAlertaLlegada();
            mensajeEnviado = true;
        }
    }

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

    datosParaEnviar.touchPiso1 = digitalRead(PIN_TOUCH1);
    datosParaEnviar.touchPiso2 = digitalRead(PIN_TOUCH2);

    // ==========================================
    // GEMELO DIGITAL: SIMULACIÓN DE ELEVADOR
    // ==========================================
    if (!sim_elevadorEnViaje && !sim_esperando)
    {
        if (datosParaEnviar.touchPiso1 || datosParaEnviar.touchPiso2)
        {
            sim_esperando = true;
            sim_tiempoApertura = tiempoActual;
        }
    }
    if (sim_esperando && (tiempoActual - sim_tiempoApertura >= TIEMPO_ESPERA_ELEVADOR))
    {
        sim_esperando = false;
        sim_elevadorEnViaje = true;
        sim_tiempoViaje = tiempoActual;
    }
    if (sim_elevadorEnViaje && (tiempoActual - sim_tiempoViaje >= TIEMPO_TRAYECTO))
    {
        sim_elevadorEnViaje = false;
        sim_elevadorEnPiso1 = !sim_elevadorEnPiso1;
    }

    if (tiempoActual - ultimaLecturaClima >= 2000)
    {
        sensors_event_t humedad, temperaturaAHT;
        aht.getEvent(&humedad, &temperaturaAHT);
        tempActual = temperaturaAHT.temperature;
        datosParaEnviar.ventiladorActivo = (tempActual >= tempUmbral);
        ultimaLecturaClima = tiempoActual;
    }

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
        long difX = abs((long)AcX - baseAcX);
        long difY = abs((long)AcY - baseAcY);
        long difZ = abs((long)AcZ - baseAcZ);
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

    static bool buzzerSonando = false;
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
        if (buzzerSonando)
        {
            noTone(BUZZER_PIN);
            buzzerSonando = false;
        }
        notaActual = 0;
        notaActualSismo = 0;
        notaActualLluvia = 0;
    }

    if (digitalRead(PIN_IR_PLUMA) == LOW)
    {
        datosParaEnviar.abrirPluma = true;
        millisPluma = tiempoActual;
    }
    else
    {
        if (datosParaEnviar.abrirPluma && (tiempoActual - millisPluma >= TIEMPO_ESPERA_PLUMA))
            datosParaEnviar.abrirPluma = false;
    }

    // ==========================================
    // ACTUALIZACIÓN DE PANTALLA TFT
    // ==========================================
    int estadoActualSenal = 0;
    if (sistemaArmado)
        estadoActualSenal = 2;
    else if (gsmListo)
        estadoActualSenal = 1;

    if (estadoActualSenal != tft_estadoSenal)
    {
        tft_estadoSenal = estadoActualSenal;
        if (estadoActualSenal == 0)
            actualizarFilaTFT(34, "Senal:", "BUSCANDO RED", ILI9341_ORANGE);
        else if (estadoActualSenal == 1)
            actualizarFilaTFT(34, "Senal:", "BUSCANDO GPS", ILI9341_YELLOW);
        else if (estadoActualSenal == 2)
            actualizarFilaTFT(34, "Senal:", "EN LINEA", ILI9341_GREEN);
    }

    if (sismoActivo != tft_sismo)
    {
        tft_sismo = sismoActivo;
        if (sismoActivo)
            actualizarFilaTFT(54, "Sismo:", "PELIGRO!!", ILI9341_RED);
        else
            actualizarFilaTFT(54, "Sismo:", "SEGURO", ILI9341_GREEN);
    }
    if (lluviaActiva != tft_lluvia)
    {
        tft_lluvia = lluviaActiva;
        if (lluviaActiva)
            actualizarFilaTFT(74, "Lluvia:", "LLOVIENDO", ILI9341_BLUE);
        else
            actualizarFilaTFT(74, "Lluvia:", "DESPEJADO", ILI9341_GREEN);
    }

    if (abs(tempActual - tft_temp) >= 0.5 && tempActual > 0)
    {
        tft_temp = tempActual;
        String textoTemp = String(tft_temp, 1) + "C";
        if (datosParaEnviar.ventiladorActivo)
            actualizarFilaTFT(94, "Clima:", textoTemp + " (V:ON)", ILI9341_ORANGE);
        else
            actualizarFilaTFT(94, "Clima:", textoTemp + " (V:OFF)", ILI9341_CYAN);
    }

    if (datosParaEnviar.presenciaPasillo != tft_pasillo)
    {
        tft_pasillo = datosParaEnviar.presenciaPasillo;
        if (tft_pasillo)
            actualizarFilaTFT(114, "Pasillo:", "DETECTADO", ILI9341_RED);
        else
            actualizarFilaTFT(114, "Pasillo:", "VACIO", ILI9341_GREEN);
    }
    if (datosParaEnviar.presenciaEntrada != tft_entrada)
    {
        tft_entrada = datosParaEnviar.presenciaEntrada;
        if (tft_entrada)
            actualizarFilaTFT(134, "Puertas:", "ABIERTAS", ILI9341_YELLOW);
        else
            actualizarFilaTFT(134, "Puertas:", "CERRADAS", ILI9341_GREEN);
    }
    if (datosParaEnviar.abrirPluma != tft_pluma)
    {
        tft_pluma = datosParaEnviar.abrirPluma;
        if (tft_pluma)
            actualizarFilaTFT(154, "Pluma:", "ARRIBA", ILI9341_YELLOW);
        else
            actualizarFilaTFT(154, "Pluma:", "ABAJO", ILI9341_GREEN);
    }
    if (cocheEstacionado != tft_coche)
    {
        tft_coche = cocheEstacionado;
        if (tft_coche)
            actualizarFilaTFT(174, "Parking:", "OCUPADO", ILI9341_RED);
        else
            actualizarFilaTFT(174, "Parking:", "LIBRE", ILI9341_GREEN);
    }
    if (fiestaActiva != tft_fiesta)
    {
        tft_fiesta = fiestaActiva;
        if (tft_fiesta)
            actualizarFilaTFT(194, "Fiesta:", "ON", ILI9341_MAGENTA);
        else
            actualizarFilaTFT(194, "Fiesta:", "OFF", ILI9341_LIGHTGREY);
    }

    // DIBUJAR ELEVADOR
    int estadoElevador = 0; // 0=Piso1, 1=Viaje, 2=Piso2
    if (sim_elevadorEnViaje)
        estadoElevador = 1;
    else if (!sim_elevadorEnPiso1)
        estadoElevador = 2;

    if (estadoElevador != tft_elevador)
    {
        tft_elevador = estadoElevador;
        if (estadoElevador == 0)
            actualizarFilaTFT(214, "Elevador:", "PISO 1 (OFF)", ILI9341_LIGHTGREY);
        else if (estadoElevador == 1)
            actualizarFilaTFT(214, "Elevador:", "MOVIENDO...", ILI9341_YELLOW);
        else if (estadoElevador == 2)
            actualizarFilaTFT(214, "Elevador:", "PISO 2 (ON)", ILI9341_GREEN);
    }

    esp_now_send(slaveAddress, (uint8_t *)&datosParaEnviar, sizeof(datosParaEnviar));
    delay(20);
}