#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Wire.h>
#include "MAX30105.h"

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
volatile int writeHead = 0; // Índice de escritura del sensor

volatile bool medicionActiva = false;

// Estados: 0=Esperando, 1=Estabilizando, 2=Midiendo
volatile int faseMedicion = 0; 
volatile int porcentajeEstabilizacion = 0;

// Variables de estado compartidas
volatile bool s1ok = false;
volatile bool s2ok = false;

// --- VARIABLE RITMO CARDÍACO ---
volatile int bpmMostrado = 0; // Entero puro, sin decimales

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

// Estados
enum Estado { MENU, MEDICION };
Estado pantallaActual = MENU;

// Coordenadas Gráfico
#define GRAPH_X 40
#define GRAPH_Y 50
#define GRAPH_W 400
#define GRAPH_H 180

// Botones Menú
int btnW = 300; int btnH = 70;
int btnX = (480 - btnW) / 2;
int btnY1 = 85;  // Test Rápido
int btnY2 = 185; // Estudio Completo

// Tiempos de Refresco Visual (El muestreo va aparte)
unsigned long lastDrawTime = 0;
const unsigned long DRAW_INTERVAL = 40; // ~25fps visuales

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

  unsigned long startContactTime = 0; 
  unsigned long baseTime = 0;         
  const unsigned long TIEMPO_ESTABILIZACION = 10000; // 10 Segundos de calibración

  // --- ALGORITMO HR (RITMO CARDÍACO) ---
  float lastValS2 = 0;
  unsigned long lastBeatTime = 0;
  const int REFRACTORY_PERIOD = 300; // ms (Mínimo tiempo entre latidos)
  const float BEAT_THRESHOLD = 20.0; // Sensibilidad de pendiente
  
  // Promedio de 10 muestras (Máxima estabilidad)
  const int BPM_AVG_SIZE = 10;
  int bpmBuffer[BPM_AVG_SIZE] = {0};
  int bpmIdx = 0;
  int validSamples = 0;

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

          // Media Móvil
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
          float delta = valFinal2 - lastValS2;
          lastValS2 = valFinal2;

          if (faseMedicion == 2) { // Solo calculamos HR si ya estamos midiendo
             if (delta > BEAT_THRESHOLD && (now - lastBeatTime > REFRACTORY_PERIOD)) {
                unsigned long deltaT = now - lastBeatTime;
                lastBeatTime = now;
                
                int instantBPM = 60000 / deltaT;

                // Rango fisiológico aceptable (evita ruido extremo)
                if (instantBPM > 40 && instantBPM < 200) {
                   bpmBuffer[bpmIdx] = instantBPM;
                   bpmIdx = (bpmIdx + 1) % BPM_AVG_SIZE;
                   
                   // Contamos muestras válidas
                   if (validSamples < BPM_AVG_SIZE) validSamples++;

                   // CONDICIÓN ESTRICTA:
                   // Solo actualizamos la variable global si el buffer está LLENO (10 muestras)
                   if (validSamples >= BPM_AVG_SIZE) { 
                      long totalBPM = 0;
                      for(int i=0; i<BPM_AVG_SIZE; i++) totalBPM += bpmBuffer[i];
                      
                      // Promedio perfecto de 10 latidos
                      bpmMostrado = totalBPM / BPM_AVG_SIZE;
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
                faseMedicion = 2;
                baseTime = millis(); 

                portENTER_CRITICAL(&bufferMux);
                writeHead = 0;
                for(int i=0; i<BUFFER_SIZE; i++) { buffer_s1[i]=0; buffer_s2[i]=0; buffer_time[i]=0; }
                for(int i=0; i<MA_SIZE; i++) { bufMA1[i]=0; bufMA2[i]=0; }
                
                bpmMostrado = 0;   // Reset BPM display
                validSamples = 0;  // Reset contador de latidos válidos
                
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
             bpmMostrado = 0; 
             validSamples = 0;
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
void dibujarBoton(int x, int y, int w, int h, String texto, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 12, color);
  tft.drawRoundRect(x, y, w, h, 12, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(texto, x + w/2, y + h/2 + 5, 4); // Ajuste visual texto
}

void dibujarMenuPrincipal() {
  tft.fillScreen(TFT_BLACK); // Menú se queda negro
  
  // Header / Logo simulado
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("STIFFIO", 240, 40, 4);

  // Botón 1: TEST RAPIDO (Verde según tu estilo o Rojo estandar)
  dibujarBoton(btnX, btnY1, btnW, btnH, "TEST RAPIDO", TFT_RED);

  // Botón 2: ESTUDIO COMPLETO (Gris o Azul - Inactivo por ahora)
  dibujarBoton(btnX, btnY2, btnW, btnH, "ESTUDIO COMPLETO", tft.color565(50,50,50));
}

void prepararPantallaMedicion() {
  tft.fillScreen(COLOR_FONDO_MEDICION);
  
  // Header
  tft.fillRect(0, 0, 480, 40, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Monitor en Tiempo Real", 10, 20, 4);

  // Botón Volver
  tft.fillRoundRect(380, 270, 90, 45, 8, TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("VOLVER", 425, 292, 2);

  // Leyendas
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_S1); tft.drawString("S1: Cuello", 40, 270, 2);
  tft.setTextColor(COLOR_S2); tft.drawString("S2: Muneca", 40, 290, 2); // Sin ñ por seguridad de fuente

  // Marco del gráfico
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_W + 2, GRAPH_H + 2, TFT_BLACK);

  // --- EJE Y (AMPLITUD) ---
  // El "0" lo dejamos fijo a la izquierda como referencia
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_BLACK); 
  tft.drawString("0", GRAPH_X - 5, GRAPH_Y + GRAPH_H/2, 2); 
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
    if (!localS1 && !localS2) {
       graphSprite.setTextColor(TFT_BLACK); graphSprite.drawString("COLOCAR AMBOS SENSORES", GRAPH_W/2, GRAPH_H/2, 4);
    } else if (!localS1) {
       graphSprite.setTextColor(COLOR_S1); graphSprite.drawString("FALTA SENSOR CUELLO", GRAPH_W/2, GRAPH_H/2, 4);
    } else if (!localS2) {
       graphSprite.setTextColor(COLOR_S2); graphSprite.drawString("FALTA SENSOR MUNECA", GRAPH_W/2, GRAPH_H/2, 4);
    }
    graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
    return;
  }
  
  if (localFase == 1) {
    graphSprite.setTextDatum(MC_DATUM);
    graphSprite.setTextColor(TFT_BLUE);
    graphSprite.drawString("ESTABILIZANDO...", GRAPH_W/2, GRAPH_H/2 - 20, 4);
    int barW = 200; int barH = 20; int barX = (GRAPH_W - barW) / 2; int barY = GRAPH_H/2 + 10;
    graphSprite.drawRect(barX, barY, barW, barH, TFT_BLACK);
    int fillW = (barW * localPorcentaje) / 100;
    graphSprite.fillRect(barX+1, barY+1, fillW-2, barH-2, TFT_GREEN);
    graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
    return;
  }

  // Mostrar BPM en la esquina superior derecha
  if (localBPM > 0) {
    graphSprite.setTextDatum(TR_DATUM);
    graphSprite.setTextColor(TFT_BLACK);
    graphSprite.drawString("HR: " + String(localBPM) + " BPM", GRAPH_W - 10, 10, 4);
  } else {
    graphSprite.setTextDatum(TR_DATUM);
    graphSprite.setTextColor(TFT_LIGHTGREY);
    graphSprite.drawString("HR: -- BPM", GRAPH_W - 10, 10, 4);
  }

  // FASE 2: GRÁFICO
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
// SETUP
// ======================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LCD_LED_PIN, OUTPUT);
  digitalWrite(LCD_LED_PIN, HIGH);

  // Inicializar Pantalla
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  graphSprite.setColorDepth(8); 
  graphSprite.createSprite(GRAPH_W, GRAPH_H);

  // Inicializar Touch
  touch.begin();
  touch.setRotation(1);

  // Inicializar Sensores (Se hace una vez antes de arrancar la tarea)
  // No iniciamos la medición todavía
  if (!iniciarSensores()) {
     tft.fillScreen(TFT_RED);
     tft.drawString("ERROR I2C", 240, 160, 4);
     while(1); // Bloquear si falla hardware
  }

  // --- LANZAR EL SEGUNDO NÚCLEO ---
  // Esto crea la tarea paralela que se encargará del sensor siempre
  xTaskCreatePinnedToCore(
    TaskSensores,   // Función
    "SensorTask",   // Nombre
    10000,          // Stack size
    NULL,           // Parametros
    1,              // Prioridad
    NULL,           // Handle
    0               // NÚCLEO 0
  );

  // Dibujar tu Menú Original
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
      // Botón TEST RAPIDO
      if (x > btnX && x < btnX + btnW && y > btnY1 && y < btnY1 + btnH) {
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Iniciando...", 240, 160, 4);
        delay(500); 

        pantallaActual = MEDICION;
        prepararPantallaMedicion();

        // Reiniciamos buffer antes de empezar
        portENTER_CRITICAL(&bufferMux);
        faseMedicion = 0; 
        s1ok = false; s2ok = false;
        writeHead = 0;
        portEXIT_CRITICAL(&bufferMux);
        
        // ¡ESTO ACTIVA AL OTRO NÚCLEO!
        medicionActiva = true; 
      }
    }
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
