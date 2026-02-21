// ==============================================================================================
// LIBRERAS
// ==============================================================================================
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include "MAX30105.h"
#include <math.h>
#include <stdlib.h>
#include "bitmaps.h" // Imagenes


// ==============================================================================================
// CONFIGURACIN
// ==============================================================================================

// Wi-Fi  ===============================================================
const char* ssid = "Gavilan GLJ 2.4";     // WiFi 
const char* password = "a1b1c1d1e1";   // Contrasea
WebSocketsServer webSocket(81);
bool wifiConectado = false;

// HARDWARE  =============================================================

// Pantalla Touch
#define TOUCH_CS   15
#define TOUCH_IRQ  27
#define BUZZER_PIN 13

// Sensores MAX30102
#define SDA1 21
#define SCL1 22
#define SDA2 25
#define SCL2 26


// ======================================================================
// VARIABLES COMPARTIDAS
// ======================================================================
#define BUFFER_SIZE 320 

// Datos de seal
volatile float buffer_s1[BUFFER_SIZE];
volatile float buffer_s2[BUFFER_SIZE];
volatile unsigned long buffer_time[BUFFER_SIZE]; 
volatile int writeHead = 0; 
const int FOOT_EVENT_BUFFER_SIZE = BUFFER_SIZE;
volatile unsigned long footEventRelS1[FOOT_EVENT_BUFFER_SIZE];
volatile unsigned long footEventRelS2[FOOT_EVENT_BUFFER_SIZE];
volatile int footEventHeadS1 = 0;
volatile int footEventHeadS2 = 0;
volatile int footEventCountS1 = 0;
volatile int footEventCountS2 = 0;

// Control
volatile bool medicionActiva = false;
volatile int faseMedicion = 0; // 0=Esperando, 1=Calibrando, 2=Calculando, 3=Midiendo
volatile int porcentajeEstabilizacion = 0;
volatile int porcentajeCalculando = 0;
volatile int conteoRRValidos = 0;
volatile int conteoPTTValidos = 0;

volatile float visBaselineS1 = 0.0f;
volatile float visBaselineS2 = 0.0f;
volatile float visHalfRangeS1 = 50.0f;
volatile float visHalfRangeS2 = 50.0f;
volatile bool visEscalaFijada = false;

volatile bool s1ok = false;
volatile bool s2ok = false;
volatile bool s1_conectado = true;
volatile bool s2_conectado = true;
const uint8_t SENSOR_I2C_ADDR = 0x57; // Direccin del MAX30102

// Resultados
volatile int bpmMostrado = 0; 
volatile float pwvMostrado = 0.0; 
volatile bool medicionFinalizada = false;
volatile bool pwvResultadoValido = false;
volatile bool hrResultadoValido = false;

// Paciente (Datos que vienen de UI local o de PC)
volatile int pacienteEdad = 0;
volatile int pacienteAltura = 0; // Se llenar desde la PC en modo estudio clnico

// MODO DE OPERACIN
enum ModoOperacion { MODO_TEST_RAPIDO, MODO_ESTUDIO_CLINICO };
ModoOperacion modoActual = MODO_TEST_RAPIDO;
// MODO DE VISUALIZACIN:  1 = Mtricas , 0 = Curvas 
int modoVisualizacion = 0;
bool graficoPausado = false;
volatile bool resetearBuffersPWVSolicitado = false;

portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// Ventanas de clculo
const int RR_WINDOW_SIZE = 15;
const int PTT_BUFFER_SIZE = 25;

// Snapshot visual cuando se pausa
float pausaBuf1[BUFFER_SIZE];
float pausaBuf2[BUFFER_SIZE];
unsigned long pausaTime[BUFFER_SIZE];
int pausaHead = 0;
unsigned long pausaFootEventRelS1[FOOT_EVENT_BUFFER_SIZE];
unsigned long pausaFootEventRelS2[FOOT_EVENT_BUFFER_SIZE];
int pausaFootEventHeadS1 = 0;
int pausaFootEventHeadS2 = 0;
int pausaFootEventCountS1 = 0;
int pausaFootEventCountS2 = 0;
int pausaFase = 0;
int pausaPorcentajeEstabilizacion = 0;
int pausaPorcentajeCalculando = 0;
int pausaConteoRR = 0;
int pausaConteoPTT = 0;
int pausaBPM = 0;
float pausaPWV = 0.0f;
bool pausaPwvFinalizado = false;
bool pausaPwvValido = false;
bool pausaHrValido = false;
float pausaBaseS1 = 0.0f;
float pausaBaseS2 = 0.0f;
float pausaHalfS1 = 50.0f;
float pausaHalfS2 = 50.0f;
bool pausaEscalaFijada = false;
bool pausaS1ok = false;
bool pausaS2ok = false;
bool pausaS1conectado = true;
bool pausaS2conectado = true;

// GLOBALES PARA TASKSENSORES (suavizado por media movil)
const int MA_SIZE = 4;
float bufMA1_global[MA_SIZE] = {0};
float bufMA2_global[MA_SIZE] = {0};
int idxMA_global = 0;

// Variables para HPF anti-deriva
float lastVal1_centered = 0;
float lastVal2_centered = 0;

// ======================================================================
// OBJETOS
// ======================================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite graphSprite = TFT_eSprite(&tft);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

MAX30105 sensorProx;
MAX30105 sensorDist;

// Pantallas UI
enum EstadoPantalla { MENU, PANTALLA_EDAD, PANTALLA_ALTURA, PANTALLA_MEDICION_RAPIDA, PANTALLA_PC_ESPERA, PANTALLA_ERROR_WIFI };
EstadoPantalla pantallaActual = MENU;
int faseAnterior = -1;

// UI Variables
String edadInput = ""; 
String alturaInput = "";
int btnW = 300; int btnH = 70;
int btnX = (480 - btnW) / 2;
int btnY1 = 85; int btnY2 = 185; 

// Grfico
#define GRAPH_W 458  // 460 - 2 pxeles de bordes
#define GRAPH_H 178  // 180 - 2 pxeles de bordes
#define GRAPH_X 11   // 10 (del marco) + 1 de margen
#define GRAPH_Y 51   // 50 (del marco) + 1 de margen
unsigned long lastDrawTime = 0;
const unsigned long DRAW_INTERVAL = 20; 

// Encabezado de pantalla de medicin
const int HEADER_ICON_Y = 5;
const int BTN_METRICAS_X = 380;
const int BTN_CURVAS_X = 425;
const int BTN_ICON_SIZE = 30;
const int BTN_PAUSA_X = 335;
const int BTN_PAUSA_Y = 5;
const int BTN_PAUSA_W = 30;
const int BTN_PAUSA_H = 30;


// ======================================================================
// WEBSOCKET EVENTS (RECEPCIN DE DATOS DESDE PC)
// ======================================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if(type == WStype_TEXT) {
        // Parseo MANUAL simple para evitar libreras pesadas (JSON)
        // Esperamos formato: {"h":175,"a":30}
        String texto = String((char*)payload);
        
        int idxH = texto.indexOf("\"h\":");
        int idxA = texto.indexOf("\"a\":");
        
        if (idxH != -1) {
            // Extraer altura
            int endH = texto.indexOf(",", idxH);
            if (endH == -1) endH = texto.indexOf("}", idxH);
            String valH = texto.substring(idxH + 4, endH);
            
            portENTER_CRITICAL(&bufferMux);
            pacienteAltura = valH.toInt();
            portEXIT_CRITICAL(&bufferMux);
        }
        
        if (idxA != -1) {
            // Extraer edad
            int endA = texto.indexOf(",", idxA);
            if (endA == -1) endA = texto.indexOf("}", idxA);
            String valA = texto.substring(idxA + 4, endA);
            
            portENTER_CRITICAL(&bufferMux);
            pacienteEdad = valA.toInt();
            portEXIT_CRITICAL(&bufferMux);
        }
    }
}

// ======================================================================
// CORE 0: MOTOR MATEMTICO + ENVO WEBSOCKET
// ======================================================================
void enviarPaqueteEstudio(bool c1, bool c2, bool s1, bool s2, float p, float d, int hr, float pwv) {
  if (modoActual != MODO_ESTUDIO_CLINICO) return;

  char hrField[16];
  char pwvField[24];
  const char* hrJson = "null";
  const char* pwvJson = "null";

  if (hr > 0) {
    snprintf(hrField, sizeof(hrField), "%d", hr);
    hrJson = hrField;
  }

  if (pwv > 0.0f) {
    snprintf(pwvField, sizeof(pwvField), "%.2f", pwv);
    pwvJson = pwvField;
  }

  char json[192];
  snprintf(
    json,
    sizeof(json),
    "{\"c1\":%s,\"c2\":%s,\"s1\":%s,\"s2\":%s,\"p\":%.2f,\"d\":%.2f,\"hr\":%s,\"pwv\":%s}",
    c1 ? "true" : "false",
    c2 ? "true" : "false",
    s1 ? "true" : "false",
    s2 ? "true" : "false",
    p,
    d,
    hrJson,
    pwvJson
  );

  webSocket.broadcastTXT(json);
}

bool verificarConexionI2C(TwoWire &wirePort) {
    wirePort.beginTransmission(SENSOR_I2C_ADDR);
    return (wirePort.endTransmission() == 0);
}

int compararFloatAsc(const void* a, const void* b) {
  float fa = *(const float*)a;
  float fb = *(const float*)b;
  if (fa < fb) return -1;
  if (fa > fb) return 1;
  return 0;
}

void calcularEscalaRobusta(const float* data, int n, float& baseline, float& halfRange) {
  baseline = 0.0f;
  halfRange = 50.0f;
  if (n < 20) return;

  static float sorted[1024];
  if (n > 1024) n = 1024;
  for (int i = 0; i < n; i++) sorted[i] = data[i];

  qsort(sorted, n, sizeof(float), compararFloatAsc);

  int idx10 = (int)(0.10f * (n - 1));
  int idx90 = (int)(0.90f * (n - 1));
  float p10 = sorted[idx10];
  float p90 = sorted[idx90];

  if ((n % 2) == 1) baseline = sorted[n / 2];
  else baseline = 0.5f * (sorted[(n / 2) - 1] + sorted[n / 2]);

  float robustSpan = p90 - p10;
  if (robustSpan < 20.0f) robustSpan = 20.0f;
  halfRange = 0.60f * robustSpan; // margen visual de ~20% sobre P10-P90
  if (halfRange < 12.0f) halfRange = 12.0f;
}

float calcularMediana(const float* data, int n) {
  if (n <= 0) return 0.0f;

  float temp[32];
  if (n > 32) n = 32;

  for (int i = 0; i < n; i++) {
    temp[i] = data[i];
  }

  for (int i = 1; i < n; i++) {
    float key = temp[i];
    int j = i - 1;
    while (j >= 0 && temp[j] > key) {
      temp[j + 1] = temp[j];
      j--;
    }
    temp[j + 1] = key;
  }

  if ((n % 2) == 1) {
    return temp[n / 2];
  }
  return 0.5f * (temp[(n / 2) - 1] + temp[n / 2]);
}

void resetearSnapshotPWVyPausa() {
  portENTER_CRITICAL(&bufferMux);
  medicionFinalizada = false;
  pwvResultadoValido = false;
  hrResultadoValido = false;
  pwvMostrado = 0.0f;
  conteoPTTValidos = 0;
  footEventHeadS1 = 0;
  footEventHeadS2 = 0;
  footEventCountS1 = 0;
  footEventCountS2 = 0;
  resetearBuffersPWVSolicitado = true;
  portEXIT_CRITICAL(&bufferMux);

  graficoPausado = false;
  pausaFootEventHeadS1 = 0;
  pausaFootEventHeadS2 = 0;
  pausaFootEventCountS1 = 0;
  pausaFootEventCountS2 = 0;
  pausaFase = 0;
  pausaPorcentajeEstabilizacion = 0;
  pausaPorcentajeCalculando = 0;
  pausaConteoRR = 0;
  pausaConteoPTT = 0;
  pausaBPM = 0;
  pausaPWV = 0.0f;
  pausaPwvFinalizado = false;
  pausaPwvValido = false;
  pausaHrValido = false;
}

void capturarSnapshotPausa() {
  portENTER_CRITICAL(&bufferMux);
  pausaFase = faseMedicion;
  pausaPorcentajeEstabilizacion = porcentajeEstabilizacion;
  pausaPorcentajeCalculando = porcentajeCalculando;
  pausaConteoRR = conteoRRValidos;
  pausaConteoPTT = conteoPTTValidos;
  pausaBPM = bpmMostrado;
  pausaPWV = pwvMostrado;
  pausaPwvFinalizado = medicionFinalizada;
  pausaPwvValido = pwvResultadoValido;
  pausaHrValido = hrResultadoValido;
  pausaBaseS1 = visBaselineS1;
  pausaBaseS2 = visBaselineS2;
  pausaHalfS1 = visHalfRangeS1;
  pausaHalfS2 = visHalfRangeS2;
  pausaEscalaFijada = visEscalaFijada;
  pausaS1ok = s1ok;
  pausaS2ok = s2ok;
  pausaS1conectado = s1_conectado;
  pausaS2conectado = s2_conectado;

  if (pausaFase >= 2) {
    memcpy(pausaBuf1, (const void*)buffer_s1, sizeof(buffer_s1));
    memcpy(pausaBuf2, (const void*)buffer_s2, sizeof(buffer_s2));
    memcpy(pausaTime, (const void*)buffer_time, sizeof(buffer_time));
    pausaHead = writeHead;
    memcpy(pausaFootEventRelS1, (const void*)footEventRelS1, sizeof(footEventRelS1));
    memcpy(pausaFootEventRelS2, (const void*)footEventRelS2, sizeof(footEventRelS2));
    pausaFootEventHeadS1 = footEventHeadS1;
    pausaFootEventHeadS2 = footEventHeadS2;
    pausaFootEventCountS1 = footEventCountS1;
    pausaFootEventCountS2 = footEventCountS2;
  } else {
    pausaHead = 0;
    pausaFootEventHeadS1 = 0;
    pausaFootEventHeadS2 = 0;
    pausaFootEventCountS1 = 0;
    pausaFootEventCountS2 = 0;
  }
  portEXIT_CRITICAL(&bufferMux);
}


void TaskSensores(void *pvParameters) {

  // Filtros mas agresivos para reducir drift y priorizar robustez de deteccion
  float s1_lp = 0, s1_dc = 0;
  float s2_lp = 0, s2_dc = 0;
  float s1_hp = 0, s2_hp = 0;
  const float ALPHA_LP = 0.88f;
  const float ALPHA_DC = 0.94f;
  const float ALPHA_HP_S1 = 0.90f;
  const float ALPHA_HP_S2 = 0.90f;
  const long SENSOR_THRESHOLD = 50000;

  // Tiempo
  unsigned long startContactTime = 0;
  unsigned long baseTime = 0;
  const unsigned long TIEMPO_ESTABILIZACION = 10000;

  // Deteccin de pies de onda locales
  const unsigned long REFRACT_MS = 370;
  const float FOOT_DERIV_EPS = 0.25f;
  const int FOOT_RISE_CONFIRM_SAMPLES = 2;
  const float FOOT_RISE_STD_FRACTION = 0.10f;
  const float FOOT_RISE_MIN_ABS = 0.25f;
  const unsigned long RR_MIN_MS = 460;
  const unsigned long RR_MAX_MS = 1700; // permite bradicardia
  // const unsigned long PTT_MIN_MS = 50;
  // const unsigned long PTT_MAX_MS = 290;
  const int THRESH_WINDOW_SAMPLES = 100; // ~2.0 s a 50 SPS

  static float thrWinS1[THRESH_WINDOW_SAMPLES] = {0};
  static float thrWinS2[THRESH_WINDOW_SAMPLES] = {0};
  int thrIdx = 0;
  int thrCount = 0;
  float thrSumS1 = 0.0f, thrSumSqS1 = 0.0f;
  float thrSumS2 = 0.0f, thrSumSqS2 = 0.0f;

  int footPriming = 0;
  float s1_prev1 = 0.0f, s2_prev1 = 0.0f;
  bool trackingValleyS1 = false, trackingValleyS2 = false;
  bool rearmS1 = true, rearmS2 = true;
  float valleyMinS1 = 0.0f, valleyMinS2 = 0.0f;
  unsigned long valleyTimeS1 = 0, valleyTimeS2 = 0;
  int riseCountS1 = 0, riseCountS2 = 0;

  unsigned long lastFootTimeS1 = 0;
  unsigned long lastFootTimeS2 = 0;
  unsigned long lastDistFootForHR = 0;
  unsigned long pendingProxFootTime = 0;
  bool waitingForDistFoot = false;

  float rrWindow[RR_WINDOW_SIZE] = {0};
  int rrIdx = 0;
  int rrCount = 0;

  float pttBuffer[PTT_BUFFER_SIZE] = {0};
  int pttCount = 0;

  static float calibS1[1024] = {0};
  static float calibS2[1024] = {0};
  int calibCount = 0;

  // Hardware Check
  unsigned long lastHardwareCheck = 0;
  const int CHECK_INTERVAL = 500;

  for (;;) {
    if (medicionActiva) {
      if (resetearBuffersPWVSolicitado) {
        resetearBuffersPWVSolicitado = false;
        waitingForDistFoot = false;
        pendingProxFootTime = 0;
        pttCount = 0;
        conteoPTTValidos = 0;
        idxMA_global = 0;
        for (int i = 0; i < MA_SIZE; i++) { bufMA1_global[i] = 0.0f; bufMA2_global[i] = 0.0f; }
        footEventHeadS1 = 0;
        footEventHeadS2 = 0;
        footEventCountS1 = 0;
        footEventCountS2 = 0;
        medicionFinalizada = false;
        pwvResultadoValido = false;
        hrResultadoValido = false;
        pwvMostrado = 0.0f;
        footPriming = 0;
        s1_prev1 = 0.0f; s2_prev1 = 0.0f;
        trackingValleyS1 = false; trackingValleyS2 = false;
        rearmS1 = true; rearmS2 = true;
        valleyMinS1 = 0.0f; valleyMinS2 = 0.0f;
        valleyTimeS1 = 0; valleyTimeS2 = 0;
        riseCountS1 = 0; riseCountS2 = 0;
        thrIdx = 0; thrCount = 0;
        thrSumS1 = 0.0f; thrSumSqS1 = 0.0f;
        thrSumS2 = 0.0f; thrSumSqS2 = 0.0f;
        for (int i = 0; i < PTT_BUFFER_SIZE; i++) pttBuffer[i] = 0.0f;
      }

      if (modoActual == MODO_ESTUDIO_CLINICO) {
        webSocket.loop();
      }

      // --- BLOQUE 1: VERIFICACIN DE HARDWARE ---
      if (millis() - lastHardwareCheck > CHECK_INTERVAL) {
        lastHardwareCheck = millis();

        // ---------------- SENSOR 1 (WIRE / PROXIMAL) ----------------
        bool s1_fisico = verificarConexionI2C(Wire);
        
        if (!s1_conectado && s1_fisico) {
          // Se acaba de reconectar fsicamente. Intentamos revivirlo:
          
          // 1. Reiniciamos el BUS Wire por si qued "tonto"
          Wire.begin(SDA1, SCL1, 100000); 
          delay(10); 

          // 2. Intentamos inicializar la librera
          if (sensorProx.begin(Wire, I2C_SPEED_STANDARD)) {
              sensorProx.softReset(); // Reset de software para limpiar registros basura
              delay(10);
              sensorProx.setup(30, 8, 2, 400, 411, 4096);
              s1_conectado = true; // XITO: Ahora s lo marcamos como conectado
          } else {
              s1_conectado = false; // Fall el handshake lgico, seguimos intentando
          }
        } 
        else if (s1_conectado && !s1_fisico) {
           s1_conectado = false; // Se desconect
        }


        // ---------------- SENSOR 2 (WIRE1 / DISTAL) ----------------
        bool s2_fisico = verificarConexionI2C(Wire1);
        
        if (!s2_conectado && s2_fisico) {
          // 1. Reiniciamos el BUS Wire1
          Wire1.begin(SDA2, SCL2, 100000);
          delay(10);

          // 2. Inicializamos librera
          if (sensorDist.begin(Wire1, I2C_SPEED_STANDARD)) {
              sensorDist.softReset();
              delay(10);
              sensorDist.setup(30, 8, 2, 400, 411, 4096);
              s2_conectado = true;
          } else {
              s2_conectado = false;
          }
        }
        else if (s2_conectado && !s2_fisico) {
           s2_conectado = false;
        }
      }

      // --- BLOQUE 2: LECTURA DE DATOS ---
      if (s1_conectado && s2_conectado) {

        sensorProx.check();
        sensorDist.check();

        if (sensorProx.available() && sensorDist.available()) {
          // Timestamp EXACTO
          unsigned long sampleTimestamp = millis();

          long ir1 = sensorProx.getIR();
          long ir2 = sensorDist.getIR();
          float sig1 = -((float)ir1);
          float sig2 = -((float)ir2);

          // Chequeo sensores colocados (Dedo puesto)
          bool currentS1 = (ir1 > SENSOR_THRESHOLD);
          bool currentS2 = (ir2 > SENSOR_THRESHOLD);
          s1ok = currentS1;
          s2ok = currentS2;

          if (currentS1 && currentS2) {
            // --- PROCESAMIENTO DE SEAL ---
            
            // 1. Low-Pass
            if (s1_lp == 0) s1_lp = sig1; if (s2_lp == 0) s2_lp = sig2;
            s1_lp = (s1_lp * ALPHA_LP) + (sig1 * (1.0 - ALPHA_LP));
            s2_lp = (s2_lp * ALPHA_LP) + (sig2 * (1.0 - ALPHA_LP));

            // 2. DC Removal
            if (s1_dc == 0) s1_dc = sig1; if (s2_dc == 0) s2_dc = sig2;
            s1_dc = (s1_dc * ALPHA_DC) + (s1_lp * (1.0 - ALPHA_DC));
            s2_dc = (s2_dc * ALPHA_DC) + (s2_lp * (1.0 - ALPHA_DC));

            float val1_centered = s1_lp - s1_dc;
            float val2_centered = s2_lp - s2_dc;

            // 3. High-Pass Filter
            s1_hp = ALPHA_HP_S1 * (s1_hp + val1_centered - lastVal1_centered);
            s2_hp = ALPHA_HP_S2 * (s2_hp + val2_centered - lastVal2_centered);
            lastVal1_centered = val1_centered;
            lastVal2_centered = val2_centered;

            // 4. Media movil larga (6 muestras), igual en ambos canales para no desincronizar
            bufMA1_global[idxMA_global] = s1_hp;
            bufMA2_global[idxMA_global] = s2_hp;
            idxMA_global = (idxMA_global + 1) % MA_SIZE;
            float sum1 = 0.0f, sum2 = 0.0f;
            for (int i = 0; i < MA_SIZE; i++) { sum1 += bufMA1_global[i]; sum2 += bufMA2_global[i]; }
            float valFinal1 = sum1 / MA_SIZE;
            float valFinal2 = sum2 / MA_SIZE;

            if (faseMedicion == 1 && calibCount < 1024) {
              calibS1[calibCount] = valFinal1;
              calibS2[calibCount] = valFinal2;
              calibCount++;
            }

            // 5. Threshold adaptativo para pies de onda: mean - k*std (ventana mvil ~2 s)
            if (thrCount < THRESH_WINDOW_SAMPLES) {
              thrWinS1[thrIdx] = valFinal1;
              thrWinS2[thrIdx] = valFinal2;
              thrSumS1 += valFinal1;
              thrSumSqS1 += valFinal1 * valFinal1;
              thrSumS2 += valFinal2;
              thrSumSqS2 += valFinal2 * valFinal2;
              thrCount++;
            } else {
              float oldS1 = thrWinS1[thrIdx];
              float oldS2 = thrWinS2[thrIdx];
              thrSumS1 -= oldS1;
              thrSumSqS1 -= oldS1 * oldS1;
              thrSumS2 -= oldS2;
              thrSumSqS2 -= oldS2 * oldS2;

              thrWinS1[thrIdx] = valFinal1;
              thrWinS2[thrIdx] = valFinal2;
              thrSumS1 += valFinal1;
              thrSumSqS1 += valFinal1 * valFinal1;
              thrSumS2 += valFinal2;
              thrSumSqS2 += valFinal2 * valFinal2;
            }
            thrIdx = (thrIdx + 1) % THRESH_WINDOW_SAMPLES;

            float thresholdS1 = 0.0f;
            float thresholdS2 = 0.0f;
            float rearmThresholdS1 = 0.0f;
            float rearmThresholdS2 = 0.0f;
            float sigmaS1 = 1.0f;
            float sigmaS2 = 1.0f;
            if (thrCount > 0) {
              float meanS1 = thrSumS1 / (float)thrCount;
              float meanS2 = thrSumS2 / (float)thrCount;
              float varS1 = (thrSumSqS1 / (float)thrCount) - (meanS1 * meanS1);
              float varS2 = (thrSumSqS2 / (float)thrCount) - (meanS2 * meanS2);
              if (varS1 < 0.0f) varS1 = 0.0f;
              if (varS2 < 0.0f) varS2 = 0.0f;
              sigmaS1 = sqrtf(varS1);
              sigmaS2 = sqrtf(varS2);
              thresholdS1 = meanS1 - (0.95f * sigmaS1);
              thresholdS2 = meanS2 - (0.90f * sigmaS2);
              rearmThresholdS1 = meanS1 - (0.15f * sigmaS1);
              rearmThresholdS2 = meanS2 - (0.15f * sigmaS2);
            }

            // 6. Detector de pie robusto:
            //    - Busca el valle mas bajo bajo umbral adaptativo.
            //    - Confirma solo cuando arranca una subida sostenida.
            //    - Exige rearme por amplitud para evitar dobles pies por ciclo.
            bool footS1 = false;
            bool footS2 = false;
            unsigned long footTimeS1 = 0;
            unsigned long footTimeS2 = 0;

            if (footPriming >= 1) {
              float dS1 = valFinal1 - s1_prev1;
              float dS2 = valFinal2 - s2_prev1;
              float riseDeltaS1 = fmaxf(FOOT_RISE_MIN_ABS, FOOT_RISE_STD_FRACTION * sigmaS1);
              float riseDeltaS2 = fmaxf(FOOT_RISE_MIN_ABS, FOOT_RISE_STD_FRACTION * sigmaS2);

              if (!rearmS1 && valFinal1 > rearmThresholdS1) rearmS1 = true;
              if (!rearmS2 && valFinal2 > rearmThresholdS2) rearmS2 = true;

              if (rearmS1 && !trackingValleyS1 && valFinal1 < thresholdS1) {
                trackingValleyS1 = true;
                valleyMinS1 = valFinal1;
                valleyTimeS1 = sampleTimestamp;
                riseCountS1 = 0;
              }
              if (rearmS2 && !trackingValleyS2 && valFinal2 < thresholdS2) {
                trackingValleyS2 = true;
                valleyMinS2 = valFinal2;
                valleyTimeS2 = sampleTimestamp;
                riseCountS2 = 0;
              }

              if (trackingValleyS1) {
                if (valFinal1 < valleyMinS1) {
                  valleyMinS1 = valFinal1;
                  valleyTimeS1 = sampleTimestamp;
                }
                if (dS1 > riseDeltaS1) riseCountS1++;
                else if (dS1 < -FOOT_DERIV_EPS) riseCountS1 = 0;

                if (riseCountS1 >= FOOT_RISE_CONFIRM_SAMPLES && valFinal1 > (valleyMinS1 + riseDeltaS1)) {
                  if ((valleyTimeS1 - lastFootTimeS1) > REFRACT_MS) {
                    footS1 = true;
                    footTimeS1 = valleyTimeS1;
                    lastFootTimeS1 = footTimeS1;
                  }
                  trackingValleyS1 = false;
                  rearmS1 = false;
                  riseCountS1 = 0;
                } else if (valFinal1 > rearmThresholdS1) {
                  trackingValleyS1 = false;
                  riseCountS1 = 0;
                }
              }

              if (trackingValleyS2) {
                if (valFinal2 < valleyMinS2) {
                  valleyMinS2 = valFinal2;
                  valleyTimeS2 = sampleTimestamp;
                }
                if (dS2 > riseDeltaS2) riseCountS2++;
                else if (dS2 < -FOOT_DERIV_EPS) riseCountS2 = 0;

                if (riseCountS2 >= FOOT_RISE_CONFIRM_SAMPLES && valFinal2 > (valleyMinS2 + riseDeltaS2)) {
                  if ((valleyTimeS2 - lastFootTimeS2) > REFRACT_MS) {
                    footS2 = true;
                    footTimeS2 = valleyTimeS2;
                    lastFootTimeS2 = footTimeS2;
                  }
                  trackingValleyS2 = false;
                  rearmS2 = false;
                  riseCountS2 = 0;
                } else if (valFinal2 > rearmThresholdS2) {
                  trackingValleyS2 = false;
                  riseCountS2 = 0;
                }
              }
            }

            if (faseMedicion >= 2) {
              if (footS1 && footTimeS1 >= baseTime) {
                unsigned long footRelS1 = footTimeS1 - baseTime;
                portENTER_CRITICAL(&bufferMux);
                footEventRelS1[footEventHeadS1] = footRelS1;
                footEventHeadS1 = (footEventHeadS1 + 1) % FOOT_EVENT_BUFFER_SIZE;
                if (footEventCountS1 < FOOT_EVENT_BUFFER_SIZE) footEventCountS1++;
                portEXIT_CRITICAL(&bufferMux);
              }
              if (footS2 && footTimeS2 >= baseTime) {
                unsigned long footRelS2 = footTimeS2 - baseTime;
                portENTER_CRITICAL(&bufferMux);
                footEventRelS2[footEventHeadS2] = footRelS2;
                footEventHeadS2 = (footEventHeadS2 + 1) % FOOT_EVENT_BUFFER_SIZE;
                if (footEventCountS2 < FOOT_EVENT_BUFFER_SIZE) footEventCountS2++;
                portEXIT_CRITICAL(&bufferMux);
              }
            }

            s1_prev1 = valFinal1;
            s2_prev1 = valFinal2;
            if (footPriming == 0) footPriming = 1;
            
            // --- ALGORITMOS HR / PWV ---
            if (faseMedicion >= 2) {
              // HR desde pies de onda distales (S2)
              if (footS2) {
                if (lastDistFootForHR > 0) {
                  long delta = footTimeS2 - lastDistFootForHR;
                  if (delta > (long)RR_MIN_MS && delta < (long)RR_MAX_MS) {
                    rrWindow[rrIdx] = (float)delta;
                    rrIdx = (rrIdx + 1) % RR_WINDOW_SIZE;
                    if (rrCount < RR_WINDOW_SIZE) rrCount++;
                    conteoRRValidos = rrCount;

                    if (rrCount >= RR_WINDOW_SIZE) {
                      float medianRR = calcularMediana(rrWindow, RR_WINDOW_SIZE);
                      if (medianRR > 0.0f) {
                        float beatsPerMinute = 60000.0f / medianRR;
                        if (beatsPerMinute > 20.0f && beatsPerMinute < 220.0f) {
                          bpmMostrado = (int)(beatsPerMinute + 0.5f);
                        }
                      }
                    }
                  }
                }
                lastDistFootForHR = footTimeS2;
              }

              // PTT desde pie proximal -> pie distal
              if (!medicionFinalizada && footS1) {
                // Inicia ventana de bsqueda distal para PTT
                pendingProxFootTime = footTimeS1;
                waitingForDistFoot = true;
              }

              // Clculo PTT/PWV: primer pie distal vlido luego del proximal
              if (!medicionFinalizada && waitingForDistFoot && footS2) {
                long transitTime = footTimeS2 - pendingProxFootTime;
                if (transitTime > 0) {
                  if (pttCount < PTT_BUFFER_SIZE) {
                    pttBuffer[pttCount] = (float)transitTime;
                    pttCount++;
                  }
                  conteoPTTValidos = pttCount;

                  if (pttCount >= PTT_BUFFER_SIZE) {
                    float medianPTT = calcularMediana(pttBuffer, PTT_BUFFER_SIZE);
                    if (medianPTT > 0.0f) {
                      hrResultadoValido = (bpmMostrado >= 40 && bpmMostrado <= 150);
                      float alturaCalc = (pacienteAltura > 0) ? (float)pacienteAltura : 170.0f;
                      float distMeters = (alturaCalc * 0.436f) / 100.0f;
                      float pwvCrudo = distMeters / (medianPTT / 1000.0f);
                      float pwvFinal = pwvCrudo + 5.0f;
                      if (pwvFinal >= 4.0f && pwvFinal <= 18.0f) {
                        pwvMostrado = pwvFinal;
                        pwvResultadoValido = true;
                      } else {
                        pwvMostrado = 0.0f;
                        pwvResultadoValido = false;
                      }
                      medicionFinalizada = true;
                    }
                  }
                }
                waitingForDistFoot = false;
              }
            } // Fin Fase 2

            // --- SALIDA DE DATOS ---
            if (modoActual == MODO_TEST_RAPIDO) {
              if (faseMedicion >= 2) {
                unsigned long tiempoRelativo = sampleTimestamp - baseTime;
                portENTER_CRITICAL(&bufferMux);
                buffer_s1[writeHead] = valFinal1;
                buffer_s2[writeHead] = valFinal2;
                buffer_time[writeHead] = tiempoRelativo;
                writeHead++;
                if (writeHead >= BUFFER_SIZE) writeHead = 0;
                portEXIT_CRITICAL(&bufferMux);
              }
            }
            else if (modoActual == MODO_ESTUDIO_CLINICO) {
              enviarPaqueteEstudio(
                s1_conectado,
                s2_conectado,
                currentS1,
                currentS2,
                valFinal1,
                valFinal2,
                bpmMostrado,
                pwvMostrado
              );
            }

            // --- MQUINA DE ESTADOS ---
            if (faseMedicion == 0) {
              faseMedicion = 1;
              startContactTime = millis();
              calibCount = 0;
              portENTER_CRITICAL(&bufferMux);
              visEscalaFijada = false;
              visBaselineS1 = 0.0f; visBaselineS2 = 0.0f;
              visHalfRangeS1 = 50.0f; visHalfRangeS2 = 50.0f;
              portEXIT_CRITICAL(&bufferMux);
            }
            else if (faseMedicion == 1) {
              unsigned long transcurrido = millis() - startContactTime;
              porcentajeEstabilizacion = (transcurrido * 100) / TIEMPO_ESTABILIZACION;
              if (transcurrido >= TIEMPO_ESTABILIZACION) {
                float baseS1 = 0.0f, baseS2 = 0.0f;
                float halfS1 = 50.0f, halfS2 = 50.0f;
                calcularEscalaRobusta(calibS1, calibCount, baseS1, halfS1);
                calcularEscalaRobusta(calibS2, calibCount, baseS2, halfS2);

                portENTER_CRITICAL(&bufferMux);
                visBaselineS1 = baseS1; visBaselineS2 = baseS2;
                visHalfRangeS1 = halfS1; visHalfRangeS2 = halfS2;
                visEscalaFijada = true;
                portEXIT_CRITICAL(&bufferMux);

                faseMedicion = 2; baseTime = millis();
                porcentajeCalculando = 0;
                if (modoActual == MODO_TEST_RAPIDO) {
                  portENTER_CRITICAL(&bufferMux);
                  writeHead = 0;
                  for (int i = 0; i < BUFFER_SIZE; i++) { buffer_s1[i] = 0; buffer_s2[i] = 0; buffer_time[i] = 0; }
                  footEventHeadS1 = 0;
                  footEventHeadS2 = 0;
                  footEventCountS1 = 0;
                  footEventCountS2 = 0;
                  portEXIT_CRITICAL(&bufferMux);
                }
                idxMA_global = 0;
                for (int i = 0; i < MA_SIZE; i++) { bufMA1_global[i] = 0.0f; bufMA2_global[i] = 0.0f; }
                bpmMostrado = 0; pwvMostrado = 0.0;
                medicionFinalizada = false;
                pwvResultadoValido = false;
                hrResultadoValido = false;
                waitingForDistFoot = false;
                pendingProxFootTime = 0;
                lastDistFootForHR = 0;
                lastFootTimeS1 = 0;
                lastFootTimeS2 = 0;
                footPriming = 0;
                s1_prev1 = 0.0f; s2_prev1 = 0.0f;
                trackingValleyS1 = false; trackingValleyS2 = false;
                rearmS1 = true; rearmS2 = true;
                valleyMinS1 = 0.0f; valleyMinS2 = 0.0f;
                valleyTimeS1 = 0; valleyTimeS2 = 0;
                riseCountS1 = 0; riseCountS2 = 0;
                thrIdx = 0; thrCount = 0;
                thrSumS1 = 0.0f; thrSumSqS1 = 0.0f;
                thrSumS2 = 0.0f; thrSumSqS2 = 0.0f;
                rrIdx = 0; rrCount = 0;
                pttCount = 0;
                conteoRRValidos = 0;
                conteoPTTValidos = 0;
                for (int i = 0; i < RR_WINDOW_SIZE; i++) rrWindow[i] = 0.0f;
                for (int i = 0; i < PTT_BUFFER_SIZE; i++) pttBuffer[i] = 0.0f;
              }
            }
            else if (faseMedicion == 2) {
              int rrProgress = (rrCount * 100) / RR_WINDOW_SIZE;
              int pttProgress = (pttCount * 100) / PTT_BUFFER_SIZE;
              if (medicionFinalizada) pttProgress = 100;
              porcentajeCalculando = (rrProgress + pttProgress) / 2;
              if (porcentajeCalculando > 100) porcentajeCalculando = 100;

              if (medicionFinalizada) {
                porcentajeCalculando = 100;
                if (pwvResultadoValido && hrResultadoValido) {
                  faseMedicion = 3;
                }
              }
            }

          } else {
            // --- SIN DEDOS (Connected but no finger) ---
            if (modoActual == MODO_ESTUDIO_CLINICO) {
              enviarPaqueteEstudio(
                s1_conectado,
                s2_conectado,
                currentS1,
                currentS2,
                0.0f,
                0.0f,
                0,
                0.0f
              );
            }
            if (faseMedicion != 0) {
              faseMedicion = 0; porcentajeEstabilizacion = 0;
              porcentajeCalculando = 0;
              s1_lp = 0; s1_dc = 0; s2_lp = 0; s2_dc = 0;
              s1_hp = 0; s2_hp = 0;
              lastVal1_centered = 0; lastVal2_centered = 0;
              bpmMostrado = 0; pwvMostrado = 0.0;
              medicionFinalizada = false;
              pwvResultadoValido = false;
              hrResultadoValido = false;
              waitingForDistFoot = false;
              pendingProxFootTime = 0;
              lastDistFootForHR = 0;
              lastFootTimeS1 = 0;
              lastFootTimeS2 = 0;
              s1_prev1 = 0.0f; s2_prev1 = 0.0f;
              trackingValleyS1 = false; trackingValleyS2 = false;
              rearmS1 = true; rearmS2 = true;
              valleyMinS1 = 0.0f; valleyMinS2 = 0.0f;
              valleyTimeS1 = 0; valleyTimeS2 = 0;
              riseCountS1 = 0; riseCountS2 = 0;
              rrIdx = 0; rrCount = 0;
              pttCount = 0;
              conteoRRValidos = 0;
              conteoPTTValidos = 0;
              footEventHeadS1 = 0;
              footEventHeadS2 = 0;
              footEventCountS1 = 0;
              footEventCountS2 = 0;
              idxMA_global = 0;
              for (int i = 0; i < MA_SIZE; i++) { bufMA1_global[i] = 0.0f; bufMA2_global[i] = 0.0f; }
              for (int i = 0; i < RR_WINDOW_SIZE; i++) rrWindow[i] = 0.0f;
              for (int i = 0; i < PTT_BUFFER_SIZE; i++) pttBuffer[i] = 0.0f;
              footPriming = 0;
              thrIdx = 0;
              thrCount = 0;
              thrSumS1 = 0.0f; thrSumSqS1 = 0.0f;
              thrSumS2 = 0.0f; thrSumSqS2 = 0.0f;
              calibCount = 0;
              portENTER_CRITICAL(&bufferMux);
              visEscalaFijada = false;
              visBaselineS1 = 0.0f; visBaselineS2 = 0.0f;
              visHalfRangeS1 = 50.0f; visHalfRangeS2 = 50.0f;
              portEXIT_CRITICAL(&bufferMux);
              sensorProx.nextSample(); sensorDist.nextSample();
            }
          }
        }
      } else {
        // --- CABLES DESCONECTADOS ---
        // Si hay desconexin fsica, reseteamos la lgica para que al volver empiece de 0
        s1ok = false;
        s2ok = false;
        faseMedicion = 0;
        porcentajeEstabilizacion = 0;
        porcentajeCalculando = 0;
        conteoRRValidos = 0;
        conteoPTTValidos = 0;
        footEventHeadS1 = 0;
        footEventHeadS2 = 0;
        footEventCountS1 = 0;
        footEventCountS2 = 0;
        idxMA_global = 0;
        for (int i = 0; i < MA_SIZE; i++) { bufMA1_global[i] = 0.0f; bufMA2_global[i] = 0.0f; }
        bpmMostrado = 0;
        pwvMostrado = 0.0f;
        medicionFinalizada = false;
        pwvResultadoValido = false;
        hrResultadoValido = false;
        waitingForDistFoot = false;
        pendingProxFootTime = 0;
        lastDistFootForHR = 0;
        lastFootTimeS1 = 0;
        lastFootTimeS2 = 0;
        footPriming = 0;
        s1_prev1 = 0.0f; s2_prev1 = 0.0f;
        trackingValleyS1 = false; trackingValleyS2 = false;
        rearmS1 = true; rearmS2 = true;
        valleyMinS1 = 0.0f; valleyMinS2 = 0.0f;
        valleyTimeS1 = 0; valleyTimeS2 = 0;
        riseCountS1 = 0; riseCountS2 = 0;
        rrIdx = 0; rrCount = 0;
        pttCount = 0;
        thrIdx = 0;
        thrCount = 0;
        thrSumS1 = 0.0f; thrSumSqS1 = 0.0f;
        thrSumS2 = 0.0f; thrSumSqS2 = 0.0f;
        for (int i = 0; i < RR_WINDOW_SIZE; i++) rrWindow[i] = 0.0f;
        for (int i = 0; i < PTT_BUFFER_SIZE; i++) pttBuffer[i] = 0.0f;
        calibCount = 0;
        portENTER_CRITICAL(&bufferMux);
        visEscalaFijada = false;
        visBaselineS1 = 0.0f; visBaselineS2 = 0.0f;
        visHalfRangeS1 = 50.0f; visHalfRangeS2 = 50.0f;
        portEXIT_CRITICAL(&bufferMux);
        if (modoActual == MODO_ESTUDIO_CLINICO) {
          enviarPaqueteEstudio(
            s1_conectado,
            s2_conectado,
            false,
            false,
            0.0f,
            0.0f,
            0,
            0.0f
          );
        }
      }
    } else {
      vTaskDelay(10);
      if (modoActual == MODO_ESTUDIO_CLINICO) webSocket.loop();
    }
    vTaskDelay(1);
  }
}

// ======================================================================
// WIFI
// ======================================================================
void iniciarWiFi() {
  // BUSCANDO WIFI ---------------------------------------------------------------------
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CONECTANDO WIFI...", 230, 120, 4);

  // Barra de carga
  int barW = 240; int barH = 20; int barX = (480 - barW) / 2; int barY = 160;
  tft.drawRoundRect(barX, barY, barW, barH, 5, TFT_BLACK);

  // Inicializar WiFi
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true); 
  delay(1000); 
  WiFi.begin(ssid, password);

  // Bucle de conexin
  int intentos = 0;
  int maxIntentos = 20;
  while (WiFi.status() != WL_CONNECTED && intentos < maxIntentos) {
    delay(500);
    // Animacin de la barra
    int progreso = map(intentos, 0, maxIntentos, 0, barW - 4);
    tft.fillRect(barX + 2, barY + 2, progreso, barH - 4, TFT_GREEN);
    intentos++;
  }

  // WIFI CONECTADO
  if (WiFi.status() == WL_CONNECTED) {
    wifiConectado = true;
    IPAddress ip = WiFi.localIP();
    Serial.println("/nWifi Conectado! IP: " + ip.toString());
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
  } 
  
  // ERROR DE CONEXIN
  else {
    wifiConectado = false;
  }
}


// ==================================================================================================================================================================
// INTERFAZ GRFICA
// ==================================================================================================================================================================
// ------------------------------------------------------------------------------------
// PALETA DE COLORES
// ------------------------------------------------------------------------------------
#define COLOR_FONDO                TFT_WHITE
#define COLOR_BORDE                TFT_BLACK    
#define COLOR_SOMBRA               TFT_DARKGREY 
#define COLOR_TEXTO                TFT_BLACK   

#define COLOR_BOTON                TFT_LIGHTGREY 
#define COLOR_BOTON_ACCION         TFT_RED       
#define COLOR_BOTON_ACCION2        tft.color565(255, 0, 255)  // Rosa
#define COLOR_TEXTO_BOTON_ACCION   TFT_WHITE 
#define COLOR_BOTON_VOLVER         TFT_RED       
#define COLOR_BOTON_SIGUIENTE      TFT_GREEN     
#define COLOR_FLECHA               TFT_WHITE  
#define COLOR_TECLADO              TFT_BLACK  
#define COLOR_TEXTO_TECLADO        TFT_WHITE

#define COLOR_S1                   TFT_RED      
#define COLOR_S2                   tft.color565(255, 0, 255)  // Rosa   


// ------------------------------------------------------------------------------------
// SONIDOS
// ------------------------------------------------------------------------------------
void sonarPitido() {
  digitalWrite(BUZZER_PIN, HIGH); 
  delay(60); 
  digitalWrite(BUZZER_PIN, LOW);
}


void sonarAlerta() {   // Doble pitido
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
}


// ------------------------------------------------------------------------------------
// BOTONES
// ------------------------------------------------------------------------------------
void dibujarBotonVolver() {
  int circX = 50, circY = 275, r = 22;
  tft.fillCircle(circX, circY, r, COLOR_BOTON_VOLVER);
  tft.fillTriangle(circX-11, circY, circX+2, circY-7, circX+2, circY+7, COLOR_FLECHA);
  tft.fillRect(circX+1, circY-2, 9, 5, COLOR_FLECHA); 
}


void dibujarBotonSiguiente(bool activo) {
  int circX = 430, circY = 275, r = 22;
  
  // Decidir el color del boton segn el estado: "true" (verde) o "false" (gris)
  uint16_t colorFondo = activo ? COLOR_BOTON_SIGUIENTE : COLOR_BOTON;
  tft.fillCircle(circX, circY, r, colorFondo);
  tft.fillTriangle(circX+11, circY, circX-2, circY-7, circX-2, circY+7, COLOR_FLECHA);
  tft.fillRect(circX-10, circY-2, 9, 5, COLOR_FLECHA);
}

bool mostrarBotonPausaEnHeader() {
  if (graficoPausado) return true;

  int faseLocal = 0;
  portENTER_CRITICAL(&bufferMux);
  faseLocal = faseMedicion;
  portEXIT_CRITICAL(&bufferMux);

  return (faseLocal >= 3);
}

void dibujarBotonPausa() {
  int cx = BTN_PAUSA_X + (BTN_PAUSA_W / 2);
  int cy = BTN_PAUSA_Y + (BTN_PAUSA_H / 2);
  int r = (BTN_PAUSA_W < BTN_PAUSA_H ? BTN_PAUSA_W : BTN_PAUSA_H) / 2;
  uint16_t colorFondo = graficoPausado ? COLOR_BOTON : COLOR_BOTON_VOLVER; // gris en pausa, rojo en ejecucion
  uint16_t colorIcono = COLOR_TEXTO_BOTON_ACCION;                           // blanco

  // Boton circular manteniendo el tamano/ubicacion del cuadro original 30x30
  tft.fillCircle(cx, cy, r, colorFondo);

  if (graficoPausado) {
    // Icono PLAY
    tft.fillTriangle(cx - 3, cy - 6, cx - 3, cy + 6, cx + 7, cy, colorIcono);
  } else {
    // Icono PAUSA
    tft.fillRect(cx - 6, cy - 6, 4, 12, colorIcono);
    tft.fillRect(cx + 2, cy - 6, 4, 12, colorIcono);
  }
}


// ------------------------------------------------------------------------------------
// ALERTAS
// ------------------------------------------------------------------------------------
void mostrarAlertaValorInvalido(String titulo, String mensaje) {
  int px = 60, py = 40, pw = 340, ph = 210;
  tft.fillRoundRect(px + 8, py + 8, pw, ph, 10, COLOR_SOMBRA); 
  tft.fillRoundRect(px, py, pw, ph, 10, COLOR_FONDO);
  tft.drawRoundRect(px, py, pw, ph, 10, COLOR_BORDE);
  tft.drawRoundRect(px + 1, py + 1, pw - 2, ph - 2, 10, COLOR_BORDE); // Borde grueso
  // Icono advertencia
  int iconoX = px + (pw - 60) / 2; int iconoY = py + 15;
  tft.setSwapBytes(true);
  tft.pushImage(iconoX, iconoY, 60, 60, (const uint16_t*)epd_bitmap_IconoAdvertencia);
  tft.setSwapBytes(false);
  // Ttulo
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString(titulo, 230, py + 95, 4);      
  tft.drawString(titulo, 230 + 1, py + 95, 4);  // Negrita
  // Mensaje del error
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("Por favor ingrese un valor", 230, py + 125, 2); // Rengln 1: Frase fija
  tft.drawString(mensaje, 230, py + 145, 2);                      // Rengln 2: Rango vlido

  // Botn OK
  int okW = 120, okH = 35; int okX = 230 - (okW / 2); int okY = py + 165;
  tft.fillRoundRect(okX, okY, okW, okH, 5, COLOR_BOTON);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("OK", 230, okY + 15, 2);

  // El cdigo se queda atrapado aqu hasta que el usuario toque el botn de OK
  bool esperando = true;
  while (esperando) {
    if (touch.touched()) {
      TS_Point p = touch.getPoint();
      int x = map(p.x, 200, 3700, 0, 480);
      int y = map(p.y, 200, 3800, 0, 320);

      if (x > okX && x < okX + okW && y > okY && y < okY + okH) {
        sonarPitido();
        esperando = false;
        delay(200); 
      }
    }

    yield(); // Evita que el ESP32 crea que se colg
  }
}


bool confirmarSalir() {
  int px = 60, py = 60, pw = 340, ph = 180; 
  tft.fillRoundRect(px + 8, py + 8, pw, ph, 10, COLOR_SOMBRA); 
  tft.fillRoundRect(px, py, pw, ph, 10, COLOR_FONDO); 
  tft.drawRoundRect(px, py, pw, ph, 10, COLOR_BORDE);
  tft.drawRoundRect(px + 1, py + 1, pw - 2, ph - 2, 10, COLOR_BORDE); // Borde grueso
  // Texto
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("Desea realizar", 230, 110, 4);
  tft.drawString("una nueva medicion?", 230, 140, 4);

  // Botones
  int btnW = 120, btnH = 40;
  int cancelX = 85,  btnY = 175;
  int salirX = 255;
  // Botn CANCELAR
  tft.fillRoundRect(cancelX, btnY, btnW, btnH, 8, TFT_LIGHTGREY);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("CANCELAR", cancelX + btnW/2, btnY + btnH/2, 2);
  // Botn SALIR
  tft.fillRoundRect(salirX, btnY, btnW, btnH, 8, COLOR_BOTON_ACCION);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("SALIR", salirX + btnW/2, btnY + btnH/2, 2);

  // El cdigo se queda atrapado aqu hasta que el usuario toque un botn
  delay(300); 
  while (true) {
    if (touch.touched()) {
      TS_Point p = touch.getPoint();
      int x = map(p.x, 200, 3700, 0, 480);
      int y = map(p.y, 200, 3800, 0, 320);

      // Si toca CANCELAR
      if (x > cancelX && x < cancelX + btnW && y > btnY && y < btnY + btnH) {
        sonarPitido();
        return false; // El usuario se queda
      }
      // Si toca SALIR
      if (x > salirX && x < salirX + btnW && y > btnY && y < btnY + btnH) {
        sonarPitido();
        return true; // El usuario sale
      }
    }
    yield();
  }
}


// ------------------------------------------------------------------------------------
// PANTALLAS
// ------------------------------------------------------------------------------------
void dibujarMenuPrincipal() {
  pantallaActual = MENU;
  tft.fillScreen(COLOR_FONDO); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("Seleccione modo de uso", 240, 40, 4);
  
  // Botn TEST RPIDO
  tft.fillRoundRect(btnX, btnY1, btnW, btnH, 35, COLOR_BOTON_ACCION2);
  tft.setTextColor(COLOR_TEXTO_BOTON_ACCION);
  tft.drawString("TEST RAPIDO", 240, btnY1 + 36, 4);
  
  // Botn ESTUDIO CLNICO
  tft.fillRoundRect(btnX, btnY2, btnW, btnH, 35,  COLOR_BOTON_ACCION); 
  tft.setTextColor(COLOR_TEXTO_BOTON_ACCION);
  tft.drawString("ESTUDIO CLINICO", 240, btnY2 + 36, 4);
}



void dibujarPantallaPC() {
  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXTO);

  // Texto
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("Abra StiffioApp", 240, 70, 4);
  // Icono de la Computadora
  tft.setSwapBytes(true);
  tft.pushImage(180, 100, 120, 120, (const uint16_t*)epd_bitmap_IconoCompu);
  tft.setSwapBytes(false);
  // Botn Salir
  tft.fillRoundRect(380, 255, 80, 40, 20, COLOR_BOTON_ACCION);
  tft.setTextDatum(MC_DATUM); 
  tft.setTextColor(COLOR_TEXTO_BOTON_ACCION, COLOR_BOTON_ACCION);
  tft.drawString("SALIR", 420, 275, 2);
}



void dibujarPantallaFalloWiFi() {
  tft.fillScreen(COLOR_FONDO);
  tft.setTextColor(COLOR_TEXTO);

  // Icono advertencia
  int iconoW = 60; int iconoX = (480 - iconoW) / 2; int iconoY = 45; 
  // Sonido de alerta
  sonarAlerta();

  // Mensaje
  tft.setSwapBytes(true);
  tft.pushImage(iconoX, iconoY, 60, 60, (const uint16_t*)epd_bitmap_IconoAdvertencia);
  tft.setSwapBytes(false);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ERROR DE CONEXION", 240, 135, 4);
  tft.setTextSize(1); 
  tft.drawString("Por favor, revise su conexion a la red.", 240, 170, 2); // Texto ms pequeo

  // Botn reintentar
  tft.fillRoundRect(170, 200, 140, 45, 22, COLOR_BOTON); 
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("REINTENTAR", 240, 222, 2);
  // Botn volver
  dibujarBotonVolver(); 
}

void dibujarPantallaErrorMedicion() {
  tft.fillScreen(COLOR_FONDO);
  tft.setTextColor(COLOR_TEXTO);

  // Icono advertencia
  int iconoW = 60; int iconoX = (480 - iconoW) / 2; int iconoY = 45;

  // Mensaje
  tft.setSwapBytes(true);
  tft.pushImage(iconoX, iconoY, 60, 60, (const uint16_t*)epd_bitmap_IconoAdvertencia);
  tft.setSwapBytes(false);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ERROR DE MEDICION", 240, 135, 4);
  tft.setTextSize(1);
  tft.drawString("Por favor, repita la medicion.", 240, 170, 2);

  // Botn reintentar
  tft.fillRoundRect(170, 200, 140, 45, 22, COLOR_BOTON);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("REINTENTAR", 240, 222, 2);
  // Botn volver
  dibujarBotonVolver();
}

bool errorMedicionBloqueanteActivo() {
  bool activo = false;
  portENTER_CRITICAL(&bufferMux);
  activo = (faseMedicion == 2) && medicionFinalizada && (!pwvResultadoValido || !hrResultadoValido);
  portEXIT_CRITICAL(&bufferMux);
  return activo;
}



void actualizarDisplayEdad() {
  tft.fillRoundRect(170, 60, 140, 45, 22,  COLOR_TECLADO); 
  tft.setTextColor(COLOR_TEXTO_TECLADO); 
  tft.drawString(edadInput, 240, 85, 4);
}

void dibujarTecladoEdad() {
  pantallaActual = PANTALLA_EDAD;
  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(MC_DATUM); 
  tft.setTextColor(COLOR_TEXTO); 
  tft.drawString("EDAD", 240, 30, 4);

  // Actualizar valor en pantalla
  actualizarDisplayEdad(); 
  
  // Teclado Numrico
  int tstartX = 180; int tstartY = 140; int tgapX = 55; int tgapY = 45; 
  for (int i = 0; i < 11; i++) {
    int row = i / 3; int col = i % 3;
    if (i == 9) { col = 1; row = 3; } if (i == 10) { col = 2; row = 3; }
    int kX = tstartX + (col * tgapX); int kY = tstartY + (row * tgapY);

    uint16_t colorBtn = (i == 10) ? COLOR_BOTON_ACCION : COLOR_TECLADO;              // Relleno
    uint16_t colorTxt = (i == 10) ? COLOR_TEXTO_BOTON_ACCION : COLOR_TEXTO_TECLADO;  // Texto
    tft.fillRoundRect(kX - 22, kY - 18, 45, 35, 4, colorBtn); 
    tft.setTextColor(colorTxt);

    if (i < 9) tft.drawNumber(i + 1, kX, kY + 2, 4);
    else if (i == 9) tft.drawNumber(0, kX, kY + 2, 4);
    else tft.drawString("C", kX, kY + 2, 4);
  }

  // Botones
  dibujarBotonVolver();
  dibujarBotonSiguiente(edadInput.length() > 0);
}



void actualizarDisplayAltura() {
  tft.fillRoundRect(170, 60, 140, 45, 22,  COLOR_TECLADO); 
  tft.setTextColor(COLOR_TEXTO_TECLADO); 
  tft.drawString(alturaInput, 240, 85, 4);
}

void dibujarTecladoAltura() {
  pantallaActual = PANTALLA_ALTURA;
  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(MC_DATUM); 
  tft.setTextColor(COLOR_TEXTO); 
  tft.drawString("ALTURA (cm)", 240, 30, 4);

  // Actualizar valor en pantalla
  actualizarDisplayAltura();
  
  // Teclado Numrico
  int tstartX = 180; int tstartY = 140; int tgapX = 55; int tgapY = 45; 
  for (int i = 0; i < 11; i++) {
    int row = i / 3; int col = i % 3;
    if (i == 9) { col = 1; row = 3; } if (i == 10) { col = 2; row = 3; }
    int kX = tstartX + (col * tgapX); int kY = tstartY + (row * tgapY);
    uint16_t colorBtn    = (i == 10) ? COLOR_BOTON_ACCION : COLOR_TECLADO;               // Relleno 
    uint16_t colorTxt    = (i == 10) ? COLOR_TEXTO_BOTON_ACCION : COLOR_TEXTO_TECLADO;   // Texto
    tft.fillRoundRect(kX - 22, kY - 18, 45, 35, 4, colorBtn); 
    tft.setTextColor(colorTxt);

    if (i < 9) tft.drawNumber(i + 1, kX, kY, 4);
    else if (i == 9) tft.drawNumber(0, kX, kY, 4);
    else tft.drawString("C", kX, kY, 4);
  }

  // Botones
  dibujarBotonVolver();
  dibujarBotonSiguiente(alturaInput.length() > 0);
}



void dibujarPantallaMedicion() {
  tft.fillScreen(COLOR_FONDO);

  // Barra superior
  tft.pushImage(BTN_METRICAS_X, HEADER_ICON_Y, BTN_ICON_SIZE, BTN_ICON_SIZE, (const uint16_t*)epd_bitmap_IconoMetricas);
  tft.pushImage(BTN_CURVAS_X, HEADER_ICON_Y, BTN_ICON_SIZE, BTN_ICON_SIZE, (const uint16_t*)epd_bitmap_IconoCurvas);

  // Indicador de seleccin
  if (modoVisualizacion == 1) {
      tft.drawRect(BTN_METRICAS_X - 2, HEADER_ICON_Y - 2, BTN_ICON_SIZE + 4, BTN_ICON_SIZE + 4, COLOR_BOTON_ACCION);
      tft.drawRect(BTN_METRICAS_X - 1, HEADER_ICON_Y - 1, BTN_ICON_SIZE + 2, BTN_ICON_SIZE + 2, COLOR_BOTON_ACCION);
  } else {
      tft.drawRect(BTN_CURVAS_X - 2, HEADER_ICON_Y - 2, BTN_ICON_SIZE + 4, BTN_ICON_SIZE + 4, COLOR_BOTON_ACCION);
      tft.drawRect(BTN_CURVAS_X - 1, HEADER_ICON_Y - 1, BTN_ICON_SIZE + 2, BTN_ICON_SIZE + 2, COLOR_BOTON_ACCION);
  }

  // Botn PAUSA/REANUDAR
  if (mostrarBotonPausaEnHeader()) {
    dibujarBotonPausa();
  }

  // Botn Volver
  dibujarBotonVolver();

  // Botn Salir
  tft.fillRoundRect(380, 255, 80, 40, 20, COLOR_BOTON_ACCION);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXTO_BOTON_ACCION, COLOR_BOTON_ACCION);
  tft.drawString("SALIR", 420, 275, 2);
}


void actualizarMedicion() {
  float localBuf1[BUFFER_SIZE];                          // Buffer proximal
  float localBuf2[BUFFER_SIZE];                          // Buffer distal
  unsigned long localTime[BUFFER_SIZE];                  // Timestamp
  static unsigned long localFootEventRelS1[FOOT_EVENT_BUFFER_SIZE];
  static unsigned long localFootEventRelS2[FOOT_EVENT_BUFFER_SIZE];
  int localFase = 0;                                     // Estado del sistema
  static int faseAnterior = -1; 
  static bool pantallaErrorMedicionVisible = false;
  static unsigned long ultimoSonido = 0;                 // Memoria del cronmetro de alarma
  int localHead = 0;                                     // Indica donde empezar a leer
  int localFootHeadS1 = 0;
  int localFootHeadS2 = 0;
  int localFootCountS1 = 0;
  int localFootCountS2 = 0;
  int localPorcentaje = 0;                               // Porcentaje barra
  int localPorcentajeCalculando = 0;                     // Porcentaje barra clculo
  int localConteoRR = 0;
  int localConteoPTT = 0;
  int localBPM = 0;                                      // BPM
  float localPWV = 0.0f;                                 // PWV
  float localBaseS1 = 0.0f;
  float localBaseS2 = 0.0f;
  float localHalfS1 = 50.0f;
  float localHalfS2 = 50.0f;
  bool localEscalaFijada = false;
  bool localPwvFinalizado = false;
  bool localPwvValido = false;
  bool localHrValido = false;
  bool localS1ok = false;
  bool localS2ok = false;
  bool localS1Conectado = true;
  bool localS2Conectado = true;

  if (graficoPausado) {
    localFase = pausaFase;
    localPorcentaje = pausaPorcentajeEstabilizacion;
    localPorcentajeCalculando = pausaPorcentajeCalculando;
    localConteoRR = pausaConteoRR;
    localConteoPTT = pausaConteoPTT;
    localBPM = pausaBPM;
    localPWV = pausaPWV;
    localPwvFinalizado = pausaPwvFinalizado;
    localPwvValido = pausaPwvValido;
    localHrValido = pausaHrValido;
    localBaseS1 = pausaBaseS1;
    localBaseS2 = pausaBaseS2;
    localHalfS1 = pausaHalfS1;
    localHalfS2 = pausaHalfS2;
    localEscalaFijada = pausaEscalaFijada;
    localS1ok = pausaS1ok;
    localS2ok = pausaS2ok;
    localS1Conectado = pausaS1conectado;
    localS2Conectado = pausaS2conectado;

    if (localFase >= 2) {
      memcpy(localBuf1, pausaBuf1, sizeof(pausaBuf1));
      memcpy(localBuf2, pausaBuf2, sizeof(pausaBuf2));
      memcpy(localTime, pausaTime, sizeof(pausaTime));
      localHead = pausaHead;
      memcpy(localFootEventRelS1, pausaFootEventRelS1, sizeof(pausaFootEventRelS1));
      memcpy(localFootEventRelS2, pausaFootEventRelS2, sizeof(pausaFootEventRelS2));
      localFootHeadS1 = pausaFootEventHeadS1;
      localFootHeadS2 = pausaFootEventHeadS2;
      localFootCountS1 = pausaFootEventCountS1;
      localFootCountS2 = pausaFootEventCountS2;
    }
  } else {
    localFase = faseMedicion;
    localPorcentaje = porcentajeEstabilizacion;
    localPorcentajeCalculando = porcentajeCalculando;
    localConteoRR = conteoRRValidos;
    localConteoPTT = conteoPTTValidos;
    localBPM = bpmMostrado;
    localPWV = pwvMostrado;
    localPwvFinalizado = medicionFinalizada;
    localPwvValido = pwvResultadoValido;
    localHrValido = hrResultadoValido;
    localBaseS1 = visBaselineS1;
    localBaseS2 = visBaselineS2;
    localHalfS1 = visHalfRangeS1;
    localHalfS2 = visHalfRangeS2;
    localEscalaFijada = visEscalaFijada;
    localS1ok = s1ok;
    localS2ok = s2ok;
    localS1Conectado = s1_conectado;
    localS2Conectado = s2_conectado;

    // Copiar los buffers para graficar (Proteccin con Mutex)
    portENTER_CRITICAL(&bufferMux);
    if (localFase >= 2) {
      memcpy(localBuf1, (const void*)buffer_s1, sizeof(buffer_s1));
      memcpy(localBuf2, (const void*)buffer_s2, sizeof(buffer_s2));
      memcpy(localTime, (const void*)buffer_time, sizeof(buffer_time));
      localHead = writeHead;
      memcpy(localFootEventRelS1, (const void*)footEventRelS1, sizeof(footEventRelS1));
      memcpy(localFootEventRelS2, (const void*)footEventRelS2, sizeof(footEventRelS2));
      localFootHeadS1 = footEventHeadS1;
      localFootHeadS2 = footEventHeadS2;
      localFootCountS1 = footEventCountS1;
      localFootCountS2 = footEventCountS2;
    }
    portEXIT_CRITICAL(&bufferMux);
  }

  bool localErrorMedicion = (localFase == 2) && localPwvFinalizado && (!localPwvValido || !localHrValido);
  if (localErrorMedicion) {
    if (!pantallaErrorMedicionVisible) {
      dibujarPantallaErrorMedicion();
      pantallaErrorMedicionVisible = true;
    }
    return;
  }

  if (pantallaErrorMedicionVisible) {
    pantallaErrorMedicionVisible = false;
    dibujarPantallaMedicion();
  }

  // Limpiar pantalla si cambia la fase
  if (localFase != faseAnterior) {
      tft.fillRect(0, 0, 380, 50, COLOR_FONDO);   // Borra las leyendas
      tft.fillRect(0, 48, 480, 185, COLOR_FONDO);  // Borra grfico  
      tft.fillRect(80, 260, 280, 40, COLOR_FONDO);  // Borra resultados
      if (mostrarBotonPausaEnHeader()) {
        dibujarBotonPausa();
      }
      faseAnterior = localFase;    
  }

  // ==========================================================================================
  // ESTADO DE ERROR / ESPERA (Fase 0)
  // Aqu es donde distinguimos entre "Desconectado" y "No Colocado"
  // ==========================================================================================
  if (localFase == 0) {
    graphSprite.fillSprite(COLOR_FONDO); 
    graphSprite.setTextDatum(MC_DATUM); 
    int yCentro = GRAPH_H / 2; 
    int yIcono = yCentro - 75; 
    int yMsg1 = yCentro + 10;
    int yMsg2 = yCentro + 50;

    // Dibujar Icono de Advertencia (comn para ambos casos)
    graphSprite.setSwapBytes(true);
    graphSprite.pushImage((GRAPH_W - 60) / 2, yIcono, 60, 60, (const uint16_t*)epd_bitmap_IconoAdvertencia); 
    graphSprite.setSwapBytes(false);

    // -----------------------------------------------------------------------
    // PRIORIDAD 1: DESCONEXIN FSICA (Cables sueltos)
    // -----------------------------------------------------------------------
    if (!localS1Conectado || !localS2Conectado) {
        
        // Alarma Sonora RPIDA (Beep-Beep constante)
        if (millis() - ultimoSonido > 200) { 
            // Genera un pitido corto manual sin bloquear mucho tiempo
            digitalWrite(BUZZER_PIN, HIGH);
            delay(30); 
            digitalWrite(BUZZER_PIN, LOW);
            ultimoSonido = millis();
        }

        // Sub-caso: AMBOS desconectados
        if (!localS1Conectado && !localS2Conectado) {
            int inicioX = (GRAPH_W - 320) / 2; 
            graphSprite.setTextDatum(ML_DATUM);
            graphSprite.setTextColor(COLOR_S1); 
            graphSprite.drawString("Sensor Proximal (1)", inicioX, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString(" y", inicioX + 230, yMsg1, 4); 
            
            int inicioX2 = (GRAPH_W - 350) / 2;
            graphSprite.setTextColor(COLOR_S2); 
            graphSprite.drawString("Sensor Distal (2)", inicioX2, yMsg2, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString(" desconectados", inicioX2 + 190, yMsg2, 4);
        }
        // Sub-caso: SOLO S1 desconectado
        else if (!localS1Conectado) {
            graphSprite.setTextDatum(MC_DATUM);
            graphSprite.setTextColor(COLOR_S1); 
            graphSprite.drawString("Sensor Proximal (1)", GRAPH_W/2, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString("desconectado", GRAPH_W/2, yMsg2, 4);
        }
        // Sub-caso: SOLO S2 desconectado
        else if (!localS2Conectado) {
            graphSprite.setTextDatum(MC_DATUM);
            graphSprite.setTextColor(COLOR_S2); 
            graphSprite.drawString("Sensor Distal (2)", GRAPH_W/2, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString("desconectado", GRAPH_W/2, yMsg2, 4);
        }
    }

    // -----------------------------------------------------------------------
    // PRIORIDAD 2: SENSORES CONECTADOS PERO SIN DEDO (Usuario)
    // -----------------------------------------------------------------------
    else {
        // Alarma LENTA (Recordatorio cada 4 seg)
        if (millis() - ultimoSonido > 4000) { 
            sonarAlerta(); // Tu funcin de doble pitido
            ultimoSonido = millis();
        }

        // CASO 1: Ambos sensores no colocados
        if (!localS1ok && !localS2ok) {
            int inicioX = (GRAPH_W - 320) / 2; 
            graphSprite.setTextDatum(ML_DATUM); 
            graphSprite.setTextColor(COLOR_S1); 
            graphSprite.drawString("Sensor Proximal (1)", inicioX, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString(" y", inicioX + 230, yMsg1, 4); 
            int inicioX2 = (GRAPH_W - 350) / 2;
            graphSprite.setTextColor(COLOR_S2); 
            graphSprite.drawString("Sensor Distal (2)", inicioX2, yMsg2, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString(" no colocados", inicioX2 + 190, yMsg2, 4);
        }
        // CASO 2: Sensor 1 no colocado
        else if (!localS1ok) {
            graphSprite.setTextDatum(MC_DATUM);
            graphSprite.setTextColor(COLOR_S1); 
            graphSprite.drawString("Sensor Proximal (1)", GRAPH_W/2, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString("no colocado", GRAPH_W/2, yMsg2, 4);
        }
        // CASO 3: Sensor 2 no colocado
        else if (!localS2ok) {
            graphSprite.setTextDatum(MC_DATUM);
            graphSprite.setTextColor(COLOR_S2); 
            graphSprite.drawString("Sensor Distal (2)", GRAPH_W/2, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString("no colocado", GRAPH_W/2, yMsg2, 4);
        }
    }

    graphSprite.pushSprite(11, 51); // Mostrar sprite final
    return;
  }
 
  // CALIBRANDO (10s fijos) ----------------------------------------------------------------------
  if (localFase == 1) {
    graphSprite.fillSprite(COLOR_FONDO); 
    graphSprite.setTextDatum(MC_DATUM); 
    graphSprite.setTextColor(COLOR_TEXTO);
    graphSprite.drawString("CALIBRANDO ...", GRAPH_W/2, GRAPH_H/2 - 20, 4);
    int barW = 200; int barH = 20; int barX = (GRAPH_W - barW)/2;  int barY = GRAPH_H/2 + 10;
    graphSprite.drawRect(barX, barY, barW, barH, TFT_BLACK);   // Barra de progreso
    int fillW = (barW * localPorcentaje) / 100;
    if (fillW > 2) graphSprite.fillRect(barX+1, barY+1, fillW-2, barH-2, TFT_GREEN);
    graphSprite.pushSprite(11, 51); 
    return;
  }

  // CALCULANDO (tiempo variable hasta HR+PWV vlidos) ------------------------------------------
  if (localFase == 2) {
    graphSprite.fillSprite(COLOR_FONDO);
    graphSprite.setTextDatum(MC_DATUM);
    graphSprite.setTextColor(COLOR_TEXTO);
    graphSprite.drawString("CALCULANDO ...", GRAPH_W/2, GRAPH_H/2 - 20, 4);

    int barW = 200; int barH = 20; int barX = (GRAPH_W - barW)/2;  int barY = GRAPH_H/2 + 10;
    graphSprite.drawRect(barX, barY, barW, barH, TFT_BLACK);

    if (localPorcentajeCalculando < 0) localPorcentajeCalculando = 0;
    if (localPorcentajeCalculando > 100) localPorcentajeCalculando = 100;
    int fillW = (barW * localPorcentajeCalculando) / 100;
    if (fillW > 2) graphSprite.fillRect(barX + 1, barY + 1, fillW - 2, barH - 2, TFT_GREEN);

    if (localConteoRR < 0) localConteoRR = 0;
    if (localConteoRR > RR_WINDOW_SIZE) localConteoRR = RR_WINDOW_SIZE;
    if (localConteoPTT < 0) localConteoPTT = 0;
    if (localConteoPTT > PTT_BUFFER_SIZE) localConteoPTT = PTT_BUFFER_SIZE;
    graphSprite.setTextColor(COLOR_TEXTO);
    graphSprite.setTextDatum(MC_DATUM);
    graphSprite.drawString("RR " + String(localConteoRR) + "/" + String(RR_WINDOW_SIZE) + "   PTT " + String(localConteoPTT) + "/" + String(PTT_BUFFER_SIZE), GRAPH_W/2, barY + 34, 2);

    graphSprite.pushSprite(11, 51);
    return;
  }

  // VISUALIZACIN MODO MTRICAS -----------------------------------------------------------------
  if (modoVisualizacion == 1) {
      int centroX = 240;
      tft.setTextDatum(MC_DATUM);

      // PWV
      tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      tft.drawString("PWV", centroX, 80, 4);
      tft.setTextPadding(240);
      if (localPwvFinalizado && (!localPwvValido || !localHrValido)) {
        tft.setTextColor(COLOR_BOTON_ACCION, COLOR_FONDO);
        tft.drawString("REINTENTAR", centroX, 150, 4);
        tft.setTextPadding(0);
      } else if (localPwvValido && localPWV > 0.0f) {
        tft.setTextColor(TFT_GREEN, COLOR_FONDO);
        tft.drawString(String(localPWV, 1), centroX - 30, 150, 7);
        tft.setTextPadding(0);
        tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
        tft.drawString("m/s", centroX + 70, 160, 4); 
      } else {
        tft.setTextColor(TFT_GREEN, COLOR_FONDO);
        tft.drawString("---", centroX, 150, 7);
        tft.setTextPadding(0);
      }

      // HR
      int yHR = 220;
      tft.setSwapBytes(true);
      tft.pushImage(centroX - 72, yHR - 12, 24, 24, (const uint16_t*)epd_bitmap_ImgCorazon); // Imagen corazon
      tft.setSwapBytes(false);
      tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      tft.setTextPadding(180); // Ancho de limpieza seguro
      int xTexto = centroX + 10; 
      if (localPwvFinalizado && !localHrValido) {
         tft.setTextColor(COLOR_BOTON_ACCION, COLOR_FONDO);
         tft.drawString("REINTENTAR", centroX, yHR, 2);
         tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      } else if (localBPM > 0) {
         // Si hay dato
         tft.drawString(String(localBPM) + " BPM", xTexto, yHR, 4);
      } else {
         // Si no hay dato
         tft.drawString("---", centroX, yHR, 4);
      }
      
      tft.setTextPadding(0); // Apagar padding
      
  }

  // VISUALIZACIN MODO CURVAS ---------------------------------------------------------------------
  else {
      graphSprite.fillSprite(COLOR_FONDO);  // Limpiar sprite

      // Recuadro para grfico
      tft.drawRect(10, 50, 460, 180, TFT_BLACK); 
      // Referencias sensores
      tft.setTextDatum(ML_DATUM); 
      tft.setTextColor(COLOR_S1, COLOR_FONDO); tft.drawString("Sensor Proximal (1)  ", 15, 25, 2);  
      tft.setTextColor(COLOR_S2, COLOR_FONDO); tft.drawString("Sensor Distal (2)    ", 165, 25, 2);
      
      // 1. Escalas Y fijas por canal (calculadas en calibracin)
      float baseS1 = localEscalaFijada ? localBaseS1 : 0.0f;
      float baseS2 = localEscalaFijada ? localBaseS2 : 0.0f;
      float halfS1 = localEscalaFijada ? localHalfS1 : 50.0f;
      float halfS2 = localEscalaFijada ? localHalfS2 : 50.0f;
      if (halfS1 < 1.0f) halfS1 = 50.0f;
      if (halfS2 < 1.0f) halfS2 = 50.0f;

      int margenSuperior = 28;
      int margenInferior = GRAPH_H - 38;
      float yCenter = (margenSuperior + margenInferior) * 0.5f;
      float yHalf = (margenInferior - margenSuperior) * 0.5f;

      // 2. Dibujar Grilla y Ejes
      graphSprite.drawFastHLine(0, (int)yCenter, GRAPH_W, TFT_DARKGREY);
      float xStep = (float)GRAPH_W / (float)BUFFER_SIZE; 
      graphSprite.setTextDatum(TC_DATUM); graphSprite.setTextColor(TFT_DARKGREY);

      // 3. Bucle de Dibujado de Lneas
      for (int i = 0; i < BUFFER_SIZE - 1; i++) {
        int idx = (localHead + i) % BUFFER_SIZE; 
        int nextIdx = (localHead + i + 1) % BUFFER_SIZE;
        
        if (localTime[idx] == 0 && localTime[nextIdx] == 0) continue;
        
        int x1 = (int)(i * xStep); 
        int x2 = (int)((i + 1) * xStep);
        
        unsigned long tCurrent = localTime[idx]; unsigned long tNext = localTime[nextIdx];
        if (tNext > tCurrent) {
            unsigned long secCurrent = tCurrent / 1000; unsigned long secNext = tNext / 1000;
            if (secNext > secCurrent) { 
                graphSprite.drawFastVLine(x2, 0, GRAPH_H, 0xE71C); 
                graphSprite.drawString(String(secNext)+"s", x2, GRAPH_H - 20, 2);
            }
        }
        
        float n1A = (localBuf1[idx] - baseS1) / halfS1;
        float n1B = (localBuf1[nextIdx] - baseS1) / halfS1;
        float n2A = (localBuf2[idx] - baseS2) / halfS2;
        float n2B = (localBuf2[nextIdx] - baseS2) / halfS2;

        int y1A = (int)(yCenter - (n1A * yHalf));
        int y1B = (int)(yCenter - (n1B * yHalf));
        int y2A = (int)(yCenter - (n2A * yHalf));
        int y2B = (int)(yCenter - (n2B * yHalf));
        
        graphSprite.drawLine(x1, constrain(y1A,0,GRAPH_H), x2, constrain(y1B,0,GRAPH_H), COLOR_S1);
        graphSprite.drawLine(x1, constrain(y2A,0,GRAPH_H), x2, constrain(y2B,0,GRAPH_H), COLOR_S2); 
      }

      // Visualizacion de pies de onda desactivada en test rapido.
      
      graphSprite.pushSprite(11, 51); 

      // Datos numricos 
      int yInfo = 280; 
      
      // PWV
      tft.setTextDatum(ML_DATUM); 
      tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      tft.drawString("PWV = ", 100, yInfo, 4);
      tft.setTextPadding(160);
      if (localPwvFinalizado && (!localPwvValido || !localHrValido)) tft.drawString("REINTENTAR", 180, yInfo, 4);
      else if (localPwvValido && localPWV > 0.0f) tft.drawString(String(localPWV, 1) + " m/s", 180, yInfo, 4);
      else tft.drawString("--- m/s", 180, yInfo, 4);
      tft.setTextPadding(0);

      // HR
      tft.setSwapBytes(true);
      tft.pushImage(270, yInfo - 12, 24, 24, (const uint16_t*)epd_bitmap_ImgCorazon);
      tft.setSwapBytes(false);
      tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      tft.setTextPadding(80);
      int xHrCurvas = 300;
      if (localPwvFinalizado && !localHrValido) {
        tft.setTextColor(COLOR_BOTON_ACCION, COLOR_FONDO);
        tft.drawString("REINTENTAR", xHrCurvas, yInfo, 2);
        tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      } else if(localBPM > 0) {
        tft.drawString(String(localBPM), xHrCurvas, yInfo, 4);
      } else {
        tft.drawString("---", xHrCurvas, yInfo, 4);
      }
      tft.setTextPadding(0);
    }
}


// ==================================================================================================================================================================
// MAIN SETUP & LOOP
// ==================================================================================================================================================================
void iniciarSensores() {
  // Inicializamos los buses
  Wire.begin(SDA1, SCL1, 100000); 
  Wire1.begin(SDA2, SCL2, 100000);
  Wire.setClock(100000); 
  Wire1.setClock(100000);

  // Intentamos iniciar Sensor 1
  if (sensorProx.begin(Wire, I2C_SPEED_STANDARD)) {
      sensorProx.setup(30, 8, 2, 400, 411, 4096);
      s1_conectado = true;
  } else {
      s1_conectado = false; // Marcamos como desconectado desde el inicio
  }

  // Intentamos iniciar Sensor 2
  if (sensorDist.begin(Wire1, I2C_SPEED_STANDARD)) {
      sensorDist.setup(30, 8, 2, 400, 411, 4096);
      s2_conectado = true;
  } else {
      s2_conectado = false; // Marcamos como desconectado desde el inicio
  }
}


void setup() {
  Serial.begin(115200);

  // Inicializar buzzer
  pinMode(BUZZER_PIN, OUTPUT); 

  // Inicializar pantalla
  tft.init(); 
  tft.setRotation(3); 
  tft.setSwapBytes(true);
  graphSprite.setColorDepth(8); 
  graphSprite.createSprite(GRAPH_W, GRAPH_H);
  touch.begin(); 
  touch.setRotation(1);

  
  // PANTALLA DE INICIO ----------------------------------------------------------------
  tft.fillScreen(COLOR_FONDO);
  int imgW = 190, imgH = 160;
  tft.pushImage((480-imgW)/2, (320-imgH)/2 - 20, imgW, imgH, epd_bitmap_Logo); // Logo
  
  // Sonido de encendido
  digitalWrite(BUZZER_PIN, HIGH); 
  delay(400); 
  digitalWrite(BUZZER_PIN, LOW);
  delay(2000);

  tft.setTextDatum(MC_DATUM); 
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("iniciando sistema...", 240, 230, 2); // Texto
  delay(3000); 
  // -----------------------------------------------------------------------------------

  // Inicializar sensores (no bloquea si no los detecta)
  // TaskSensores se encargar de intentar reconectarlos luego.
  iniciarSensores(); 
  delay(1000);
  xTaskCreatePinnedToCore(TaskSensores,"SensorTask",10000,NULL,1,NULL,0);

  // PANTALLA PRINCIPAL ----------------------------------------------------------------
  dibujarMenuPrincipal();
}



void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    int x = map(p.x, 200, 3700, 0, 480);
    int y = map(p.y, 200, 3800, 0, 320);

    // PANTALLA PRINCIPAL --------------------------------------------------------------
    if (pantallaActual == MENU) {

      // MODO TEST RPIDO
      if (x > btnX && x < btnX + btnW && y > btnY1 && y < btnY1 + btnH) {
         sonarPitido(); 
         modoActual = MODO_TEST_RAPIDO;
         edadInput = ""; 
         alturaInput = "";
         modoVisualizacion = 0; // Reset modo visualizacin a curvas
         resetearSnapshotPWVyPausa();
         dibujarTecladoEdad();
      }
      // MODO ESTUDIO CLNICO
      else if (x > btnX && x < btnX + btnW && y > btnY2 && y < btnY2 + btnH) {
        sonarPitido();
        modoActual = MODO_ESTUDIO_CLINICO;

        if (!wifiConectado) {
            iniciarWiFi(); // Muestra barra de carga
            delay(500);
        }
        
        // Wifi conectado
        if (wifiConectado) {
            pantallaActual = PANTALLA_PC_ESPERA;
            dibujarPantallaPC();
            resetearSnapshotPWVyPausa();
             
            // Reiniciar lgica y buffers para esperar a la PC
            portENTER_CRITICAL(&bufferMux);
            faseMedicion = 0; s1ok=false; s2ok=false;
            pacienteAltura = 0; // Reset altura, la PC debe mandarla
            portEXIT_CRITICAL(&bufferMux);
             
            medicionActiva = true; 
         } 
         
        // Wifi no conectado
        else {
            pantallaActual = PANTALLA_ERROR_WIFI;
            dibujarPantallaFalloWiFi();
         }
      }
    }


    // PANTALLA CONEXIN PC -------------------------------------------------------------
    else if (pantallaActual == PANTALLA_PC_ESPERA) {
      // Boton salir
      if (x > 380 && x < 460 && y > 255 && y < 295) {
        sonarPitido();

        // Confirmar salida
        // SALIR
        if (confirmarSalir()) {  
            medicionActiva = false;  // Detiene el envo de datos
            resetearSnapshotPWVyPausa();
            pantallaActual = MENU;
            dibujarMenuPrincipal();
            delay(300);
        } 
        // CANCELAR
        else {  
            dibujarPantallaPC(); // Redibujamos la pantalla azul para limpiar el popup
        }
      }
    }

    // PANTALLA ERROR WIFI ---------------------------------------------------------------
    else if (pantallaActual == PANTALLA_ERROR_WIFI) {
      // Boton reintentar
      if (x > 170 && x < 310 && y > 190 && y < 235) {
          sonarPitido();
          iniciarWiFi(); // Intenta conectar de nuevo
          
          if (wifiConectado) {
              pantallaActual = PANTALLA_PC_ESPERA;
              dibujarPantallaPC();
              medicionActiva = true;
          } else {
              dibujarPantallaFalloWiFi(); // Si vuelve a fallar, redibuja el error
          }
      }
      
      // Botn VOLVER
      else if (x > 28 && x < 72 && y > 253 && y < 297) {
          sonarPitido();
          pantallaActual = MENU;
          dibujarMenuPrincipal();
          delay(300);
      }
    }


    // PANTALLA DATOS --------------------------------------------------------------
    // Ingresar Edad
    else if (pantallaActual == PANTALLA_EDAD) {
       int tstartX = 180; int tstartY = 140; int tgapX = 55; int tgapY = 45; 
       for (int i = 0; i < 11; i++) {
         int row = i / 3; int col = i % 3;
         if (i == 9) { col = 1; row = 3; } if (i == 10) { col = 2; row = 3; }
         int kX = tstartX + (col * tgapX); int kY = tstartY + (row * tgapY);
         if (x > kX - 22 && x < kX + 22 && y > kY - 17 && y < kY + 17) {
            sonarPitido();
            if (i < 9) edadInput += String(i + 1);
            else if (i == 9) edadInput += "0";
            else if (i == 10) edadInput = "";
            if (edadInput.length() > 3) edadInput = edadInput.substring(0, 3);

            // Actualizar valor ingresado
            actualizarDisplayEdad();
            dibujarBotonSiguiente(edadInput.length() > 0);
            delay(250);
         }
       }

       // Boton volver
       if ((x-50)*(x-50)+(y-275)*(y-275) <= 900) { 
          sonarPitido(); 
          dibujarMenuPrincipal(); 
          delay(300); 
       }

       // Boton Siguiente
       if (edadInput.length() > 0 && (x-430)*(x-430)+(y-275)*(y-275) <= 900) {
          int val = edadInput.toInt();
          
          // RANGO VLIDO
          if (val >= 10 && val <= 100) {  
             sonarPitido(); 
             pacienteEdad = val; 
             dibujarTecladoAltura();
             delay(300);
          } 
          
          // RANGO INVLIDO
          else {   
             sonarAlerta();
             mostrarAlertaValorInvalido("EDAD INVALIDA", "entre 10 y 100 anos");;
             dibujarTecladoEdad();  // Redibujar la pantalla para limpiar el popup
          }
       }
    }


    // Ingresar altura
    else if (pantallaActual == PANTALLA_ALTURA) {
       int tstartX = 180; int tstartY = 140; int tgapX = 55; int tgapY = 45; 
       for (int i = 0; i < 11; i++) {
         int row = i / 3; int col = i % 3;
         if (i == 9) { col = 1; row = 3; } if (i == 10) { col = 2; row = 3; }
         int kX = tstartX + (col * tgapX); int kY = tstartY + (row * tgapY);
         if (x > kX - 22 && x < kX + 22 && y > kY - 17 && y < kY + 17) {
            sonarPitido();
            if (i < 9) alturaInput += String(i + 1);
            else if (i == 9) alturaInput += "0";
            else if (i == 10) alturaInput = "";
            if (alturaInput.length() > 3) alturaInput = alturaInput.substring(0, 3);
            actualizarDisplayAltura();
            dibujarBotonSiguiente(alturaInput.length() > 0);
            delay(250);
         }
       }
       if ((x-50)*(x-50)+(y-275)*(y-275) <= 900) { 
          sonarPitido(); 
          dibujarTecladoEdad(); 
          delay(300); 
       }


       // Boton Siguiente
       if (alturaInput.length() > 0 && (x-430)*(x-430)+(y-275)*(y-275) <= 900) {
          int val = alturaInput.toInt();
          
          if (val >= 120 && val <= 220) {
             // RANGO VLIDO
             sonarPitido(); 
             pacienteAltura = val; 
             resetearSnapshotPWVyPausa();
             pantallaActual = PANTALLA_MEDICION_RAPIDA; 
             dibujarPantallaMedicion();
             medicionActiva = true; 
             portENTER_CRITICAL(&bufferMux); faseMedicion = 0; s1ok = false; s2ok = false; portEXIT_CRITICAL(&bufferMux); 
             delay(300);
          } 
          
          else {
             // RANGO INVLIDO
             sonarAlerta();
             mostrarAlertaValorInvalido("ALTURA INVALIDA", "entre 120 y 220 cm");
             dibujarTecladoAltura(); // Redibujar la pantalla para limpiar el popup
          }
       }
    }
    


    // PANTALLA MEDICIN ----------------------------------------------------------------
    else if (pantallaActual == PANTALLA_MEDICION_RAPIDA) {
          if (errorMedicionBloqueanteActivo()) {
              // Botn REINTENTAR (misma ubicacin que error de conexin)
              if (x > 170 && x < 310 && y > 190 && y < 245) {
                  sonarPitido();
                  graficoPausado = false;
                  portENTER_CRITICAL(&bufferMux);
                  faseMedicion = 0;
                  porcentajeEstabilizacion = 0;
                  porcentajeCalculando = 0;
                  conteoRRValidos = 0;
                  conteoPTTValidos = 0;
                  bpmMostrado = 0;
                  pwvMostrado = 0.0f;
                  medicionFinalizada = false;
                  pwvResultadoValido = false;
                  hrResultadoValido = false;
                  resetearBuffersPWVSolicitado = true;
                  portEXIT_CRITICAL(&bufferMux);

                  dibujarPantallaMedicion();
                  actualizarMedicion();
                  delay(250);
              }
              // Botn volver
              else if ((x-50)*(x-50)+(y-275)*(y-275) <= 900) {
                  sonarPitido();
                  medicionActiva = false;
                  resetearSnapshotPWVyPausa();

                  // Volver a corregir altura
                  pantallaActual = PANTALLA_ALTURA;
                  alturaInput = String(pacienteAltura); // Recordar el dato
                  dibujarTecladoAltura();
                  delay(300);
              }
          }

          // Cambio de Modo Visualizacin
          else if (y < 45) {
              // Botn PAUSA/REANUDAR
              if (mostrarBotonPausaEnHeader() && x >= BTN_PAUSA_X && x <= (BTN_PAUSA_X + BTN_PAUSA_W) && y >= BTN_PAUSA_Y && y <= (BTN_PAUSA_Y + BTN_PAUSA_H)) {
                sonarPitido();
                graficoPausado = !graficoPausado;
                if (graficoPausado) {
                  capturarSnapshotPausa();
                }
                dibujarPantallaMedicion();
                actualizarMedicion();
                delay(200);
              }
              // Icono Mtricas
              else if (x >= BTN_METRICAS_X && x <= (BTN_METRICAS_X + BTN_ICON_SIZE)) {
                if (modoVisualizacion != 1) {
                    sonarPitido();
                    modoVisualizacion = 1;
                    dibujarPantallaMedicion(); // Redibujar estructura
                    actualizarMedicion();      // Actualizar datos
                    delay(200);
                }
              }
              // Icono Curvas
              else if (x >= BTN_CURVAS_X && x <= (BTN_CURVAS_X + BTN_ICON_SIZE)) {
                if (modoVisualizacion != 0) {
                    sonarPitido();
                    modoVisualizacion = 0;
                    dibujarPantallaMedicion();
                    actualizarMedicion();
                    delay(200);
                }
              }
          }

          // Botn salir
          else if (x > 360 && y > 260) {
              sonarPitido();
              
              // Confirmar salida
              // SALIR
              if (confirmarSalir()) {   
                  medicionActiva = false; 
                  resetearSnapshotPWVyPausa();
                  pantallaActual = MENU;
                  dibujarMenuPrincipal();
                  delay(300);
              } 
              // CANCELAR
              else {                  
                  dibujarPantallaMedicion(); 
                  actualizarMedicion(); // Vuelven a aparecer los datos/curvas
              }
          }

          // Botn volver
          else if (x < 100 && y > 250) {
            sonarPitido();
            medicionActiva = false;
            resetearSnapshotPWVyPausa();
            
            // Volver a corregir altura
            pantallaActual = PANTALLA_ALTURA;
            alturaInput = String(pacienteAltura); // Recordar el dato
            dibujarTecladoAltura();
            delay(300);
          }
    }
  }

  // Actualizacin contnua del grfico (si estamos midiendo)
  if (modoActual == MODO_TEST_RAPIDO && medicionActiva && pantallaActual == PANTALLA_MEDICION_RAPIDA && !graficoPausado) {
    if (millis() - lastDrawTime >= DRAW_INTERVAL) {
      lastDrawTime = millis();
      actualizarMedicion();
    }
  }

}

