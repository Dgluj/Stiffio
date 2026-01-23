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

// Variables de estado compartidas
volatile bool medicionActiva = false;
volatile bool s1ok = false;
volatile bool s2ok = false;
volatile unsigned long totalMuestras = 0;

// Semáforo para proteger la memoria (evita que se lea mientras se escribe)
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// ======================================================================
// VARIABLES GLOBALES
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
// TAREA NÚCLEO 0: SENSOR Y MATEMÁTICA (NO TOCAR UI AQUÍ)
// ======================================================================
void TaskSensores(void *pvParameters) {
  // Variables locales del filtro (Fórmula Mágica)
  const int NUM_READINGS = 4;
  long readings1[NUM_READINGS] = {0};
  long readings2[NUM_READINGS] = {0};
  int readIndex = 0;
  long total1 = 0, total2 = 0;
  float dc1 = 0, dc2 = 0;
  const float alpha = 0.95;
  const long FINGER_THRESHOLD = 50000;

  // Loop infinito de alta velocidad
  for (;;) {
    if (medicionActiva) {
      
      long ir1 = sensorProx.getIR();
      long ir2 = sensorDist.getIR();

      // Actualizar estado global de sensores
      s1ok = (ir1 > FINGER_THRESHOLD);
      s2ok = (ir2 > FINGER_THRESHOLD);

      if (s1ok && s2ok) {
        // --- PROCESAMIENTO MATEMÁTICO (TU CÓDIGO) ---
        total1 -= readings1[readIndex]; readings1[readIndex] = ir1; total1 += readings1[readIndex];
        long smooth1 = total1 / NUM_READINGS;
        dc1 = (alpha * dc1) + (1.0 - alpha) * smooth1;
        float val1 = smooth1 - dc1;

        total2 -= readings2[readIndex]; readings2[readIndex] = ir2; total2 += readings2[readIndex];
        long smooth2 = total2 / NUM_READINGS;
        dc2 = (alpha * dc2) + (1.0 - alpha) * smooth2;
        float val2 = smooth2 - dc2;

        readIndex = (readIndex + 1) % NUM_READINGS;

        // --- GUARDADO SEGURO EN BUFFER COMPARTIDO ---
        portENTER_CRITICAL(&bufferMux); 
        buffer_s1[writeHead] = val1;
        buffer_s2[writeHead] = val2;
        writeHead++;
        if (writeHead >= BUFFER_SIZE) writeHead = 0;
        totalMuestras++;
        portEXIT_CRITICAL(&bufferMux);

      } else {
         // Reset filtros si no hay sensor
         dc1 = ir1; total1 = ir1 * NUM_READINGS;
         dc2 = ir2; total2 = ir2 * NUM_READINGS;
         for(int i=0; i<NUM_READINGS; i++) { readings1[i]=ir1; readings2[i]=ir2; }
      }
    }
    
    // CONTROL DE TIEMPO EXACTO: 10ms = 100Hz
    // Esto garantiza que el tiempo fluya real, sin dilatarse
    vTaskDelay(pdMS_TO_TICKS(10)); 
  }
}

// ======================================================================
// FUNCIONES
// ======================================================================
bool iniciarSensores() {
  Wire.begin(SDA1, SCL1, 400000);
  Wire1.begin(SDA2, SCL2, 400000);

  if (!sensorProx.begin(Wire, I2C_SPEED_FAST)) return false;
  if (!sensorDist.begin(Wire1, I2C_SPEED_FAST)) return false;

  // Configuración Rápida y Suave
  sensorProx.setup(31, 4, 2, 100, 411, 4096);
  sensorDist.setup(31, 4, 2, 100, 411, 4096);
  
  // Limpiar Buffers Globales
  portENTER_CRITICAL(&bufferMux);
  for(int i=0; i<BUFFER_SIZE; i++) { buffer_s1[i]=0; buffer_s2[i]=0; }
  writeHead = 0;
  totalMuestras = 0;
  portEXIT_CRITICAL(&bufferMux);
  
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
  // 1. COPIA SEGURA DE DATOS (SNAPSHOT)
  // Copiamos los datos del núcleo 0 a variables locales para dibujar tranquilos
  float localBuf1[BUFFER_SIZE];
  float localBuf2[BUFFER_SIZE];
  int localHead;
  unsigned long localTotalMuestras;

  portENTER_CRITICAL(&bufferMux);
  memcpy(localBuf1, (const void*)buffer_s1, sizeof(buffer_s1));
  memcpy(localBuf2, (const void*)buffer_s2, sizeof(buffer_s2));
  localHead = writeHead;
  localTotalMuestras = totalMuestras;
  portEXIT_CRITICAL(&bufferMux);

  // 1. Limpiar Sprite (Fondo Blanco)
  graphSprite.fillSprite(COLOR_FONDO_MEDICION);

  // 2. VERIFICAR SENSORES Y MOSTRAR MENSAJES ESPECÍFICOS
  // Si falta ALGUNO de los dos, detenemos el graficado y mostramos alerta
  if (!s1ok || !s2ok) {
    graphSprite.setTextDatum(MC_DATUM);
    
    if (!s1ok && !s2ok) {
       // Faltan ambos
       graphSprite.setTextColor(TFT_BLACK);
       graphSprite.drawString("ESPERANDO AMBOS SENSORES...", GRAPH_W/2, GRAPH_H/2, 4);
    } 
    else if (!s1ok) {
       // Falta solo S1
       graphSprite.setTextColor(COLOR_S1); // Mensaje en ROJO
       graphSprite.drawString("COLOCAR SENSOR PROXIMAL (S1)", GRAPH_W/2, GRAPH_H/2, 4);
    } 
    else if (!s2ok) {
       // Falta solo S2
       graphSprite.setTextColor(COLOR_S2); // Mensaje en AZUL
       graphSprite.drawString("COLOCAR SENSOR DISTAL (S2)", GRAPH_W/2, GRAPH_H/2, 4);
    }

    graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
    return; // Salir sin dibujar ondas
  }
  // --- AMBOS sensores están puestos ---
  
  // 3. Línea Central Fija (Referencia Nivel 0)
  graphSprite.drawFastHLine(0, GRAPH_H/2, GRAPH_W, TFT_LIGHTGREY); 

  // 4. Autoescala
  float minV = -50, maxV = 50;
  for(int i=0; i<BUFFER_SIZE; i++) {
    if(localBuf1[i] < minV) minV = localBuf1[i];
    if(localBuf1[i] > maxV) maxV = localBuf1[i];
  }

  // Clamp anti-ruido (Para que no haga zoom en estática)
  if((maxV - minV) < 100) {
    float mid = (maxV + minV) / 2;
    maxV = mid + 50; minV = mid - 50;
  }
  
  // 5. Dibujar Onda + Rejilla Móvil
  float xStep = (float)GRAPH_W / (float)BUFFER_SIZE; // Recorremos el buffer de izquierda (antiguo) a derecha (nuevo)
  
  // Configuración texto para los segundos
  graphSprite.setTextDatum(TC_DATUM); 
  graphSprite.setTextColor(COLOR_EJE);

  for (int i = 0; i < BUFFER_SIZE - 1; i++) {
    // Truco: Usamos 'localHead' para alinear el gráfico siempre igual
    // Esto hace que la onda vieja salga por la izq y la nueva entre por la derecha
    int idx = (localHead + i) % BUFFER_SIZE;
    int nextIdx = (localHead + i + 1) % BUFFER_SIZE;

    // Coordenadas X en pantalla
    int x1 = (int)(i * xStep);
    int x2 = (int)((i + 1) * xStep);

    // EJE X MÓVIL
    // Calculamos el tiempo basado en el contador total del Núcleo 0
    long absoluteSample = localTotalMuestras - (BUFFER_SIZE - 1) + i; // totalMuestras = final del buffer (borde derecho), restamos para saber la antiguedad hacia la izquierda

    if (absoluteSample > 0 && absoluteSample % 100 == 0) { // Cada 100 muestras = 1 segundo
      graphSprite.drawFastVLine(x1, 0, GRAPH_H, COLOR_GRILLA); // Línea vertical tenue
      // Texto "Xs" que viaja con la línea
      String label = String(absoluteSample / 100) + "s";
      graphSprite.drawString(label, x1, GRAPH_H - 20, 2); 
    }

    // Ignorar si no hay datos
    if(localBuf1[idx] == 0 && localBuf1[nextIdx] == 0) continue;

    // Mapeo Y
    int y1A = map((long)localBuf1[idx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y1B = map((long)localBuf1[nextIdx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y2A = map((long)localBuf2[idx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y2B = map((long)localBuf2[nextIdx], (long)minV, (long)maxV, GRAPH_H, 0);

    // Clipping (Seguridad visual)
    y1A = constrain(y1A, 0, GRAPH_H-1); y1B = constrain(y1B, 0, GRAPH_H-1);
    y2A = constrain(y2A, 0, GRAPH_H-1); y2B = constrain(y2B, 0, GRAPH_H-1);

    // Dibujar líneas de señal
    graphSprite.drawLine(x1, y1A, x2, y1B, COLOR_S1);
    graphSprite.drawLine(x1, y2A, x2, y2B, COLOR_S2);
  }

  // 5. Estampar Sprite
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
     tft.drawString("ERROR SENSOR", 240, 160, 4);
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
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("Inicializando...", 240, 160, 4);
        
        // Reiniciamos buffer antes de empezar
        portENTER_CRITICAL(&bufferMux);
        totalMuestras = 0;
        writeHead = 0;
        for(int i=0; i<BUFFER_SIZE; i++) { buffer_s1[i]=0; buffer_s2[i]=0; }
        portEXIT_CRITICAL(&bufferMux);

        pantallaActual = MEDICION;
        prepararPantallaMedicion();
        
        // ¡ESTO ACTIVA AL OTRO NÚCLEO!
        medicionActiva = true; 
        
        delay(500); 
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
