#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Wire.h>
#include "MAX30105.h"

#include "LogoStiffio.h" 

// ======================================================================
// CONFIGURACIÓN HARDWARE
// ======================================================================
#define TOUCH_CS   32
#define TOUCH_IRQ  14
#define BUZZER_PIN 33
#define LCD_LED_PIN 13

// I2C
#define SDA1 21
#define SCL1 22
#define SDA2 16
#define SCL2 17

// ======================================================================
// COLORES PERSONALIZADOS (MODO BLANCO)
// ======================================================================
#define COLOR_FONDO_MEDICION TFT_WHITE
#define COLOR_TEXTO_MEDICION TFT_BLACK
#define COLOR_EJE            TFT_DARKGREY
#define COLOR_GRILLA         0xE71C // Gris muy claro
#define COLOR_S1             TFT_RED      // Proximal (Cuello)
#define COLOR_PINK           0xFC9F       // Rosado personalizado (RGB565)
#define COLOR_S2             COLOR_PINK   // Distal (Muñeca)

// ======================================================================
// VARIABLES COMPARTIDAS (MULTICORE)
// ======================================================================
// El buffer ahora es 'volatile' porque se comparte entre núcleos
#define BUFFER_SIZE 320 // Ajustado al ancho del gráfico
volatile float buffer_s1[BUFFER_SIZE];
volatile float buffer_s2[BUFFER_SIZE];
volatile unsigned long buffer_time[BUFFER_SIZE]; 
volatile int writeHead = 0; // Índice de escritura del sensor

// Control de flujo
volatile bool medicionActiva = false;
// Estados: 0=Esperando, 1=Estabilizando, 2=Midiendo
volatile int faseMedicion = 0; 
volatile int porcentajeEstabilizacion = 0;

// Variables de estado compartidas
volatile bool s1ok = false;
volatile bool s2ok = false;

// --- VARIABLES RESULTADOS (BPM y PWV) ---
volatile int bpmMostrado = 0; 
volatile float pwvMostrado = 0.0; 

// Variables de Paciente (Ingresadas en UI)
int pacienteEdad = 0;
int pacienteAltura = 0; // en cm

// Semáforo para proteger la memoria (evita que se lea mientras se escribe)
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// ======================================================================
// VARIABLES GLOBALES / OBJETOS
// ======================================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite graphSprite = TFT_eSprite(&tft); // Sprite para gráficos sin parpadeo
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

MAX30105 sensorProx;
MAX30105 sensorDist;

// Estados de Pantalla (Fusionados)
enum Estado { MENU, PANTALLA_EDAD, PANTALLA_ALTURA, PANTALLA_RESUMEN, MEDICION };
Estado pantallaActual = MENU;

// Variables de Input UI
String edadInput = ""; 
String alturaInput = "";

// Diseño UI
int tstartX = 180; int tstartY = 140; 
int tgapX = 55; int tgapY = 45; 
int btnW = 300; int btnH = 70;
int btnX = (480 - btnW) / 2;
int btnY1 = 85; int btnY2 = 185; 

// Coordenadas Gráfico
#define GRAPH_X 40
#define GRAPH_Y 50
#define GRAPH_W 400
#define GRAPH_H 180

// Tiempos de Refresco Visual (El muestreo va aparte)
unsigned long lastDrawTime = 0;
const unsigned long DRAW_INTERVAL = 40; // ~25fps visuales

// ======================================================================
// FUNCIONES AUXILIARES SONIDO
// ======================================================================
void sonarPitido() {
  digitalWrite(BUZZER_PIN, HIGH); delay(60); digitalWrite(BUZZER_PIN, LOW);
}

// ======================================================================
// TAREA NÚCLEO 0: TAREA DE SENSORES (38 FPS + CÁLCULO BPM)
// ======================================================================
void TaskSensores(void *pvParameters) {

  // VARIABLES DE FILTRADO (Persistentes)
  float s1_lp = 0, s1_dc = 0;
  float s2_lp = 0, s2_dc = 0;
  const float ALPHA_LP = 0.7; // Factor de suavizado (0.7 mantiene la forma pero quita ruido fino)
  const float ALPHA_DC = 0.95; // Factor de filtro DC (Centrado en 0)
  const long SENSOR_THRESHOLD = 50000;

  // Buffers Media Móvil
  const int MA_SIZE = 5; 
  float bufMA1[MA_SIZE] = {0};
  float bufMA2[MA_SIZE] = {0};
  int idxMA = 0;

  // Variables Tiempo
  unsigned long startContactTime = 0; 
  unsigned long baseTime = 0;         
  const unsigned long TIEMPO_ESTABILIZACION = 10000; // 10 Segundos de calibración

  // Variables HR y PWV
  float lastValS1 = 0;
  float lastValS2 = 0;
  
  unsigned long timePeakS1 = 0; 
  bool waitingForS2 = false;    
  
  unsigned long lastBeatTime = 0;
  const int REFRACTORY_PERIOD = 250; // ms (Mínimo tiempo entre latidos)
  const float BEAT_THRESHOLD = 15.0; // Sensibilidad de pendiente
  
  // Promedio de 10 muestras (Máxima estabilidad)
  const int AVG_SIZE = 10;
  int bpmBuffer[AVG_SIZE] = {0};
  int bpmIdx = 0;
  int validSamples = 0;

  float pwvBuffer[AVG_SIZE] = {0.0};
  int pwvIdx = 0;
  int validSamplesPWV = 0;

  // Loop infinito de alta velocidad
  for (;;) {
    if (medicionActiva) {
      
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
          // --- FILTRADO (SIN INVERSIÓN) ---
          // Inicializar si es el primer dato
          if (s1_lp == 0) s1_lp = ir1; 
          if (s2_lp == 0) s2_lp = ir2;
          // Filtro Paso Bajo (Low Pass) - Suaviza "dientes"
          s1_lp = (s1_lp * ALPHA_LP) + (ir1 * (1.0 - ALPHA_LP));
          s2_lp = (s2_lp * ALPHA_LP) + (ir2 * (1.0 - ALPHA_LP));

          // Filtro DC (High Pass) - Centra en 0
          if (s1_dc == 0) s1_dc = ir1;
          if (s2_dc == 0) s2_dc = ir2;
          s1_dc = (s1_dc * ALPHA_DC) + (s1_lp * (1.0 - ALPHA_DC));
          s2_dc = (s2_dc * ALPHA_DC) + (s2_lp * (1.0 - ALPHA_DC));

          // Resultado FINAL (ORIENTACIÓN ORIGINAL)
          //    Formula: Val = SeñalSuavizada - DC.
          //    Cuando haya un latido, la señal bajará, así que el valor será negativo.
          float rawVal1 = s1_lp - s1_dc;
          float rawVal2 = s2_lp - s2_dc;

          // Media Móvil, suavizado
          bufMA1[idxMA] = rawVal1;
          bufMA2[idxMA] = rawVal2;
          idxMA = (idxMA + 1) % MA_SIZE;

          float sum1 = 0, sum2 = 0;
          for(int i=0; i<MA_SIZE; i++) { sum1 += bufMA1[i]; sum2 += bufMA2[i]; }
          
          float valFinal1 = sum1 / MA_SIZE;
          float valFinal2 = sum2 / MA_SIZE;

          // --------------------------
          // CÁLCULO BPM (HR)
          // --------------------------
          // Usamos Sensor 2 (Muñeca/Rosa) y criterio de pendiente
          unsigned long now = millis();
          float deltaS1 = valFinal1 - lastValS1;
          float deltaS2 = valFinal2 - lastValS2;
          lastValS1 = valFinal1;
          lastValS2 = valFinal2;

          if (faseMedicion == 2) {
             
             // A) Detectar subida en S1 (CUELLO)
             if (deltaS1 > BEAT_THRESHOLD && !waitingForS2) {
                 if (now - timePeakS1 > REFRACTORY_PERIOD) {
                     timePeakS1 = now;
                     waitingForS2 = true; // Cronómetro iniciado
                 }
             }

             // B) Detectar subida en S2 (DEDO)
             if (deltaS2 > BEAT_THRESHOLD && (now - lastBeatTime > REFRACTORY_PERIOD)) {
                
                // --- CALCULAR HR ---
                unsigned long deltaBeat = now - lastBeatTime;
                lastBeatTime = now;
                int instantBPM = 60000 / deltaBeat;

                if (instantBPM > 40 && instantBPM < 200) {
                   bpmBuffer[bpmIdx] = instantBPM;
                   bpmIdx = (bpmIdx + 1) % AVG_SIZE;
                   if (validSamplesBPM < AVG_SIZE) validSamplesBPM++;
                   
                   if (validSamplesBPM >= AVG_SIZE) {
                      long total = 0;
                      for(int i=0; i<AVG_SIZE; i++) total += bpmBuffer[i];
                      bpmMostrado = total / AVG_SIZE;
                   }
                }

                // --- CALCULAR PWV ---
                if (waitingForS2) {
                    long transitTime = now - timePeakS1; // Delta T (ms)
                    waitingForS2 = false; 
                    
                    // IMPORTANTE: Filtramos tiempos absurdos.
                    // Si es < 20ms, probablemente sea ruido o medición "dedo-dedo".
                    // Un tránsito real cuello-dedo suele ser > 60ms.
                    if (transitTime > 20 && transitTime < 400) {
                        
                        // FÓRMULA CORREGIDA: 0.436 * Altura
                        // pacienteAltura está en cm, pasamos a metros dividiendo por 100
                        float distMeters = (pacienteAltura * 0.436) / 100.0;
                        
                        // PWV = Metros / Segundos
                        float instantPWV = distMeters / (transitTime / 1000.0);
                        
                        // Filtro de realismo (3 m/s a 30 m/s es lo humano posible)
                        if (instantPWV > 3.0 && instantPWV < 30.0) {
                            pwvBuffer[pwvIdx] = instantPWV;
                            pwvIdx = (pwvIdx + 1) % AVG_SIZE;
                            if (validSamplesPWV < AVG_SIZE) validSamplesPWV++;
                            
                            if (validSamplesPWV >= AVG_SIZE) {
                                float totalPWV = 0;
                                for(int i=0; i<AVG_SIZE; i++) totalPWV += pwvBuffer[i];
                                pwvMostrado = totalPWV / AVG_SIZE;
                            }
                        }
                    }
                }
             }
          }

          // --- MAQUINA DE ESTADOS ---

          if (faseMedicion == 0) {
             faseMedicion = 1; 
             startContactTime = millis();
          }
          else if (faseMedicion == 1) { 
             unsigned long transcurrido = millis() - startContactTime;
             porcentajeEstabilizacion = (transcurrido * 100) / TIEMPO_ESTABILIZACION;
             if (transcurrido >= TIEMPO_ESTABILIZACION) {
                faseMedicion = 2; baseTime = millis(); 
                portENTER_CRITICAL(&bufferMux);
                writeHead = 0;
                for(int i=0; i<BUFFER_SIZE; i++) { buffer_s1[i]=0; buffer_s2[i]=0; buffer_time[i]=0; }
                bpmMostrado = 0; pwvMostrado = 0.0;
                validSamplesBPM = 0; validSamplesPWV = 0;
                portEXIT_CRITICAL(&bufferMux);
             }
          }
          else if (faseMedicion == 2) {
             unsigned long tiempoRelativo = millis() - baseTime;

             portENTER_CRITICAL(&bufferMux); 
             buffer_s1[writeHead] = valFinal1;
             buffer_s2[writeHead] = valFinal2;
             buffer_time[writeHead] = tiempoRelativo;
             writeHead++;
             if (writeHead >= BUFFER_SIZE) writeHead = 0;
             portEXIT_CRITICAL(&bufferMux);
          }

        } else {
          // Se levantó el dedo, reset
          if (faseMedicion != 0) {
             faseMedicion = 0;
             porcentajeEstabilizacion = 0;
             s1_lp = 0; s1_dc = 0;
             s2_lp = 0; s2_dc = 0;
             sensorProx.nextSample(); sensorDist.nextSample();
             bpmMostrado = 0; pwvMostrado = 0.0;
             validSamplesBPM = 0; validSamplesPWV = 0;
          }
        }
      }
    } else {
      vTaskDelay(10); 
    }
    vTaskDelay(1); 
  }  
}

// ======================================================================
// FUNCIONES
// ======================================================================
bool iniciarSensores() {
  Wire.begin(SDA1, SCL1, 400000);
  Wire1.begin(SDA2, SCL2, 400000);
  Wire.setClock(400000); Wire1.setClock(400000);

  if (!sensorProx.begin(Wire, I2C_SPEED_FAST)) return false;
  if (!sensorDist.begin(Wire1, I2C_SPEED_FAST)) return false;

  // CONFIGURACIÓN SUAVIZADA (SampleAvg 8)
  // Al promediar 8 muestras hardware, la señal sale mucho más limpia.
  // para que no sature y tenga buena definición.
  sensorProx.setup(20, 8, 2, 400, 411, 4096); // Antes 60, luego 40, luego 30 (Igual al otro)
  sensorDist.setup(30, 8, 2, 400, 411, 4096); // Se queda en 30 (El "Golden Standard")
  
  return true;
}

// ======================================================================
// INTERFAZ GRÁFICA (UI)
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
  // Mismo loop para dibujar teclado...
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

void mostrarAlerta(String titulo, String mensaje) {
  tft.fillRoundRect(110, 100, 260, 120, 10, tft.color565(180, 0, 0));
  tft.drawRoundRect(110, 100, 260, 120, 10, TFT_WHITE);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE); 
  tft.drawString(titulo, 240, 140, 4);
  tft.drawString(mensaje, 240, 180, 2);
  sonarPitido(); delay(1500); 
  if (pantallaActual == PANTALLA_EDAD) dibujarTecladoEdad();
  else if (pantallaActual == PANTALLA_ALTURA) dibujarTecladoAltura();
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
  
  tft.fillRoundRect(btnX, btnY2, btnW, btnH, 12, tft.color565(50,50,50));
  tft.drawRoundRect(btnX, btnY2, btnW, btnH, 12, TFT_WHITE);
  tft.drawString("ESTUDIO COMPLETO", 240, btnY2 + 35, 4);
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

// ======================================================================
// ACTUALIZAR GRÁFICO (ADAPTADO A MULTICORE)
// ======================================================================
void actualizarGrafico() {

  graphSprite.fillSprite(COLOR_FONDO_MEDICION);
  // COPIA SEGURA DE DATOS (SNAPSHOT)
  // Copiamos los datos del núcleo 0 a variables locales para dibujar tranquilos
  float localBuf1[BUFFER_SIZE];
  float localBuf2[BUFFER_SIZE];
  unsigned long localTime[BUFFER_SIZE];
  int localHead;
  int localFase = faseMedicion;
  int localPorcentaje = porcentajeEstabilizacion;
  int localBPM = bpmMostrado;
  float localPWV = pwvMostrado;

  portENTER_CRITICAL(&bufferMux);
  if (localFase == 2) {
    memcpy(localBuf1, (const void*)buffer_s1, sizeof(buffer_s1));
    memcpy(localBuf2, (const void*)buffer_s2, sizeof(buffer_s2));
    memcpy(localTime, (const void*)buffer_time, sizeof(buffer_time));
    localHead = writeHead;
  }
  portEXIT_CRITICAL(&bufferMux);

  // --- UI LÓGICA ---
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
    graphSprite.setTextDatum(MC_DATUM);
    graphSprite.setTextColor(TFT_BLUE);
    graphSprite.drawString("CALIBRANDO (10s)...", GRAPH_W/2, GRAPH_H/2 - 20, 4);
    int barW = 200; int barH = 20; int barX = (GRAPH_W - barW)/2; int barY = GRAPH_H/2 + 10;
    graphSprite.drawRect(barX, barY, barW, barH, TFT_BLACK);
    int fillW = (barW * localPorcentaje) / 100;
    graphSprite.fillRect(barX+1, barY+1, fillW-2, barH-2, TFT_GREEN);
    graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
    return;
  }

  // --- RESULTADOS EN PANTALLA ---
  graphSprite.setTextDatum(TR_DATUM);

  // BPM
  if (localBPM > 0) {
    graphSprite.setTextColor(TFT_BLACK);
    graphSprite.drawString("HR: " + String(localBPM), GRAPH_W - 10, 10, 4);
  } else {
    graphSprite.setTextColor(TFT_LIGHTGREY);
    graphSprite.drawString("HR: --", GRAPH_W - 10, 10, 4);
  }

  // PWV
  if (localPWV > 0) {
    graphSprite.setTextColor(TFT_BLUE);
    // Mostramos 1 decimal
    graphSprite.drawString("PWV: " + String(localPWV, 1) + " m/s", GRAPH_W - 10, 40, 4);
  } else {
    graphSprite.setTextColor(TFT_LIGHTGREY);
    graphSprite.drawString("PWV: --", GRAPH_W - 10, 40, 4);
  }

  // GRÁFICO
  graphSprite.drawFastHLine(0, GRAPH_H/2, GRAPH_W, TFT_LIGHTGREY); 

  float minV = -50, maxV = 50;
  for(int i=0; i<BUFFER_SIZE; i+=5) { 
    if(localBuf1[i] < minV) minV = localBuf1[i]; if(localBuf1[i] > maxV) maxV = localBuf1[i];
  }
  if((maxV - minV) < 100) { float mid = (maxV + minV) / 2; maxV = mid + 50; minV = mid - 50; }
  
  float xStep = (float)GRAPH_W / (float)BUFFER_SIZE; 
  graphSprite.setTextDatum(TC_DATUM); 
  graphSprite.setTextColor(COLOR_EJE);

  for (int i = 0; i < BUFFER_SIZE - 1; i++) {
    int idx = (localHead + i) % BUFFER_SIZE;
    int nextIdx = (localHead + i + 1) % BUFFER_SIZE;

    if (localTime[idx] == 0 && localTime[nextIdx] == 0) continue;

    int x1 = (int)(i * xStep);
    int x2 = (int)((i + 1) * xStep);

    unsigned long tCurrent = localTime[idx];
    unsigned long tNext = localTime[nextIdx];
    
    if (tNext > tCurrent) {
        unsigned long secCurrent = tCurrent / 1000;
        unsigned long secNext = tNext / 1000;
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
// MAIN SETUP
// ======================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LCD_LED_PIN, OUTPUT);
  digitalWrite(LCD_LED_PIN, HIGH);

  tft.init(); tft.setRotation(1); tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);
  
  // LOGO (OPCIONAL - SOLO TEXTO POR AHORA)
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE);
  tft.drawString("STIFFIO SYSTEM", 240, 160, 4);
  delay(2000);

  graphSprite.setColorDepth(8); 
  graphSprite.createSprite(GRAPH_W, GRAPH_H);

  touch.begin(); touch.setRotation(1);

  if (!iniciarSensores()) {
     tft.fillScreen(TFT_RED); tft.drawString("ERROR I2C", 240, 160, 4);
     while(1); 
  }

  xTaskCreatePinnedToCore(TaskSensores,"SensorTask",10000,NULL,1,NULL,0);
  dibujarMenuPrincipal();
}

// ======================================================================
// LOOP PRINCIPAL
// ======================================================================
void loop() {
  
  // LECTURA DE TOUCH
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    int x = map(p.x, 200, 3700, 480, 0);
    int y = map(p.y, 200, 3800, 320, 0);
    
    // --- ESTADO: MENU ---
    if (pantallaActual == MENU) {
      if (x > btnX && x < btnX + btnW && y > btnY1 && y < btnY1 + btnH) {
         sonarPitido(); 
         edadInput = ""; 
         dibujarTecladoEdad(); // IR A EDAD
         delay(300);
      }
    }
    
    // --- EDAD ---
    else if (pantallaActual == PANTALLA_EDAD) {
       // Teclado Numérico
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
       // Volver
       if ((x-50)*(x-50)+(y-275)*(y-275) <= 900) { 
          sonarPitido(); dibujarMenuPrincipal(); delay(300); 
       }
       // Siguiente
       if (edadInput.length() > 0 && (x-430)*(x-430)+(y-275)*(y-275) <= 900) {
          int val = edadInput.toInt();
          if (val >= 15 && val <= 99) {
             sonarPitido(); 
             pacienteEdad = val; // GUARDAR EDAD
             alturaInput = ""; 
             dibujarTecladoAltura(); // IR A ALTURA
             delay(300);
          } else {
             mostrarAlerta("EDAD INVALIDA", "15 - 99 anos");
          }
       }
    }
    
    // --- ALTURA ---
    else if (pantallaActual == PANTALLA_ALTURA) {
       // Teclado
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
       // Volver a Edad
       if ((x-50)*(x-50)+(y-275)*(y-275) <= 900) { 
          sonarPitido(); dibujarTecladoEdad(); delay(300); 
       }
       // SIGUIENTE -> IR A MEDICIÓN
       if (alturaInput.length() > 0 && (x-430)*(x-430)+(y-275)*(y-275) <= 900) {
          int val = alturaInput.toInt();
          if (val >= 50 && val <= 250) { // Rango amplio
             sonarPitido(); 
             pacienteAltura = val; // GUARDAR ALTURA
             
             // INICIAR MEDICIÓN
             pantallaActual = MEDICION;
             prepararPantallaMedicion();
             medicionActiva = true;
             
             portENTER_CRITICAL(&bufferMux);
             faseMedicion = 0; // REINICIAR MAQUINA DE ESTADOS
             s1ok = false; s2ok = false;
             portEXIT_CRITICAL(&bufferMux);
             
             delay(300);
          } else {
             mostrarAlerta("ALTURA INVALIDA", "50 - 250 cm");
          }
       }
    }

    // --- MEDICIÓN ---
    else if (pantallaActual == MEDICION) {
      // Botón VOLVER (Esquina inferior derecha)
      if (x > 380 && y > 260) {
        medicionActiva = false; // El otro núcleo deja de procesar
        pantallaActual = MENU;
        dibujarMenuPrincipal();
        delay(300);
      }
    }
  }

  // SOLO REFRESCO DE PANTALLA
  // Ya no hay lectura de sensor aquí.
  if (medicionActiva && pantallaActual == MEDICION) {
    if (millis() - lastDrawTime >= DRAW_INTERVAL) {
      lastDrawTime = millis();
      actualizarGrafico();
    }
  }
}
