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
#define COLOR_S1             TFT_RED
#define COLOR_S2             TFT_BLUE // Azul oscuro para contraste en blanco

// ======================================================================
// FILTROS
// ======================================================================
// Alpha HP (Pasa Altos): Elimina la deriva (DC) y la respiración lenta.
// Alpha LP (Pasa Bajos): Elimina el ruido eléctrico "dientes de sierra".
const float alpha_hp = 0.95; 
const float alpha_lp = 0.15;  // Cuanto más bajo, más suave la curva

// Umbral para detectar dedo (Evita que arranque con ruido)
const long FINGER_THRESHOLD = 50000; 

// Variables de estado para los filtros 
float hp_out1 = 0, lp_out1 = 0, prev_in1 = 0;
float hp_out2 = 0, lp_out2 = 0, prev_in2 = 0;

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
bool medicionActiva = false;
bool dedoPresente = false;

// Buffer Gráfico (4 segundos @ 50Hz = 200 muestras)
// Pantalla de 400px ancho / 200 muestras = 2 pixeles por paso
#define BUFFER_SIZE 200
float buffer_s1[BUFFER_SIZE];
float buffer_s2[BUFFER_SIZE];
int bufferIndex = 0;

// Contador Absoluto de Muestras (Para el Eje X Móvil)
unsigned long totalMuestras = 0; 

// Tiempos
unsigned long lastSampleTime = 0; 
const unsigned long SAMPLE_INTERVAL = 20000; // 50Hz
unsigned long lastDrawTime = 0;
const unsigned long DRAW_INTERVAL = 40; // ~25fps

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

// ======================================================================
// FUNCIONES DE FILTRADO
// ======================================================================
float aplicarFiltros(float rawInput, float &hp_out, float &lp_out, float &prev_in) {
  // 1. Normalización básica (opcional, pero ayuda a mantener números manejables)
  // Usamos un factor de escala simple en lugar de dividir por MAX_ADC para no perder precisión en float
  float input = rawInput; 

  // 2. Filtro Pasa Altos (High Pass) -> Elimina Deriva/Gravedad
  // Formula: y[i] = alpha * (y[i-1] + x[i] - x[i-1])
  hp_out = alpha_hp * (hp_out + input - prev_in);
  prev_in = input;

  // 3. Filtro Pasa Bajos (Low Pass) -> Suaviza Picos
  // Formula: y[i] = alpha * x[i] + (1-alpha) * y[i-1]
  lp_out = (alpha_lp * hp_out) + ((1.0 - alpha_lp) * lp_out);

  return lp_out;
}

// ======================================================================
// INICIALIZACIÓN
// ======================================================================
bool iniciarSensores() {
  Wire.begin(SDA1, SCL1, I2C_SPEED_FAST);
  Wire1.begin(SDA2, SCL2, I2C_SPEED_FAST);

  if (!sensorProx.begin(Wire, I2C_SPEED_FAST)) return false;
  if (!sensorDist.begin(Wire1, I2C_SPEED_FAST)) return false;

  // Configuración estable (sampleAverage=4 ayuda mucho al hardware, aumentar a 8 para menos ruido eléctrico)
  sensorProx.setup(0x2F, 8, 2, 400, 411, 4096);
  sensorDist.setup(0x2F, 8, 2, 400, 411, 4096);
  
  // Resetear filtros
  hp_out1 = 0; lp_out1 = 0; prev_in1 = sensorProx.getIR();
  hp_out2 = 0; lp_out2 = 0; prev_in2 = sensorDist.getIR();
  
  // Limpiar buffer
  for(int i=0; i<BUFFER_SIZE; i++) { buffer_s1[i]=0; buffer_s2[i]=0; }
  bufferIndex = 0;
  totalMuestras = 0; // Resetear tiempo
  
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
  tft.setTextColor(COLOR_S1); tft.drawString("S1: Proximal", 40, 270, 2);
  tft.setTextColor(COLOR_S2); tft.drawString("S2: Distal", 40, 290, 2);

  // Marco del gráfico
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_W + 2, GRAPH_H + 2, TFT_BLACK);

  // --- EJE Y (AMPLITUD) ---
  // El "0" lo dejamos fijo a la izquierda como referencia
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_BLACK); 
  tft.drawString("0", GRAPH_X - 5, GRAPH_Y + GRAPH_H/2, 2); 
}

// ======================================================================
// LÓGICA DE DIBUJADO (SPRITE + EJE MÓVIL)
// ======================================================================
void actualizarGrafico() {
  // 1. Limpiar Sprite (Fondo Blanco)
  graphSprite.fillSprite(COLOR_FONDO_MEDICION);

  // Mensaje si no detecta dedo
  if (!dedoPresente) {
    graphSprite.setTextDatum(MC_DATUM);
    graphSprite.setTextColor(TFT_BLACK);
    graphSprite.drawString("ESPERANDO DEDOS...", GRAPH_W/2, GRAPH_H/2 - 20, 4);
    graphSprite.drawString("Colocar sensores", GRAPH_W/2, GRAPH_H/2 + 20, 2);
    graphSprite.pushSprite(GRAPH_X, GRAPH_Y);
    return; // No dibujar ondas si no hay dedo
  }
  
  // 2. Línea Central Fija (Referencia Nivel 0)
  graphSprite.drawFastHLine(0, GRAPH_H/2, GRAPH_W, TFT_LIGHTGREY); 

  // 3. Autoescala
  float minV = -50, maxV = 50;
  for(int i=0; i<BUFFER_SIZE; i++) {
    if(buffer_s1[i] < minV) minV = buffer_s1[i];
    if(buffer_s1[i] > maxV) maxV = buffer_s1[i];
    if(buffer_s2[i] < minV) minV = buffer_s2[i];
    if(buffer_s2[i] > maxV) maxV = buffer_s2[i];
  }

  // Clamp anti-ruido (Para que no haga zoom en estática)
  if((maxV - minV) < 600) {
    float mid = (maxV + minV) / 2;
    maxV = mid + 300; minV = mid - 300;
  }
  
  // 4. Dibujar Onda + Rejilla Móvil
  // Recorremos el buffer de izquierda (antiguo) a derecha (nuevo)
  float xStep = (float)GRAPH_W / (float)BUFFER_SIZE; 
  
  // Configuración texto para los segundos
  graphSprite.setTextDatum(TC_DATUM); 
  graphSprite.setTextColor(COLOR_EJE);

  for (int i = 0; i < BUFFER_SIZE - 1; i++) {
    // Índices circulares
    int idx = (bufferIndex + i) % BUFFER_SIZE;
    int nextIdx = (bufferIndex + i + 1) % BUFFER_SIZE;

    // Coordenadas X en pantalla
    int x1 = (int)(i * xStep);
    int x2 = (int)((i + 1) * xStep);

    // --- LÓGICA DE EJE X MÓVIL ---
    // Calculamos a qué muestra histórica corresponde este píxel
    // totalMuestras = final del buffer (borde derecho)
    // restamos para saber la antiguedad hacia la izquierda
    long absoluteSample = totalMuestras - (BUFFER_SIZE - 1) + i;

    // Si es múltiplo de 50 (cada 1 segundo exacto a 50Hz)
    if (absoluteSample > 0 && absoluteSample % 50 == 0) {
      // Línea vertical tenue
      graphSprite.drawFastVLine(x1, 0, GRAPH_H, COLOR_GRILLA);
      
      // Texto "Xs" que viaja con la línea
      String label = String(absoluteSample / 50) + "s";
      graphSprite.drawString(label, x1, GRAPH_H - 20, 2); 
    }

        // Ignorar si no hay datos
    if(buffer_s1[idx] == 0 && buffer_s1[nextIdx] == 0) continue;

    // Mapeo Y
    int y1A = map((long)buffer_s1[idx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y1B = map((long)buffer_s1[nextIdx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y2A = map((long)buffer_s2[idx], (long)minV, (long)maxV, GRAPH_H, 0);
    int y2B = map((long)buffer_s2[nextIdx], (long)minV, (long)maxV, GRAPH_H, 0);

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
        
        if (iniciarSensores()) {
          pantallaActual = MEDICION;
          medicionActiva = true;
          dedoPresente = false; // Asumimos que no hay dedo al iniciar
          prepararPantallaMedicion();
        } else {
          tft.drawString("ERROR DE SENSOR", 240, 160, 4);
          delay(2000);
          dibujarMenuPrincipal();
        }
        delay(300); // Debounce
      }
    }
    
    // --- ESTADO: MEDICION ---
    else if (pantallaActual == MEDICION) {
      // Botón VOLVER (Esquina inferior derecha)
      if (x > 380 && y > 260) {
        medicionActiva = false;
        pantallaActual = MENU;
        dibujarMenuPrincipal();
        delay(300);
      }
    }
  }

  // LÓGICA DE MEDICIÓN (Sin delays bloqueantes)
  if (medicionActiva && pantallaActual == MEDICION) {
    
    // 1. Muestreo a 50Hz (Exacto)
    if (micros() - lastSampleTime >= SAMPLE_INTERVAL) {
      lastSampleTime = micros();

      float raw1 = (float)sensorProx.getIR();
      float raw2 = (float)sensorDist.getIR();

      // AJUSTE 5: Lógica de inicio/pausa según detección de dedo
      // Si la lectura es menor al umbral, NO avanzamos el tiempo ni el buffer
      if (raw1 < FINGER_THRESHOLD || raw2 < FINGER_THRESHOLD) {
         dedoPresente = false;
         
         // Reseteamos filtros para que no den saltos al volver el dedo
         prev_in1 = raw1; prev_in2 = raw2;
         hp_out1 = 0; hp_out2 = 0;
         
         // NO incrementamos totalMuestras -> El tiempo se pausa
         // NO incrementamos bufferIndex -> El gráfico se pausa

      } else {
         dedoPresente = true;
         // Aplicamos filtros solo si hay señal válida
         buffer_s1[bufferIndex] = aplicarFiltros(raw1, hp_out1, lp_out1, prev_in1);
         buffer_s2[bufferIndex] = aplicarFiltros(raw2, hp_out2, lp_out2, prev_in2);
      
         // Incrementar contador total (para el eje X móvil)
         totalMuestras++;

         // Buffer circular
         bufferIndex++;
         if (bufferIndex >= BUFFER_SIZE) bufferIndex = 0;
      }
    }

    // 2. Refresco de Pantalla (~25-30 FPS)
    if (millis() - lastDrawTime >= DRAW_INTERVAL) {
      lastDrawTime = millis();
      actualizarGrafico();
    }
  }
}

// ======================================================================
// SETUP
// ======================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LCD_LED_PIN, OUTPUT);
  digitalWrite(LCD_LED_PIN, HIGH);

  // TFT y Sprite
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);

  graphSprite.setColorDepth(8); // 8 bits es ligero y rápido
  graphSprite.createSprite(GRAPH_W, GRAPH_H);

  // Touch
  touch.begin();
  touch.setRotation(1);

  dibujarMenuPrincipal();
}