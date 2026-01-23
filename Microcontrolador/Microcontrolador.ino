#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include "MAX30105.h"
#include "LogoStiffio.h" // Asegurate de tener este archivo en la pestaña de al lado

// ======================================================================
// CONFIGURACIÓN WIFI (ESTUDIO COMPLETO)
// ======================================================================
const char* ssid = "Gavilan Extender";
const char* password = "a1b1c1d1e1";
WebSocketsServer webSocket(81);
bool wifiConectado = false;

// ======================================================================
// CONFIGURACIÓN HARDWARE
// ======================================================================
#define TOUCH_CS   32
#define TOUCH_IRQ  14
#define BUZZER_PIN 33
#define LCD_LED_PIN 13

#define SDA1 21
#define SCL1 22
#define SDA2 16
#define SCL2 17

// Colores
#define COLOR_FONDO_MEDICION TFT_WHITE
#define COLOR_TEXTO_MEDICION TFT_BLACK
#define COLOR_EJE            TFT_DARKGREY
#define COLOR_GRILLA         0xE71C 
#define COLOR_S1             TFT_RED      
#define COLOR_S2             0xFC9F       

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

// Resultados
volatile int bpmMostrado = 0; 
volatile float pwvMostrado = 0.0; 

// Paciente (Datos que vienen de UI local o de PC)
volatile int pacienteEdad = 0;
volatile int pacienteAltura = 0; // Se llenará desde la PC en modo estudio completo

// MODO DE OPERACIÓN
enum ModoOperacion { MODO_TEST_RAPIDO, MODO_ESTUDIO_COMPLETO };
ModoOperacion modoActual = MODO_TEST_RAPIDO;

portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// ======================================================================
// OBJETOS
// ======================================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite graphSprite = TFT_eSprite(&tft);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

MAX30105 sensorProx;
MAX30105 sensorDist;

// Pantallas UI
enum EstadoPantalla { MENU, PANTALLA_EDAD, PANTALLA_ALTURA, PANTALLA_MEDICION_RAPIDA, PANTALLA_PC_ESPERA };
EstadoPantalla pantallaActual = MENU;

// UI Variables
String edadInput = ""; 
String alturaInput = "";
int btnW = 300; int btnH = 70;
int btnX = (480 - btnW) / 2;
int btnY1 = 85; int btnY2 = 185; 

// Gráfico
#define GRAPH_X 40
#define GRAPH_Y 50
#define GRAPH_W 400 
#define GRAPH_H 180
unsigned long lastDrawTime = 0;
const unsigned long DRAW_INTERVAL = 40; 

// ======================================================================
// FUNCIONES AUXILIARES
// ======================================================================
void sonarPitido() {
  digitalWrite(BUZZER_PIN, HIGH); delay(60); digitalWrite(BUZZER_PIN, LOW);
}

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
void TaskSensores(void *pvParameters) {
  
  // Filtros
  float s1_lp = 0, s1_dc = 0;
  float s2_lp = 0, s2_dc = 0;
  const float ALPHA_LP = 0.7; 
  const float ALPHA_DC = 0.95;
  const long SENSOR_THRESHOLD = 50000; 
  
  // Media Móvil
  const int MA_SIZE = 5; 
  float bufMA1[MA_SIZE] = {0};
  float bufMA2[MA_SIZE] = {0};
  int idxMA = 0;

  // Tiempo
  unsigned long startContactTime = 0; 
  unsigned long baseTime = 0;         
  const unsigned long TIEMPO_ESTABILIZACION = 5000; 

  // Detección PWV/HR
  float lastValS1 = 0; float lastValS2 = 0;
  unsigned long timePeakS1 = 0; 
  bool waitingForS2 = false;    
  unsigned long lastBeatTime = 0; 
  const int REFRACTORY_PERIOD = 250; 
  const float BEAT_THRESHOLD = 15.0; 
  
  // Buffers Promedio
  const int AVG_SIZE = 10;
  int bpmBuffer[AVG_SIZE] = {0}; int bpmIdx = 0; int validSamplesBPM = 0;
  float pwvBuffer[AVG_SIZE] = {0.0}; int pwvIdx = 0; int validSamplesPWV = 0;

  for (;;) {
    if (medicionActiva) {
      
      if (modoActual == MODO_ESTUDIO_COMPLETO) {
        webSocket.loop();
      }

      sensorProx.check();
      sensorDist.check(); 
      
      if (sensorProx.available()) {
        long ir1 = sensorProx.getIR();
        long ir2 = sensorDist.getIR(); 

        bool currentS1 = (ir1 > SENSOR_THRESHOLD);
        bool currentS2 = (ir2 > SENSOR_THRESHOLD);
        s1ok = currentS1;
        s2ok = currentS2;

        if (currentS1 && currentS2) {
            // --- FILTRADO ---
            if (s1_lp == 0) s1_lp = ir1; if (s2_lp == 0) s2_lp = ir2;
            s1_lp = (s1_lp * ALPHA_LP) + (ir1 * (1.0 - ALPHA_LP));
            s2_lp = (s2_lp * ALPHA_LP) + (ir2 * (1.0 - ALPHA_LP));

            if (s1_dc == 0) s1_dc = ir1; if (s2_dc == 0) s2_dc = ir2;
            s1_dc = (s1_dc * ALPHA_DC) + (s1_lp * (1.0 - ALPHA_DC));
            s2_dc = (s2_dc * ALPHA_DC) + (s2_lp * (1.0 - ALPHA_DC));

            float rawVal1 = s1_lp - s1_dc;
            float rawVal2 = s2_lp - s2_dc;

            bufMA1[idxMA] = rawVal1; bufMA2[idxMA] = rawVal2;
            idxMA = (idxMA + 1) % MA_SIZE;

            float sum1 = 0; float sum2 = 0;
            for(int i=0; i<MA_SIZE; i++) { sum1 += bufMA1[i]; sum2 += bufMA2[i]; }
            float valFinal1 = sum1 / MA_SIZE;
            float valFinal2 = sum2 / MA_SIZE;

            // --- LÓGICA DE DETECCIÓN (HR y PWV) ---
            unsigned long now = millis();
            float deltaS1 = valFinal1 - lastValS1;
            float deltaS2 = valFinal2 - lastValS2;
            lastValS1 = valFinal1; lastValS2 = valFinal2;

            if (faseMedicion == 2) {
               // Detección S1 (Cuello)
               if (deltaS1 > BEAT_THRESHOLD && !waitingForS2) {
                   if (now - timePeakS1 > REFRACTORY_PERIOD) {
                       timePeakS1 = now; waitingForS2 = true;
                   }
               }
               // Detección S2 (Muñeca)
               if (deltaS2 > BEAT_THRESHOLD && (now - lastBeatTime > REFRACTORY_PERIOD)) {
                  unsigned long deltaBeat = now - lastBeatTime;
                  lastBeatTime = now;
                  int instantBPM = 60000 / deltaBeat;
                  if (instantBPM > 40 && instantBPM < 200) {
                     bpmBuffer[bpmIdx] = instantBPM; bpmIdx = (bpmIdx + 1) % AVG_SIZE;
                     if (validSamplesBPM < AVG_SIZE) validSamplesBPM++;
                     if (validSamplesBPM >= AVG_SIZE) {
                        long total = 0; for(int i=0; i<AVG_SIZE; i++) total += bpmBuffer[i];
                        bpmMostrado = total / AVG_SIZE;
                     }
                  }
                  // PWV
                  if (waitingForS2) {
                      long transitTime = now - timePeakS1;
                      waitingForS2 = false;
                      // Filtro de tiempo fisiológico (evita dedo-dedo falso o ruido)
                      if (transitTime > 20 && transitTime < 400) {
                          
                          // USAR ALTURA RECIBIDA DE LA PC (O DEL TEST RÁPIDO)
                          int alturaCalc = (pacienteAltura > 0) ? pacienteAltura : 170; // Default 170
                          
                          float distMeters = (alturaCalc * 0.436) / 100.0;
                          float instantPWV = distMeters / (transitTime / 1000.0);
                          
                          if (instantPWV > 3.0 && instantPWV < 50.0) {
                              pwvBuffer[pwvIdx] = instantPWV; pwvIdx = (pwvIdx + 1) % AVG_SIZE;
                              if (validSamplesPWV < AVG_SIZE) validSamplesPWV++;
                              if (validSamplesPWV >= AVG_SIZE) {
                                  float totalPWV = 0; for(int i=0; i<AVG_SIZE; i++) totalPWV += pwvBuffer[i];
                                  pwvMostrado = totalPWV / AVG_SIZE;
                              }
                          }
                      }
                  }
               }
            }

            // --- ACCIÓN SEGÚN MODO ---
            if (modoActual == MODO_TEST_RAPIDO) {
                if (faseMedicion == 2) {
                    unsigned long tiempoRelativo = millis() - baseTime;
                    portENTER_CRITICAL(&bufferMux); 
                    buffer_s1[writeHead] = valFinal1; buffer_s2[writeHead] = valFinal2;
                    buffer_time[writeHead] = tiempoRelativo;
                    writeHead++; if (writeHead >= BUFFER_SIZE) writeHead = 0;
                    portEXIT_CRITICAL(&bufferMux);
                }
            } 
            else if (modoActual == MODO_ESTUDIO_COMPLETO) {
                char json[128];
                // Enviamos señales y resultados calculados
                // 'p'=proximal, 'd'=distal, 'hr'=ritmo, 'pwv'=velocidad
                snprintf(json, sizeof(json), 
                         "{\"s1\":true,\"s2\":true,\"p\":%.2f,\"d\":%.2f,\"hr\":%d,\"pwv\":%.2f}", 
                         valFinal1, valFinal2, bpmMostrado, pwvMostrado);
                webSocket.broadcastTXT(json);
            }

            // Máquina de estados
            if (faseMedicion == 0) {
               faseMedicion = 1; startContactTime = millis();
            } else if (faseMedicion == 1) {
               unsigned long transcurrido = millis() - startContactTime;
               porcentajeEstabilizacion = (transcurrido * 100) / TIEMPO_ESTABILIZACION;
               if (transcurrido >= TIEMPO_ESTABILIZACION) {
                  faseMedicion = 2; baseTime = millis();
                  if(modoActual == MODO_TEST_RAPIDO) {
                      portENTER_CRITICAL(&bufferMux);
                      writeHead = 0;
                      for(int i=0; i<BUFFER_SIZE; i++) {buffer_s1[i]=0; buffer_s2[i]=0; buffer_time[i]=0;}
                      portEXIT_CRITICAL(&bufferMux);
                  }
                  bpmMostrado = 0; pwvMostrado = 0.0;
                  validSamplesBPM = 0; validSamplesPWV = 0;
               }
            }

        } else {
             // Sin dedos
             if(modoActual == MODO_ESTUDIO_COMPLETO) {
                webSocket.broadcastTXT("{\"s1\":false,\"s2\":false}");
             }
             if (faseMedicion != 0) {
                 faseMedicion = 0; porcentajeEstabilizacion = 0;
                 s1_lp=0; s1_dc=0; s2_lp=0; s2_dc=0;
                 bpmMostrado = 0; pwvMostrado = 0.0;
                 sensorProx.nextSample(); sensorDist.nextSample();
             }
        }
      }
    } else {
      vTaskDelay(10);
      if (modoActual == MODO_ESTUDIO_COMPLETO) webSocket.loop(); // Mantener vivo el socket aunque no mida
    }
    vTaskDelay(1);
  }
}

// ======================================================================
// WIFI
// ======================================================================
void iniciarWiFi() {
  // --- PASO 1: LIMPIEZA AGRESIVA ---
  // Esto desconecta cualquier intento anterior y borra la config guardada
  WiFi.disconnect(true);  
  WiFi.mode(WIFI_OFF);    
  delay(500); // Dale un respiro
  WiFi.mode(WIFI_STA);    
  
  // --- PASO 2: INTERFAZ ---
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE);
  tft.drawString("Conectando WiFi...", 240, 140, 4);
  tft.drawString(ssid, 240, 180, 2);

  // --- PASO 3: CONEXIÓN ---
  WiFi.begin(ssid, password);
  
  int intentos = 0;
  // Aumentamos a 60 intentos (30 segundos) como hablamos
  while (WiFi.status() != WL_CONNECTED && intentos < 60) { 
    delay(500);
    tft.drawString(".", 240 + ((intentos%20)*10), 220, 4); // Efecto visual para que no se salga de pantalla
    
    // Debug por puerto serie para que veas qué pasa
    Serial.print(".");
    if (intentos % 10 == 0) Serial.println(""); 
    
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
     wifiConectado = true;
     
     // Mostrar IP en pantalla grande para que la veas bien
     tft.fillScreen(TFT_BLACK);
     tft.setTextColor(TFT_GREEN);
     tft.drawString("CONECTADO!", 240, 120, 4);
     tft.setTextColor(TFT_WHITE);
     tft.drawString("IP: " + WiFi.localIP().toString(), 240, 160, 4);
     Serial.println("");
     Serial.println("WiFi conectado.");
     Serial.println("IP address: ");
     Serial.println(WiFi.localIP());
     
     webSocket.begin();
     webSocket.onEvent(webSocketEvent);
     delay(3000); // Te da 3 segs para leer la IP antes de cambiar de pantalla
  } else {
     tft.fillScreen(TFT_RED);
     tft.setTextColor(TFT_WHITE);
     tft.drawString("ERROR WIFI", 240, 140, 4);
     tft.drawString("Revise contrasena", 240, 180, 2);
     Serial.println("\nFallo conexion WiFi");
     delay(2000);
     wifiConectado = false;
  }
}

// ======================================================================
// INTERFAZ GRÁFICA
// ======================================================================
void dibujarBotonVolver() {
  int circX = 50, circY = 275, r = 22;
  tft.fillCircle(circX, circY, r, TFT_WHITE);
  tft.fillTriangle(circX-11, circY, circX+2, circY-7, circX+2, circY+7, TFT_BLACK);
  tft.fillRect(circX+1, circY-2, 9, 5, TFT_BLACK); 
}

void dibujarBotonSiguiente() {
  int circX = 430, circY = 275, r = 22;
  tft.fillCircle(circX, circY, r, tft.color565(0, 180, 0));
  tft.fillTriangle(circX+11, circY, circX-2, circY-7, circX-2, circY+7, TFT_WHITE);
  tft.fillRect(circX-10, circY-2, 9, 5, TFT_WHITE);
}

void actualizarDisplayEdad() {
  tft.fillRect(190, 60, 100, 35, tft.color565(20, 20, 20));
  tft.drawRoundRect(190, 60, 100, 35, 4, TFT_WHITE);
  tft.setTextColor(TFT_CYAN); tft.drawString(edadInput, 240, 77, 4);
}

void dibujarTecladoEdad() {
  pantallaActual = PANTALLA_EDAD;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE);
  tft.drawString("INGRESE EDAD", 240, 30, 4);
  actualizarDisplayEdad();
  
  int tstartX = 180; int tstartY = 140; int tgapX = 55; int tgapY = 45; 
  for (int i = 0; i < 11; i++) {
    int row = i / 3; int col = i % 3;
    if (i == 9) { col = 1; row = 3; } if (i == 10) { col = 2; row = 3; }
    int kX = tstartX + (col * tgapX); int kY = tstartY + (row * tgapY);
    uint16_t colorBtn = (i == 10) ? tft.color565(180, 0, 0) : tft.color565(40, 40, 40);
    tft.fillRoundRect(kX - 22, kY - 17, 45, 35, 4, colorBtn);
    tft.drawRoundRect(kX - 22, kY - 17, 45, 35, 4, TFT_LIGHTGREY);
    tft.setTextColor(TFT_WHITE);
    if (i < 9) tft.drawNumber(i + 1, kX, kY, 4);
    else if (i == 9) tft.drawNumber(0, kX, kY, 4);
    else tft.drawString("C", kX, kY, 4);
  }
  dibujarBotonVolver();
  if (edadInput.length() > 0) dibujarBotonSiguiente();
}

void actualizarDisplayAltura() {
  tft.fillRect(190, 60, 100, 35, tft.color565(20, 20, 20));
  tft.drawRoundRect(190, 60, 100, 35, 4, TFT_WHITE);
  tft.setTextColor(TFT_ORANGE); tft.drawString(alturaInput, 240, 77, 4);
}

void dibujarTecladoAltura() {
  pantallaActual = PANTALLA_ALTURA;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE);
  tft.drawString("ALTURA (cm)", 240, 30, 4);
  actualizarDisplayAltura();
  
  int tstartX = 180; int tstartY = 140; int tgapX = 55; int tgapY = 45; 
  for (int i = 0; i < 11; i++) {
    int row = i / 3; int col = i % 3;
    if (i == 9) { col = 1; row = 3; } if (i == 10) { col = 2; row = 3; }
    int kX = tstartX + (col * tgapX); int kY = tstartY + (row * tgapY);
    uint16_t colorBtn = (i == 10) ? tft.color565(180, 0, 0) : tft.color565(40, 40, 40);
    tft.fillRoundRect(kX - 22, kY - 17, 45, 35, 4, colorBtn);
    tft.drawRoundRect(kX - 22, kY - 17, 45, 35, 4, TFT_LIGHTGREY);
    tft.setTextColor(TFT_WHITE);
    if (i < 9) tft.drawNumber(i + 1, kX, kY, 4);
    else if (i == 9) tft.drawNumber(0, kX, kY, 4);
    else tft.drawString("C", kX, kY, 4);
  }
  dibujarBotonVolver();
  if (alturaInput.length() > 0) dibujarBotonSiguiente();
}

void dibujarMenuPrincipal() {
  pantallaActual = MENU;
  tft.fillScreen(TFT_BLACK); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("STIFFIO", 240, 40, 4);
  
  tft.fillRoundRect(btnX, btnY1, btnW, btnH, 12, TFT_RED);
  tft.drawRoundRect(btnX, btnY1, btnW, btnH, 12, TFT_WHITE);
  tft.drawString("TEST RAPIDO", 240, btnY1 + 35, 4);
  
  tft.fillRoundRect(btnX, btnY2, btnW, btnH, 12, tft.color565(0, 100, 200)); 
  tft.drawRoundRect(btnX, btnY2, btnW, btnH, 12, TFT_WHITE);
  tft.drawString("ESTUDIO COMPLETO", 240, btnY2 + 35, 4);
}

void prepararPantallaPC() {
  tft.fillScreen(tft.color565(0, 50, 100)); // Fondo azulado
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  
  tft.drawString("MODO ESTUDIO COMPLETO", 240, 40, 4);
  tft.drawString("Conectado a WiFi:", 240, 100, 2);
  tft.setTextColor(TFT_GREEN);
  tft.drawString(WiFi.localIP().toString(), 240, 130, 4);
  
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Esperando datos desde PC...", 240, 200, 4);
  
  tft.fillRoundRect(380, 270, 90, 45, 8, TFT_RED);
  tft.drawString("SALIR", 425, 292, 2);
}

void prepararPantallaMedicion() {
  tft.fillScreen(COLOR_FONDO_MEDICION);
  tft.fillRect(0, 0, 480, 40, TFT_BLACK);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(TFT_WHITE);
  tft.drawString("Monitor PWV", 10, 20, 4);
  tft.fillRoundRect(380, 270, 90, 45, 8, TFT_RED);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE);
  tft.drawString("VOLVER", 425, 292, 2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_S1); tft.drawString("S1: Cuello", 40, 270, 2);
  tft.setTextColor(COLOR_S2); tft.drawString("S2: Muneca", 40, 290, 2); 
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_W + 2, GRAPH_H + 2, TFT_BLACK);
}

void actualizarGrafico() {
  graphSprite.fillSprite(COLOR_FONDO_MEDICION);

  float localBuf1[BUFFER_SIZE]; float localBuf2[BUFFER_SIZE];
  unsigned long localTime[BUFFER_SIZE]; int localHead;
  int localFase = faseMedicion; int localPorcentaje = porcentajeEstabilizacion;
  int localBPM = bpmMostrado; float localPWV = pwvMostrado;

  portENTER_CRITICAL(&bufferMux);
  if (localFase == 2) {
    memcpy(localBuf1, (const void*)buffer_s1, sizeof(buffer_s1));
    memcpy(localBuf2, (const void*)buffer_s2, sizeof(buffer_s2));
    memcpy(localTime, (const void*)buffer_time, sizeof(buffer_time));
    localHead = writeHead;
  }
  portEXIT_CRITICAL(&bufferMux);

  if (localFase == 0) {
    graphSprite.setTextDatum(MC_DATUM);
    if (!s1ok && !s2ok) {
       graphSprite.setTextColor(TFT_BLACK); graphSprite.drawString("COLOCAR AMBOS SENSORES", GRAPH_W/2, GRAPH_H/2, 4);
    } else {
       graphSprite.setTextColor(TFT_BLACK); graphSprite.drawString("DETECTANDO...", GRAPH_W/2, GRAPH_H/2, 4);
    }
    graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
    return;
  }
  
  if (localFase == 1) {
    graphSprite.setTextDatum(MC_DATUM); graphSprite.setTextColor(TFT_BLUE);
    graphSprite.drawString("CALIBRANDO (10s)...", GRAPH_W/2, GRAPH_H/2 - 20, 4);
    int barW = 200; int barH = 20; int barX = (GRAPH_W - barW)/2; int barY = GRAPH_H/2 + 10;
    graphSprite.drawRect(barX, barY, barW, barH, TFT_BLACK);
    int fillW = (barW * localPorcentaje) / 100;
    graphSprite.fillRect(barX+1, barY+1, fillW-2, barH-2, TFT_GREEN);
    graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
    return;
  }

  // Resultados
  graphSprite.setTextDatum(TR_DATUM);
  if (localBPM > 0) {
    graphSprite.setTextColor(TFT_BLACK); graphSprite.drawString("HR: " + String(localBPM), GRAPH_W - 10, 10, 4);
  } else {
    graphSprite.setTextColor(TFT_LIGHTGREY); graphSprite.drawString("HR: --", GRAPH_W - 10, 10, 4);
  }
  if (localPWV > 0) {
    graphSprite.setTextColor(TFT_BLUE); graphSprite.drawString("PWV: " + String(localPWV, 1) + " m/s", GRAPH_W - 10, 40, 4);
  } else {
    graphSprite.setTextColor(TFT_LIGHTGREY); graphSprite.drawString("PWV: --", GRAPH_W - 10, 40, 4);
  }

  // Gráfico
  graphSprite.drawFastHLine(0, GRAPH_H/2, GRAPH_W, TFT_LIGHTGREY); 
  float minV = -50, maxV = 50;
  for(int i=0; i<BUFFER_SIZE; i+=5) { 
    if(localBuf1[i] < minV) minV = localBuf1[i]; if(localBuf1[i] > maxV) maxV = localBuf1[i];
  }
  if((maxV - minV) < 100) { float mid = (maxV + minV) / 2; maxV = mid + 50; minV = mid - 50; }
  float xStep = (float)GRAPH_W / (float)BUFFER_SIZE; 
  graphSprite.setTextDatum(TC_DATUM); graphSprite.setTextColor(COLOR_EJE);

  for (int i = 0; i < BUFFER_SIZE - 1; i++) {
    int idx = (localHead + i) % BUFFER_SIZE; int nextIdx = (localHead + i + 1) % BUFFER_SIZE;
    if (localTime[idx] == 0 && localTime[nextIdx] == 0) continue;
    int x1 = (int)(i * xStep); int x2 = (int)((i + 1) * xStep);
    unsigned long tCurrent = localTime[idx]; unsigned long tNext = localTime[nextIdx];
    if (tNext > tCurrent) {
        unsigned long secCurrent = tCurrent / 1000; unsigned long secNext = tNext / 1000;
        if (secNext > secCurrent) { 
            graphSprite.drawFastVLine(x2, 0, GRAPH_H, COLOR_GRILLA); 
            graphSprite.drawString(String(secNext)+"s", x2, GRAPH_H - 20, 2); 
        }
    }
    int y1A = map((long)localBuf1[idx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y1B = map((long)localBuf1[nextIdx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y2A = map((long)localBuf2[idx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y2B = map((long)localBuf2[nextIdx], (long)minV, (long)maxV, GRAPH_H, 0);
    graphSprite.drawLine(x1, constrain(y1A,0,GRAPH_H), x2, constrain(y1B,0,GRAPH_H), COLOR_S1);
    graphSprite.drawLine(x1, constrain(y2A,0,GRAPH_H), x2, constrain(y2B,0,GRAPH_H), COLOR_S2); 
  }
  graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
}

// ======================================================================
// MAIN SETUP & LOOP
// ======================================================================
bool iniciarSensores() {
  Wire.begin(SDA1, SCL1, 400000); Wire1.begin(SDA2, SCL2, 400000);
  Wire.setClock(400000); Wire1.setClock(400000);
  if (!sensorProx.begin(Wire, I2C_SPEED_FAST)) return false;
  if (!sensorDist.begin(Wire1, I2C_SPEED_FAST)) return false;
  sensorProx.setup(20, 8, 2, 400, 411, 4096);
  sensorDist.setup(30, 8, 2, 400, 411, 4096);
  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT); pinMode(LCD_LED_PIN, OUTPUT); digitalWrite(LCD_LED_PIN, HIGH);
  tft.init(); tft.setRotation(1); tft.setSwapBytes(true);
  
  // --- MOSTRAR LOGO AL INICIO ---
  tft.fillScreen(TFT_BLACK);
  // Asumiendo que el logo es 190x159 (ajusta si es otro tamaño)
  int imgW = 190, imgH = 159;
  if(epd_bitmap_Logo_invertido) { 
      tft.pushImage((480-imgW)/2, (320-imgH)/2 - 20, imgW, imgH, epd_bitmap_Logo_invertido);
  } else {
      tft.drawString("STIFFIO", 240, 160, 4);
  }
  delay(3000); // Esperar 3 segundos viendo el logo

  graphSprite.setColorDepth(8); graphSprite.createSprite(GRAPH_W, GRAPH_H);
  touch.begin(); touch.setRotation(1);

  if (!iniciarSensores()) {
     tft.fillScreen(TFT_RED); tft.drawString("ERROR I2C", 240, 160, 4); while(1); 
  }

  xTaskCreatePinnedToCore(TaskSensores,"SensorTask",10000,NULL,1,NULL,0);
  dibujarMenuPrincipal();
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    int x = map(p.x, 200, 3700, 480, 0); int y = map(p.y, 200, 3800, 320, 0);

    if (pantallaActual == MENU) {
      // 1. MODO TEST RÁPIDO
      if (x > btnX && x < btnX + btnW && y > btnY1 && y < btnY1 + btnH) {
         sonarPitido(); 
         modoActual = MODO_TEST_RAPIDO;
         edadInput = ""; 
         dibujarTecladoEdad();
      }
      // 2. MODO ESTUDIO COMPLETO
      else if (x > btnX && x < btnX + btnW && y > btnY2 && y < btnY2 + btnH) {
         sonarPitido();
         modoActual = MODO_ESTUDIO_COMPLETO;
         if (!wifiConectado) iniciarWiFi(); 
         
         if (wifiConectado) {
             pantallaActual = PANTALLA_PC_ESPERA;
             prepararPantallaPC();
             
             // Reiniciar lógica y buffers para esperar a la PC
             portENTER_CRITICAL(&bufferMux);
             faseMedicion = 0; s1ok=false; s2ok=false;
             pacienteAltura = 0; // Reset altura, la PC debe mandarla
             portEXIT_CRITICAL(&bufferMux);
             
             medicionActiva = true; 
         } else {
             dibujarMenuPrincipal(); // Volver si falla wifi
         }
      }
    }
    // ... Lógica Teclados Test Rápido ...
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
            actualizarDisplayEdad();
            if (edadInput.length() > 0) dibujarBotonSiguiente();
            delay(250);
         }
       }
       if ((x-50)*(x-50)+(y-275)*(y-275) <= 900) { 
          sonarPitido(); dibujarMenuPrincipal(); delay(300); 
       }
       if (edadInput.length() > 0 && (x-430)*(x-430)+(y-275)*(y-275) <= 900) {
          int val = edadInput.toInt();
          if (val >= 15 && val <= 99) {
             sonarPitido(); pacienteEdad = val; alturaInput = ""; dibujarTecladoAltura(); delay(300);
          }
       }
    }
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
            if (alturaInput.length() > 0) dibujarBotonSiguiente();
            delay(250);
         }
       }
       if ((x-50)*(x-50)+(y-275)*(y-275) <= 900) { 
          sonarPitido(); dibujarTecladoEdad(); delay(300); 
       }
       if (alturaInput.length() > 0 && (x-430)*(x-430)+(y-275)*(y-275) <= 900) {
          int val = alturaInput.toInt();
          if (val >= 50 && val <= 250) {
             sonarPitido(); pacienteAltura = val; 
             pantallaActual = PANTALLA_MEDICION_RAPIDA; prepararPantallaMedicion();
             medicionActiva = true; portENTER_CRITICAL(&bufferMux); faseMedicion = 0; s1ok = false; s2ok = false; portEXIT_CRITICAL(&bufferMux); delay(300);
          }
       }
    }
    
    // ... Pantalla PC Espera y Medicion ...
    else if (pantallaActual == PANTALLA_PC_ESPERA) {
       if (x > 380 && y > 260) {
           medicionActiva = false;
           dibujarMenuPrincipal();
           delay(300);
       }
    }
    else if (pantallaActual == PANTALLA_MEDICION_RAPIDA) {
      if (x > 380 && y > 260) {
        medicionActiva = false; 
        pantallaActual = MENU;
        dibujarMenuPrincipal();
        delay(300);
      }
    }
  }

  if (modoActual == MODO_TEST_RAPIDO && medicionActiva && pantallaActual == PANTALLA_MEDICION_RAPIDA) {
    if (millis() - lastDrawTime >= DRAW_INTERVAL) {
      lastDrawTime = millis();
      actualizarGrafico();
    }
  }
}