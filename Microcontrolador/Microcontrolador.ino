// ==============================================================================================
// LIBRERÍAS
// ==============================================================================================
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include "MAX30105.h"
#include <math.h>
#include "bitmaps.h" // Imagenes


// ==============================================================================================
// CONFIGURACIÓN
// ==============================================================================================

// Wi-Fi  ===============================================================
const char* ssid = "Di Toro1 2.4GHz";     // WiFi 
const char* password = "ditoro3080";   // Contraseña
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

// Datos de señal
volatile float buffer_s1[BUFFER_SIZE];
volatile float buffer_s2[BUFFER_SIZE];
volatile unsigned long buffer_time[BUFFER_SIZE]; 
volatile int writeHead = 0; 

// Control
volatile bool medicionActiva = false;
volatile int faseMedicion = 0; // 0=Esperando, 1=Estabilizando, 2=Midiendo
volatile int porcentajeEstabilizacion = 0;

volatile bool s1ok = false;
volatile bool s2ok = false;
volatile bool s1_conectado = true;
volatile bool s2_conectado = true;
const uint8_t SENSOR_I2C_ADDR = 0x57; // Dirección del MAX30102

// Resultados
volatile int bpmMostrado = 0; 
volatile float pwvMostrado = 0.0; 

// Paciente (Datos que vienen de UI local o de PC)
volatile int pacienteEdad = 0;
volatile int pacienteAltura = 0; // Se llenará desde la PC en modo estudio clínico

// MODO DE OPERACIÓN
enum ModoOperacion { MODO_TEST_RAPIDO, MODO_ESTUDIO_CLINICO };
ModoOperacion modoActual = MODO_TEST_RAPIDO;
// MODO DE VISUALIZACIÓN:  1 = Métricas , 0 = Curvas 
int modoVisualizacion = 1;

portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// GLOBALES PARA TASKSENSORES (evitar stack overflow)
const int MA_SIZE = 4;
float bufMA1_global[4] = {0};
float bufMA2_global[4] = {0};
int idxMA_global = 0;

const int AVG_SIZE = 10;
int bpmBuffer_global[10] = {0}; 
int bpmIdx_global = 0;
int validSamplesBPM_global = 0;
float pwvBuffer_global[10] = {0.0}; 
int pwvIdx_global = 0;
int validSamplesPWV_global = 0;

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

// Gráfico
#define GRAPH_W 458  // 460 - 2 píxeles de bordes
#define GRAPH_H 178  // 180 - 2 píxeles de bordes
#define GRAPH_X 11   // 10 (del marco) + 1 de margen
#define GRAPH_Y 51   // 50 (del marco) + 1 de margen
unsigned long lastDrawTime = 0;
const unsigned long DRAW_INTERVAL = 40; 


// ======================================================================
// WEBSOCKET EVENTS (RECEPCIÓN DE DATOS DESDE PC)
// ======================================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if(type == WStype_TEXT) {
        // Parseo MANUAL simple para evitar librerías pesadas (JSON)
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
// CORE 0: MOTOR MATEMÁTICO + ENVÍO WEBSOCKET
// ======================================================================
bool verificarConexionI2C(TwoWire &wirePort) {
    wirePort.beginTransmission(SENSOR_I2C_ADDR);
    return (wirePort.endTransmission() == 0);
}


void TaskSensores(void *pvParameters) {

  // Filtros anti-deriva mejorados
  float s1_lp = 0, s1_dc = 0;
  float s2_lp = 0, s2_dc = 0;
  float s1_hp = 0, s2_hp = 0;
  const float ALPHA_LP = 0.75;
  const float ALPHA_DC = 0.97;
  const float ALPHA_HP_S1 = 0.97;
  const float ALPHA_HP_S2 = 0.95;
  const long SENSOR_THRESHOLD = 50000;

  // Tiempo
  unsigned long startContactTime = 0;
  unsigned long baseTime = 0;
  const unsigned long TIEMPO_ESTABILIZACION = 10000;

  // Detección de picos locales (reemplaza checkForBeat/checkForBeatS1)
  const unsigned long REFRACT_MS = 300;
  const unsigned long PTT_MIN_MS = 20;
  const unsigned long PTT_MAX_MS = 200;
  const int THRESH_WINDOW_SAMPLES = 800; // ~2 s a 400 SPS

  static float thrWinS1[THRESH_WINDOW_SAMPLES] = {0};
  static float thrWinS2[THRESH_WINDOW_SAMPLES] = {0};
  int thrIdx = 0;
  int thrCount = 0;
  float thrSumS1 = 0.0f, thrSumSqS1 = 0.0f;
  float thrSumS2 = 0.0f, thrSumSqS2 = 0.0f;

  int peakPriming = 0;
  float s1_prev2 = 0.0f, s1_prev1 = 0.0f;
  float s2_prev2 = 0.0f, s2_prev1 = 0.0f;
  unsigned long prev1Time = 0;

  unsigned long lastPeakTimeS1 = 0;
  unsigned long lastPeakTimeS2 = 0;
  unsigned long lastDistPeakForHR = 0;
  unsigned long pendingProxPeakTime = 0;
  bool waitingForS2 = false;

  // Hardware Check
  unsigned long lastHardwareCheck = 0;
  const int CHECK_INTERVAL = 500;

  for (;;) {
    if (medicionActiva) {

      if (modoActual == MODO_ESTUDIO_CLINICO) {
        webSocket.loop();
      }

      // --- BLOQUE 1: VERIFICACIÓN DE HARDWARE ---
      if (millis() - lastHardwareCheck > CHECK_INTERVAL) {
        lastHardwareCheck = millis();

        // ---------------- SENSOR 1 (WIRE / PROXIMAL) ----------------
        bool s1_fisico = verificarConexionI2C(Wire);
        
        if (!s1_conectado && s1_fisico) {
          // Se acaba de reconectar físicamente. Intentamos revivirlo:
          
          // 1. Reiniciamos el BUS Wire por si quedó "tonto"
          Wire.begin(SDA1, SCL1, 100000); 
          delay(10); 

          // 2. Intentamos inicializar la librería
          if (sensorProx.begin(Wire, I2C_SPEED_STANDARD)) {
              sensorProx.softReset(); // Reset de software para limpiar registros basura
              delay(10);
              sensorProx.setup(30, 8, 2, 400, 411, 4096);
              s1_conectado = true; // ÉXITO: Ahora sí lo marcamos como conectado
          } else {
              s1_conectado = false; // Falló el handshake lógico, seguimos intentando
          }
        } 
        else if (s1_conectado && !s1_fisico) {
           s1_conectado = false; // Se desconectó
        }


        // ---------------- SENSOR 2 (WIRE1 / DISTAL) ----------------
        bool s2_fisico = verificarConexionI2C(Wire1);
        
        if (!s2_conectado && s2_fisico) {
          // 1. Reiniciamos el BUS Wire1
          Wire1.begin(SDA2, SCL2, 100000);
          delay(10);

          // 2. Inicializamos librería
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

          // Chequeo sensores colocados (Dedo puesto)
          bool currentS1 = (ir1 > SENSOR_THRESHOLD);
          bool currentS2 = (ir2 > SENSOR_THRESHOLD);
          s1ok = currentS1;
          s2ok = currentS2;

          if (currentS1 && currentS2) {
            // --- PROCESAMIENTO DE SEÑAL ---
            
            // 1. Low-Pass
            if (s1_lp == 0) s1_lp = ir1; if (s2_lp == 0) s2_lp = ir2;
            s1_lp = (s1_lp * ALPHA_LP) + (ir1 * (1.0 - ALPHA_LP));
            s2_lp = (s2_lp * ALPHA_LP) + (ir2 * (1.0 - ALPHA_LP));

            // 2. DC Removal
            if (s1_dc == 0) s1_dc = ir1; if (s2_dc == 0) s2_dc = ir2;
            s1_dc = (s1_dc * ALPHA_DC) + (s1_lp * (1.0 - ALPHA_DC));
            s2_dc = (s2_dc * ALPHA_DC) + (s2_lp * (1.0 - ALPHA_DC));

            float val1_centered = s1_lp - s1_dc;
            float val2_centered = s2_lp - s2_dc;

            // 3. High-Pass Filter
            s1_hp = ALPHA_HP_S1 * (s1_hp + val1_centered - lastVal1_centered);
            s2_hp = ALPHA_HP_S2 * (s2_hp + val2_centered - lastVal2_centered);
            lastVal1_centered = val1_centered;
            lastVal2_centered = val2_centered;

            // 4. Media Móvil
            bufMA1_global[idxMA_global] = s1_hp;
            bufMA2_global[idxMA_global] = s2_hp;
            idxMA_global = (idxMA_global + 1) % MA_SIZE;

            float sum1 = 0; float sum2 = 0;
            for (int i = 0; i < MA_SIZE; i++) { sum1 += bufMA1_global[i]; sum2 += bufMA2_global[i]; }
            float valFinal1 = sum1 / MA_SIZE;
            float valFinal2 = sum2 / MA_SIZE;

            // 5. Threshold adaptativo: mean + 0.5 * std (ventana móvil ~2 s)
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
            if (thrCount > 0) {
              float meanS1 = thrSumS1 / (float)thrCount;
              float meanS2 = thrSumS2 / (float)thrCount;
              float varS1 = (thrSumSqS1 / (float)thrCount) - (meanS1 * meanS1);
              float varS2 = (thrSumSqS2 / (float)thrCount) - (meanS2 * meanS2);
              if (varS1 < 0.0f) varS1 = 0.0f;
              if (varS2 < 0.0f) varS2 = 0.0f;
              thresholdS1 = meanS1 + (0.5f * sqrtf(varS1));
              thresholdS2 = meanS2 + (0.5f * sqrtf(varS2));
            }

            // 6. Detector explícito de máximos locales sobre señal filtrada
            bool peakS1 = false;
            bool peakS2 = false;
            unsigned long peakTimeS1 = 0;
            unsigned long peakTimeS2 = 0;

            if (peakPriming >= 2) {
              if ((s1_prev2 < s1_prev1) && (s1_prev1 > valFinal1) && (s1_prev1 > thresholdS1)) {
                if ((prev1Time - lastPeakTimeS1) > REFRACT_MS) {
                  peakS1 = true;
                  peakTimeS1 = prev1Time;
                  lastPeakTimeS1 = prev1Time;
                }
              }

              if ((s2_prev2 < s2_prev1) && (s2_prev1 > valFinal2) && (s2_prev1 > thresholdS2)) {
                if ((prev1Time - lastPeakTimeS2) > REFRACT_MS) {
                  peakS2 = true;
                  peakTimeS2 = prev1Time;
                  lastPeakTimeS2 = prev1Time;
                }
              }
            }

            if (peakPriming == 0) {
              s1_prev1 = valFinal1;
              s2_prev1 = valFinal2;
              prev1Time = sampleTimestamp;
              peakPriming = 1;
            } else {
              s1_prev2 = s1_prev1;
              s2_prev2 = s2_prev1;
              s1_prev1 = valFinal1;
              s2_prev1 = valFinal2;
              prev1Time = sampleTimestamp;
              if (peakPriming < 2) peakPriming = 2;
            }
            
            // --- ALGORITMOS HR / PWV ---
            if (faseMedicion == 2) {
              // HR desde picos distales (S2)
              if (peakS2) {
                if (lastDistPeakForHR > 0) {
                  long delta = peakTimeS2 - lastDistPeakForHR;
                    float beatsPerMinute = 60.0 / (delta / 1000.0);

                    if (beatsPerMinute > 40 && beatsPerMinute < 200) {
                        bpmBuffer_global[bpmIdx_global] = (int)beatsPerMinute;
                        bpmIdx_global = (bpmIdx_global + 1) % AVG_SIZE;
                        if (validSamplesBPM_global < AVG_SIZE) validSamplesBPM_global++;

                        if (validSamplesBPM_global >= 5) {
                            long total = 0;
                            int count = (validSamplesBPM_global < AVG_SIZE) ? validSamplesBPM_global : AVG_SIZE;
                            for (int i = 0; i < count; i++) total += bpmBuffer_global[i];
                            bpmMostrado = total / count;
                        }
                    }
                  }
                    lastDistPeakForHR = peakTimeS2;

                  }

                  // PTT desde proximal -> distal
                  if (peakS1) {
                  // Inicia ventana de búsqueda distal para PTT
                  pendingProxPeakTime = peakTimeS1;
                  waitingForS2 = true;
               }

               // Cálculo PTT/PWV: primer pico distal válido luego del proximal
               if (waitingForS2) {
                  if (peakS2) {
                    long transitTime = peakTimeS2 - pendingProxPeakTime;

                    if (transitTime > (long)PTT_MIN_MS && transitTime < (long)PTT_MAX_MS) {
                      int alturaCalc = (pacienteAltura > 0) ? pacienteAltura : 170;
                      float distMeters = (alturaCalc * 0.436) / 100.0;
                      float instantPWV = distMeters / (transitTime / 1000.0);

                      if (instantPWV > 3.0 && instantPWV < 50.0) {
                        pwvBuffer_global[pwvIdx_global] = instantPWV;
                        pwvIdx_global = (pwvIdx_global + 1) % AVG_SIZE;
                        if (validSamplesPWV_global < AVG_SIZE) validSamplesPWV_global++;

                        if (validSamplesPWV_global >= 2) {
                          float totalPWV = 0;
                          int count = (validSamplesPWV_global < AVG_SIZE) ? validSamplesPWV_global : AVG_SIZE;
                          for (int i = 0; i < count; i++) totalPWV += pwvBuffer_global[i];
                          pwvMostrado = totalPWV / count;
                        }
                      }
                      waitingForS2 = false;
                    } else if (transitTime >= (long)PTT_MAX_MS) {
                      waitingForS2 = false;
                    }
                  }

                  // Timeout de la ventana distal
                  if (waitingForS2 && ((sampleTimestamp - pendingProxPeakTime) > PTT_MAX_MS)) {
                    waitingForS2 = false;
                  }
               }
            } // Fin Fase 2

            // --- SALIDA DE DATOS ---
            if (modoActual == MODO_TEST_RAPIDO) {
              if (faseMedicion == 2) {
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
              char json[128];
              snprintf(json, sizeof(json), "{\"s1\":true,\"s2\":true,\"p\":%.2f,\"d\":%.2f,\"hr\":%d,\"pwv\":%.2f}",
                       valFinal1, valFinal2, bpmMostrado, pwvMostrado);
              webSocket.broadcastTXT(json);
            }

            // --- MÁQUINA DE ESTADOS ---
            if (faseMedicion == 0) {
              faseMedicion = 1;
              startContactTime = millis();
            }
            else if (faseMedicion == 1) {
              unsigned long transcurrido = millis() - startContactTime;
              porcentajeEstabilizacion = (transcurrido * 100) / TIEMPO_ESTABILIZACION;
              if (transcurrido >= TIEMPO_ESTABILIZACION) {
                faseMedicion = 2; baseTime = millis();
                if (modoActual == MODO_TEST_RAPIDO) {
                  portENTER_CRITICAL(&bufferMux);
                  writeHead = 0;
                  for (int i = 0; i < BUFFER_SIZE; i++) { buffer_s1[i] = 0; buffer_s2[i] = 0; buffer_time[i] = 0; }
                  portEXIT_CRITICAL(&bufferMux);
                }
                bpmMostrado = 0; pwvMostrado = 0.0;
                validSamplesBPM_global = 0; validSamplesPWV_global = 0;
                waitingForS2 = false;
                pendingProxPeakTime = 0;
                lastDistPeakForHR = 0;
                lastPeakTimeS1 = 0;
                lastPeakTimeS2 = 0;
              }
            }

          } else {
            // --- SIN DEDOS (Connected but no finger) ---
            if (modoActual == MODO_ESTUDIO_CLINICO) {
              webSocket.broadcastTXT("{\"s1\":false,\"s2\":false}");
            }
            if (faseMedicion != 0) {
              faseMedicion = 0; porcentajeEstabilizacion = 0;
              s1_lp = 0; s1_dc = 0; s2_lp = 0; s2_dc = 0;
              s1_hp = 0; s2_hp = 0;
              lastVal1_centered = 0; lastVal2_centered = 0;
              bpmMostrado = 0; pwvMostrado = 0.0;
              waitingForS2 = false;
              pendingProxPeakTime = 0;
              lastDistPeakForHR = 0;
              lastPeakTimeS1 = 0;
              lastPeakTimeS2 = 0;
              peakPriming = 0;
              thrIdx = 0;
              thrCount = 0;
              thrSumS1 = 0.0f; thrSumSqS1 = 0.0f;
              thrSumS2 = 0.0f; thrSumSqS2 = 0.0f;
              sensorProx.nextSample(); sensorDist.nextSample();
            }
          }
        }
      } else {
        // --- CABLES DESCONECTADOS ---
        // Si hay desconexión física, reseteamos la lógica para que al volver empiece de 0
        faseMedicion = 0;
        porcentajeEstabilizacion = 0;
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

  // Bucle de conexión
  int intentos = 0;
  int maxIntentos = 20;
  while (WiFi.status() != WL_CONNECTED && intentos < maxIntentos) {
    delay(500);
    // Animación de la barra
    int progreso = map(intentos, 0, maxIntentos, 0, barW - 4);
    tft.fillRect(barX + 2, barY + 2, progreso, barH - 4, TFT_GREEN);
    intentos++;
  }

  // WIFI CONECTADO
  if (WiFi.status() == WL_CONNECTED) {
    wifiConectado = true;
    IPAddress ip = WiFi.localIP();
    Serial.println("\nWiFi Conectado! IP: " + ip.toString());
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
  } 
  
  // ERROR DE CONEXIÓN
  else {
    wifiConectado = false;
  }
}


// ==================================================================================================================================================================
// INTERFAZ GRÁFICA
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
  
  // Decidir el color del boton según el estado: "true" (verde) o "false" (gris)
  uint16_t colorFondo = activo ? COLOR_BOTON_SIGUIENTE : COLOR_BOTON;
  tft.fillCircle(circX, circY, r, colorFondo);
  tft.fillTriangle(circX+11, circY, circX-2, circY-7, circX-2, circY+7, COLOR_FLECHA);
  tft.fillRect(circX-10, circY-2, 9, 5, COLOR_FLECHA);
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
  // Título
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString(titulo, 230, py + 95, 4);      
  tft.drawString(titulo, 230 + 1, py + 95, 4);  // Negrita
  // Mensaje del error
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("Por favor ingrese un valor", 230, py + 125, 2); // Renglón 1: Frase fija
  tft.drawString(mensaje, 230, py + 145, 2);                      // Renglón 2: Rango válido

  // Botón OK
  int okW = 120, okH = 35; int okX = 230 - (okW / 2); int okY = py + 165;
  tft.fillRoundRect(okX, okY, okW, okH, 5, COLOR_BOTON);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("OK", 230, okY + 15, 2);

  // El código se queda atrapado aquí hasta que el usuario toque el botón de OK
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

    yield(); // Evita que el ESP32 crea que se colgó
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
  // Botón CANCELAR
  tft.fillRoundRect(cancelX, btnY, btnW, btnH, 8, TFT_LIGHTGREY);
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("CANCELAR", cancelX + btnW/2, btnY + btnH/2, 2);
  // Botón SALIR
  tft.fillRoundRect(salirX, btnY, btnW, btnH, 8, COLOR_BOTON_ACCION);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("SALIR", salirX + btnW/2, btnY + btnH/2, 2);

  // El código se queda atrapado aquí hasta que el usuario toque un botón
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
  
  // Botón TEST RÁPIDO
  tft.fillRoundRect(btnX, btnY1, btnW, btnH, 35, COLOR_BOTON_ACCION2);
  tft.setTextColor(COLOR_TEXTO_BOTON_ACCION);
  tft.drawString("TEST RAPIDO", 240, btnY1 + 36, 4);
  
  // Botón ESTUDIO CLÍNICO
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
  // Botón Salir
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
  tft.drawString("Por favor, revise su conexion a la red.", 240, 170, 2); // Texto más pequeño

  // Botón reintentar
  tft.fillRoundRect(170, 200, 140, 45, 22, COLOR_BOTON); 
  tft.setTextColor(COLOR_TEXTO);
  tft.drawString("REINTENTAR", 240, 222, 2);
  // Botón volver
  dibujarBotonVolver(); 
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
  
  // Teclado Numérico
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
  
  // Teclado Numérico
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
  tft.pushImage(390, 5, 30, 30, (const uint16_t*)epd_bitmap_IconoMetricas);  // Icono Métricas
  tft.pushImage(435, 5, 30, 30, (const uint16_t*)epd_bitmap_IconoCurvas);    // Icono Curvas
  

  // Indicador de selección
  if (modoVisualizacion == 1) {
      // Recuadro para Icono Métricas
      tft.drawRect(390 - 2, 5 - 2, 30 + 4, 30 + 4, COLOR_BOTON_ACCION);
      tft.drawRect(390 - 1, 5 - 1, 30 + 2, 30 + 2, COLOR_BOTON_ACCION);
  } else {
      // Recuadro para Icono Curvas
      tft.drawRect(435 - 2, 5 - 2, 30 + 4, 30 + 4, COLOR_BOTON_ACCION);
      tft.drawRect(435 - 1, 5 - 1, 30 + 2, 30 + 2, COLOR_BOTON_ACCION);
  }

  // Botón Volver
  dibujarBotonVolver();

  // Botón Salir
  tft.fillRoundRect(380, 255, 80, 40, 20, COLOR_BOTON_ACCION);
  tft.setTextDatum(MC_DATUM); 
  tft.setTextColor(COLOR_TEXTO_BOTON_ACCION, COLOR_BOTON_ACCION);
  tft.drawString("SALIR", 420, 275, 2);
}


void actualizarMedicion() {
  float localBuf1[BUFFER_SIZE];                          // Buffer proximal
  float localBuf2[BUFFER_SIZE];                          // Buffer distal
  unsigned long localTime[BUFFER_SIZE];                  // Timestamp
  int localFase = faseMedicion;                          // Estado del sistema
  static int faseAnterior = -1; 
  static unsigned long ultimoSonido = 0;                 // Memoria del cronómetro de alarma
  int localHead;                                         // Indica donde empezar a leer
  int localPorcentaje = porcentajeEstabilizacion;        // Porcentaje barra
  int localBPM = bpmMostrado;                            // BPM
  float localPWV = pwvMostrado;                          // PWV

  // Copiar los buffers para graficar (Protección con Mutex)
  portENTER_CRITICAL(&bufferMux); 
  if (localFase == 2) {
    memcpy(localBuf1, (const void*)buffer_s1, sizeof(buffer_s1));
    memcpy(localBuf2, (const void*)buffer_s2, sizeof(buffer_s2));
    memcpy(localTime, (const void*)buffer_time, sizeof(buffer_time));
    localHead = writeHead;   
  }
  portEXIT_CRITICAL(&bufferMux);

  // Limpiar pantalla si cambia la fase
  if (localFase != faseAnterior) {
      tft.fillRect(0, 0, 380, 50, COLOR_FONDO);   // Borra las leyendas
      tft.fillRect(0, 48, 480, 185, COLOR_FONDO);  // Borra gráfico  
      tft.fillRect(80, 260, 280, 40, COLOR_FONDO);  // Borra resultados
      faseAnterior = localFase;    
  }

  // ==========================================================================================
  // ESTADO DE ERROR / ESPERA (Fase 0)
  // Aquí es donde distinguimos entre "Desconectado" y "No Colocado"
  // ==========================================================================================
  if (localFase == 0) {
    graphSprite.fillSprite(COLOR_FONDO); 
    graphSprite.setTextDatum(MC_DATUM); 
    int yCentro = GRAPH_H / 2; 
    int yIcono = yCentro - 75; 
    int yMsg1 = yCentro + 10;
    int yMsg2 = yCentro + 50;

    // Dibujar Icono de Advertencia (común para ambos casos)
    graphSprite.setSwapBytes(true);
    graphSprite.pushImage((GRAPH_W - 60) / 2, yIcono, 60, 60, (const uint16_t*)epd_bitmap_IconoAdvertencia); 
    graphSprite.setSwapBytes(false);

    // -----------------------------------------------------------------------
    // PRIORIDAD 1: DESCONEXIÓN FÍSICA (Cables sueltos)
    // -----------------------------------------------------------------------
    if (!s1_conectado || !s2_conectado) {
        
        // Alarma Sonora RÁPIDA (Beep-Beep constante)
        if (millis() - ultimoSonido > 200) { 
            // Genera un pitido corto manual sin bloquear mucho tiempo
            digitalWrite(BUZZER_PIN, HIGH);
            delay(30); 
            digitalWrite(BUZZER_PIN, LOW);
            ultimoSonido = millis();
        }

        // Sub-caso: AMBOS desconectados
        if (!s1_conectado && !s2_conectado) {
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
        else if (!s1_conectado) {
            graphSprite.setTextDatum(MC_DATUM);
            graphSprite.setTextColor(COLOR_S1); 
            graphSprite.drawString("Sensor Proximal (1)", GRAPH_W/2, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString("desconectado", GRAPH_W/2, yMsg2, 4);
        }
        // Sub-caso: SOLO S2 desconectado
        else if (!s2_conectado) {
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
            sonarAlerta(); // Tu función de doble pitido
            ultimoSonido = millis();
        }

        // CASO 1: Ambos sensores no colocados
        if (!s1ok && !s2ok) {
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
        else if (!s1ok) {
            graphSprite.setTextDatum(MC_DATUM);
            graphSprite.setTextColor(COLOR_S1); 
            graphSprite.drawString("Sensor Proximal (1)", GRAPH_W/2, yMsg1, 4);
            graphSprite.setTextColor(COLOR_TEXTO); 
            graphSprite.drawString("no colocado", GRAPH_W/2, yMsg2, 4);
        }
        // CASO 3: Sensor 2 no colocado
        else if (!s2ok) {
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
 
  // CALCULANDO -----------------------------------------------------------------------------------
  if (localFase == 1) {
    graphSprite.fillSprite(COLOR_FONDO); 
    graphSprite.setTextDatum(MC_DATUM); 
    graphSprite.setTextColor(COLOR_TEXTO);
    graphSprite.drawString("CALCULANDO ...", GRAPH_W/2, GRAPH_H/2 - 20, 4);
    int barW = 200; int barH = 20; int barX = (GRAPH_W - barW)/2;  int barY = GRAPH_H/2 + 10;
    graphSprite.drawRect(barX, barY, barW, barH, TFT_BLACK);   // Barra de progreso
    int fillW = (barW * localPorcentaje) / 100;
    graphSprite.fillRect(barX+1, barY+1, fillW-2, barH-2, TFT_GREEN);
    graphSprite.pushSprite(11, 51); 
    return;
  }

  // VISUALIZACIÓN MODO MÉTRICAS -----------------------------------------------------------------
  if (modoVisualizacion == 1) {
      int centroX = 240;
      tft.setTextDatum(MC_DATUM);

      // PWV
      tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      tft.drawString("PWV", centroX, 80, 4);
      tft.setTextColor(TFT_GREEN, COLOR_FONDO);
      tft.setTextPadding(240);
      if (localPWV > 0) {
        // Si hay dato
        tft.drawString(String(localPWV, 1), centroX - 30, 150, 7);
        tft.setTextPadding(0);
        tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
        tft.drawString("m/s", centroX + 70, 160, 4); 
      } else {
        // Si no hay dato
        tft.drawString("---", centroX, 150, 7);
        tft.setTextPadding(0);
      }

      // HR
      int yHR = 220;
      tft.setSwapBytes(true);
      tft.pushImage(centroX - 72, yHR - 12, 24, 24, (const uint16_t*)epd_bitmap_ImgCorazon); // Imagen corazon
      tft.setSwapBytes(false);
      tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      tft.setTextPadding(96); // Ancho de limpieza seguro
      int xTexto = centroX + 10; 
      if (localBPM > 0) {
         // Si hay dato
         tft.drawString(String(localBPM) + " BPM", xTexto, yHR, 4);
      } else {
         // Si no hay dato
         tft.drawString("---", centroX, yHR, 4);
      }
      
      tft.setTextPadding(0); // Apagar padding
      
  }

  // VISUALIZACIÓN MODO CURVAS ---------------------------------------------------------------------
  else {
      graphSprite.fillSprite(COLOR_FONDO);  // Limpiar sprite

      // Recuadro para gráfico
      tft.drawRect(10, 50, 460, 180, TFT_BLACK); 
      // Referencias sensores
      tft.setTextDatum(ML_DATUM); 
      tft.setTextColor(COLOR_S1, COLOR_FONDO); tft.drawString("Sensor Proximal (1)  ", 15, 25, 2);  
      tft.setTextColor(COLOR_S2, COLOR_FONDO); tft.drawString("Sensor Distal (2)    ", 165, 25, 2);
      
      // 1. Calcular Escala (Min/Max)
      float minV = -50, maxV = 50;
      for(int i=0; i<BUFFER_SIZE; i+=5) { 
        if(localBuf1[i] < minV) minV = localBuf1[i]; 
        if(localBuf1[i] > maxV) maxV = localBuf1[i];
      }
      if((maxV - minV) < 100) { float mid = (maxV + minV) / 2; maxV = mid + 50; minV = mid - 50; }
      
      // 2. Dibujar Grilla y Ejes
      graphSprite.drawFastHLine(0, GRAPH_H/2, GRAPH_W, TFT_DARKGREY); 
      float xStep = (float)GRAPH_W / (float)BUFFER_SIZE; 
      graphSprite.setTextDatum(TC_DATUM); graphSprite.setTextColor(TFT_DARKGREY);

      // 3. Bucle de Dibujado de Líneas
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
        
        int margenSuperior = 28;
        int margenInferior = GRAPH_H - 38;

        int y1A = map((long)localBuf1[idx], (long)minV, (long)maxV, margenInferior, margenSuperior);
        int y1B = map((long)localBuf1[nextIdx], (long)minV, (long)maxV, margenInferior, margenSuperior);
        int y2A = map((long)localBuf2[idx], (long)minV, (long)maxV, margenInferior, margenSuperior);
        int y2B = map((long)localBuf2[nextIdx], (long)minV, (long)maxV, margenInferior, margenSuperior);
        
        graphSprite.drawLine(x1, constrain(y1A,0,GRAPH_H), x2, constrain(y1B,0,GRAPH_H), COLOR_S1);
        graphSprite.drawLine(x1, constrain(y2A,0,GRAPH_H), x2, constrain(y2B,0,GRAPH_H), COLOR_S2); 
      }
      
      graphSprite.pushSprite(11, 51); 

      // Datos numéricos 
      int yInfo = 280; 
      
      // PWV
      tft.setTextDatum(ML_DATUM); 
      tft.setTextColor(COLOR_TEXTO, COLOR_FONDO);
      tft.drawString("PWV = ", 100, yInfo, 4);
      tft.setTextPadding(120);
      if(localPWV > 0) tft.drawString(String(localPWV, 1) + " m/s", 180, yInfo, 4);
      else tft.drawString("--- m/s", 180, yInfo, 4);
      tft.setTextPadding(0);

      // HR
      tft.setSwapBytes(true);
      tft.pushImage(288, yInfo - 12, 24, 24, (const uint16_t*)epd_bitmap_ImgCorazon);
      tft.setSwapBytes(false);
      tft.setTextPadding(58);
      if(localBPM > 0) tft.drawString(String(localBPM), 318, yInfo, 4);
      else tft.drawString("---", 325, yInfo, 4);
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
  // TaskSensores se encargará de intentar reconectarlos luego.
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

      // MODO TEST RÁPIDO
      if (x > btnX && x < btnX + btnW && y > btnY1 && y < btnY1 + btnH) {
         sonarPitido(); 
         modoActual = MODO_TEST_RAPIDO;
         edadInput = ""; 
         alturaInput = "";
         dibujarTecladoEdad();
      }
      // MODO ESTUDIO CLÍNICO
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
             
            // Reiniciar lógica y buffers para esperar a la PC
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


    // PANTALLA CONEXIÓN PC -------------------------------------------------------------
    else if (pantallaActual == PANTALLA_PC_ESPERA) {
      // Boton salir
      if (x > 380 && x < 460 && y > 255 && y < 295) {
        sonarPitido();

        // Confirmar salida
        // SALIR
        if (confirmarSalir()) {  
            medicionActiva = false;  // Detiene el envío de datos
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
      
      // Botón VOLVER
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
          
          // RANGO VÁLIDO
          if (val >= 10 && val <= 100) {  
             sonarPitido(); 
             pacienteEdad = val; 
             dibujarTecladoAltura();
             delay(300);
          } 
          
          // RANGO INVÁLIDO
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
             // RANGO VÁLIDO
             sonarPitido(); 
             pacienteAltura = val; 
             pantallaActual = PANTALLA_MEDICION_RAPIDA; 
             dibujarPantallaMedicion();
             medicionActiva = true; 
             portENTER_CRITICAL(&bufferMux); faseMedicion = 0; s1ok = false; s2ok = false; portEXIT_CRITICAL(&bufferMux); 
             delay(300);
          } 
          
          else {
             // RANGO INVÁLIDO
             sonarAlerta();
             mostrarAlertaValorInvalido("ALTURA INVALIDA", "entre 120 y 220 cm");
             dibujarTecladoAltura(); // Redibujar la pantalla para limpiar el popup
          }
       }
    }
    


    // PANTALLA MEDICIÓN ----------------------------------------------------------------
    else if (pantallaActual == PANTALLA_MEDICION_RAPIDA) {
          
          // Cambio de Modo Visualización
          if (y < 45) {
              // Icono Métricas
              if (x > 380 && x < 430) {
                if (modoVisualizacion != 1) {
                    sonarPitido();
                    modoVisualizacion = 1;
                    dibujarPantallaMedicion(); // Redibujar estructura
                    actualizarMedicion();        // Actualizar datos
                    delay(200);
                }
              }
              // Icono Curvas
              else if (x >= 430) {
                if (modoVisualizacion != 0) {
                    sonarPitido();
                    modoVisualizacion = 0;
                    dibujarPantallaMedicion();
                    actualizarMedicion();
                    delay(200);
                }
              }
          }

          // Botón salir
          else if (x > 360 && y > 260) {
              sonarPitido();
              
              // Confirmar salida
              // SALIR
              if (confirmarSalir()) {   
                  medicionActiva = false; 
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

          // Botón volver
          else if (x < 100 && y > 250) {
            sonarPitido();
            medicionActiva = false;
            
            // Volver a corregir altura
            pantallaActual = PANTALLA_ALTURA;
            alturaInput = String(pacienteAltura); // Recordar el dato
            dibujarTecladoAltura();
            delay(300);
          }
    }
  }

  // Actualización contínua del gráfico (si estamos midiendo)
  if (modoActual == MODO_TEST_RAPIDO && medicionActiva && pantallaActual == PANTALLA_MEDICION_RAPIDA) {
    if (millis() - lastDrawTime >= DRAW_INTERVAL) {
      lastDrawTime = millis();
      actualizarMedicion();
    }
  }

}
