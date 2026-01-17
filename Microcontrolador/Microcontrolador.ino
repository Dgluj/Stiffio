/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LIBRERÍAS
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "SH1106Wire.h"


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SENSORES Y ACTUADORES
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Sensores MAX30102
MAX30105 particleSensor1; // Sensor proximal (SDA=21, SCL=22)
MAX30105 particleSensor2; // Sensor distal (SDA=18, SCL=19)
bool sensor1_ok = false;
bool sensor2_ok = false;

// Pantalla OLED
SH1106Wire display(0x3C, 21, 22);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WIFI Y COMUNICACIÓN
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* ssid = "iPhone de Victoria (4)";
const char* password = "vitucapa";
WebSocketsServer webSocket(81);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PARÁMETROS Y VARIABLES GLOBALES
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Intervalo de muestreo
const unsigned long sampleIntervalUs = 20000; // 50 Hz → 20 ms
unsigned long lastSampleTimeUs = 0;

// Filtrado de la señal
const float fc_hp = 0.5;
const float fc_lp = 5.0;
float alpha_hp;
float alpha_lp;
float ir1_prev_in = 0.0;
float ir2_prev_in = 0.0;
float ir1_hp_out = 0.0;
float ir2_hp_out = 0.0;
float ir1_lp_out = 0.0;
float ir2_lp_out = 0.0;

// Cálculo de HR
float irFiltered = 0;
float irHighPass = 0;
float alphaLow = 0.3;
float alphaHigh = 0.05;
const byte RATE_SIZE = 40;  // Buffer para promedio de HR
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = -1;  // HR promedio inicializado como valor inválido
const long IR_THRESHOLD = 50000;
bool isStabilized = false;
int validBeatsCount = 0; // Contador de latidos válidos detectados
unsigned long lastDisplayUpdate = 0;
unsigned long lastSensorCheck = 0;  // Para reducir frecuencia de verificación de sensores
const unsigned long SENSOR_CHECK_INTERVAL = 2000;  // Verificar sensores cada 2 segundos




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SET UP
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Función para vaciar FIFOs de ambos sensores (sincronización temporal)
void clearFIFOs() {
  // Vaciar FIFO del sensor 1 (proximal) - solo si está conectado
  if (sensor1_ok) {
    for (int i = 0; i < 32; i++) {
      long val = particleSensor1.getIR();
      if (val < 0) break;  // Si hay error I2C, salir
    }
  }
  // Vaciar FIFO del sensor 2 (distal) - solo si está conectado
  if (sensor2_ok) {
    for (int i = 0; i < 32; i++) {
      long val = particleSensor2.getIR();
      if (val < 0) break;  // Si hay error I2C, salir
    }
  }
}

// Función para limpiar todos los buffers y filtros (cuando se detecta desconexión)
void clearAllBuffers() {
  // Limpiar filtros de señal
  ir1_prev_in = 0.0;
  ir2_prev_in = 0.0;
  ir1_hp_out = 0.0;
  ir2_hp_out = 0.0;
  ir1_lp_out = 0.0;
  ir2_lp_out = 0.0;
  
  // Limpiar filtros de HR
  irFiltered = 0;
  irHighPass = 0;
  beatAvg = -1;
  rateSpot = 0;
  for (byte i = 0; i < RATE_SIZE; i++) rates[i] = 70;
  beatsPerMinute = 0;
  validBeatsCount = 0;
  isStabilized = false;
  lastBeat = 0;
}

void setup() {
  // Serial.begin(115200);  // COMENTADO: elimina delays por transmisión serial
  // delay(1000);

  // Serial.println("STIFFIO");  // COMENTADO
  // Serial.println("Iniciando...");  // COMENTADO

  // Inicializar OLED ------------------------------------------------------------------------------
  display.init();
  display.flipScreenVertically();
  // Serial.println("OLED conectado");  // COMENTADO

  // Mostrar en pantalla
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(30, 15, "STIFFIO");  // Mostrar en pantalla STIFFIO al encender
  display.display();
  delay(2000);
  display.setFont(ArialMT_Plain_10);
  display.drawString(20, 35, "Iniciando sistema...."); 
  display.display();
  delay(2000);

  // Inicializar Sensores ----------------------------------------------------------------------------
  Wire.begin(21, 22); // Sensor proximal (SDA=21, SCL=22)
  particleSensor1.begin(Wire, I2C_SPEED_FAST);
  particleSensor1.setup(60, 1, 2, 50, 411, 4096);
  particleSensor1.setPulseAmplitudeRed(0x0A);
  particleSensor1.setPulseAmplitudeIR(0x2F);

  Wire1.begin(18, 19); // Sensor distal (SDA=18, SCL=19)
  particleSensor2.begin(Wire1, I2C_SPEED_FAST);
  particleSensor2.setup(60, 1, 2, 50, 411, 4096);
  particleSensor2.setPulseAmplitudeRed(0x0A);
  particleSensor2.setPulseAmplitudeIR(0x2F);
  
  // Vaciar FIFOs de ambos sensores para sincronización temporal
  clearFIFOs();

  // Conectar WiFi -----------------------------------------------------------------------------------
  WiFi.begin(ssid, password);
  // Serial.print("Conectando a WiFi...");  // COMENTADO

  bool wifiConnected = false;
  unsigned long wifiStartTime = millis();
  const unsigned long wifiTimeout = 10000; // Timeout de 10 segundos
  const unsigned long wifiRetryInterval = 5000; // Reintento de conexión cada 5s

  while (!wifiConnected) {
      // Alimentar watchdog durante espera de WiFi
      yield();
      
      // Chequeo timeout de 10 segundos
      if (millis() - wifiStartTime >= wifiTimeout) {
          // Serial.println("ERROR: WiFi no conectada");  // COMENTADO
          
          display.clear();
          display.setFont(ArialMT_Plain_10);
          display.drawString(40, 10, "ERROR");
          display.drawString(25, 25, "WiFi no conectada");
          display.drawString(25, 40, "Verifique la red");
          display.display();
          
          // Esperar antes de reintentar (con yield para no bloquear watchdog)
          for (int i = 0; i < wifiRetryInterval / 100; i++) {
            yield();
            delay(100);
          }
          wifiStartTime = millis(); // Reiniciar contador de timeout
          WiFi.begin(ssid, password); // Reintentar conexión
      }

      if (WiFi.status() == WL_CONNECTED) {
          wifiConnected = true;
          // Serial.println("WiFi conectado!");  // COMENTADO
          // Serial.print("IP ESP32: ");  // COMENTADO
          // Serial.println(WiFi.localIP());  // COMENTADO
      }

      delay(500);
      yield();  // Alimentar watchdog
  }

  // Comunicación por WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  // Serial.println("WebSocket iniciado en puerto 81");  // COMENTADO


  // Filtros -----------------------------------------------------------------------------------------
  float fs = 1.0 / (sampleIntervalUs / 1000000.0);
  float ts = 1.0 / fs;
  float rc_hp = 1.0 / (2.0 * PI * fc_hp);
  alpha_hp = rc_hp / (rc_hp + ts);
  float rc_lp = 1.0 / (2.0 * PI * fc_lp);
  alpha_lp = ts / (ts + rc_lp);

  // Inicializar buffer de HR ------------------------------------------------------------------------
  for (byte i = 0; i < RATE_SIZE; i++) {
    rates[i] = 70;
  }

}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CONEXIÓN DE SENSORES
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool checkSensorsConnected() {
    // Leer ID de sensores con manejo de errores I2C
    // Si hay error I2C, readPartID() puede fallar, así que usamos try-catch implícito
    bool s1_connected = false;
    bool s2_connected = false;
    
    // Intentar leer ID del sensor 1
    uint8_t partID1 = 0;
    partID1 = particleSensor1.readPartID();
    s1_connected = (partID1 == 0x15);
    
    // Intentar leer ID del sensor 2
    uint8_t partID2 = 0;
    partID2 = particleSensor2.readPartID();
    s2_connected = (partID2 == 0x15);

    // Si el sensor 1 estaba desconectado y ahora está conectado, inicializarlo
    if (!sensor1_ok && s1_connected) {
        // Limpiar buffers antes de reconectar para mantener sincronización
        clearAllBuffers();
        // Reinicializar bus I2C y sensor
        Wire.begin(21, 22);
        delay(10);  // Pequeño delay para estabilizar I2C
        particleSensor1.begin(Wire, I2C_SPEED_FAST);
        particleSensor1.setup(60, 1, 2, 50, 411, 4096);
        particleSensor1.setPulseAmplitudeRed(0x0A);
        particleSensor1.setPulseAmplitudeIR(0x2F);
        delay(10);  // Delay después de setup
        // Vaciar FIFO del sensor que se reconectó (con manejo de errores)
        for (int i = 0; i < 32; i++) {
          long val = particleSensor1.getIR();
          if (val < 0) break;  // Si hay error I2C, salir
        }
    }

    // Si el sensor 2 estaba desconectado y ahora está conectado, inicializarlo
    if (!sensor2_ok && s2_connected) {
        // Limpiar buffers antes de reconectar para mantener sincronización
        clearAllBuffers();
        // Reinicializar bus I2C y sensor
        Wire1.begin(18, 19);
        delay(10);  // Pequeño delay para estabilizar I2C
        particleSensor2.begin(Wire1, I2C_SPEED_FAST);
        particleSensor2.setup(60, 1, 2, 50, 411, 4096);
        particleSensor2.setPulseAmplitudeRed(0x0A);
        particleSensor2.setPulseAmplitudeIR(0x2F);
        delay(10);  // Delay después de setup
        // Vaciar FIFO del sensor que se reconectó (con manejo de errores)
        for (int i = 0; i < 32; i++) {
          long val = particleSensor2.getIR();
          if (val < 0) break;  // Si hay error I2C, salir
        }
    }

    // Actualizar estados
    sensor1_ok = s1_connected;
    sensor2_ok = s2_connected;

    // Mostrar mensajes en pantalla solo si algún sensor está desconectado
    if (!sensor1_ok && !sensor2_ok) {
        display.clear();
        display.setFont(ArialMT_Plain_10);
        display.drawString(15, 20, "Sensor 1 y Sensor 2");
        display.drawString(25, 35, "desconectados");
        display.display();
        return false;
    } 
    else if (!sensor1_ok) {
        display.clear();
        display.setFont(ArialMT_Plain_10);
        display.drawString(10, 25, "Sensor 1 desconectado");
        display.display();
        return false;
    } 
    else if (!sensor2_ok) {
        display.clear();
        display.setFont(ArialMT_Plain_10);
        display.drawString(10, 25, "Sensor 2 desconectado");
        display.display();
        return false;
    }

    return true;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LOOP PRINCIPAL
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {
  webSocket.loop();
  yield();  // Alimentar watchdog al inicio del loop
  
  // Chequeo conexión de sensores (reducir frecuencia para evitar errores I2C) ------------
  unsigned long currentTime = millis();
  if (currentTime - lastSensorCheck >= SENSOR_CHECK_INTERVAL) {
    lastSensorCheck = currentTime;
    if (!checkSensorsConnected()) {
        // No continuar con la medición hasta que los sensores estén conectados
        yield();
        delay(500);
        return;
    }
  } else {
    // Si no es momento de verificar, pero sabemos que algún sensor no está OK, salir
    if (!sensor1_ok || !sensor2_ok) {
      yield();
      delay(100);
      return;
    }
  }
  // Si ambos sensores están conectados, continúa con la lectura de las señales


  // Leer sensores -----------------------------------------------------------------------------------
  unsigned long currentTimeUs = micros();
  if (currentTimeUs - lastSampleTimeUs >= sampleIntervalUs) { // Asegura frecuencia de muestreo de 50hz
    lastSampleTimeUs += sampleIntervalUs;
    
    // Alimentar watchdog (evita reinicios por timeout)
    yield();
    
    // Verificar que los sensores estén conectados antes de leer (evita errores I2C NACK)
    if (!sensor1_ok || !sensor2_ok) {
      // Si algún sensor no está conectado, no intentar leer (evita errores I2C)
      yield();
      delay(100);
      return;
    }
    
    // Intentar leer sensores con manejo de errores
    long ir1 = 0;
    long ir2 = 0;
    bool read_ok = true;
    
    // Leer sensor 1 (proximal) - si falla, usar valor 0
    if (sensor1_ok) {
      ir1 = particleSensor1.getIR();
      // Si la lectura falla (valor inválido o error I2C), el sensor puede estar desconectado
      if (ir1 < 0) {
        read_ok = false;
        sensor1_ok = false;  // Marcar como desconectado
      }
    }
    
    // Leer sensor 2 (distal) - si falla, usar valor 0
    if (sensor2_ok) {
      ir2 = particleSensor2.getIR();
      // Si la lectura falla (valor inválido o error I2C), el sensor puede estar desconectado
      if (ir2 < 0) {
        read_ok = false;
        sensor2_ok = false;  // Marcar como desconectado
      }
    }
    
    // Si hubo error en la lectura, salir y esperar siguiente ciclo
    if (!read_ok) {
      clearAllBuffers();
      yield();  // Alimentar watchdog
      delay(100);
      return;
    }

    // Chequeo de sensores apoyados
    bool ir1_ok = ir1 >= IR_THRESHOLD;
    bool ir2_ok = ir2 >= IR_THRESHOLD;

    if (!ir1_ok || !ir2_ok) {
        // Limpiar TODOS los buffers y filtros para mantener sincronización
        // Esto asegura que cuando se reconecte, no haya datos desfasados
        clearAllBuffers();

        // Enviar JSON optimizado a la interfaz indicando qué sensores están apoyados
        // Usar snprintf para evitar fragmentación de memoria
        char json_buffer[80];
        snprintf(json_buffer, sizeof(json_buffer), 
                 "{\"s1\":%s,\"s2\":%s,\"p\":%.3f,\"d\":%.3f}",
                 ir1_ok ? "true" : "false",
                 ir2_ok ? "true" : "false",
                 ir1_lp_out, ir2_lp_out);

        webSocket.broadcastTXT(json_buffer);
        yield();  // Alimentar watchdog

        // Mostrar en pantalla
        if (millis() - lastDisplayUpdate > 1000) {
            display.clear();
            display.setFont(ArialMT_Plain_10);

            if (!ir1_ok && !ir2_ok) {
                display.drawString(15, 20, "Por favor, coloque");
                display.drawString(25, 35, "los sensores");

            } else if (!ir1_ok) {
                display.drawString(15, 20, "Por favor, coloque");
                display.drawString(25, 35, "el sensor 1");
            } else { // !ir2_ok
                display.drawString(15, 20, "Por favor, coloque");
                display.drawString(25, 35, "el sensor 2");
            }

            display.display();
            lastDisplayUpdate = millis();
        }
        yield();
        delay(100);
        return;      // No continuar con el cálculo de HR
    }

    // Si ambos sensores están colocados correctamente, continúa con el cálculo de HR


    // Filtros para HR
    irFiltered = alphaLow * ir1 + (1 - alphaLow) * irFiltered;
    irHighPass = irFiltered - ((1 - alphaHigh) * irHighPass + alphaHigh * irFiltered);

    // Detección de latidos
    if (checkForBeat(irHighPass)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      
      beatsPerMinute = 60 / (delta / 1000.0);
      
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {  // Solo considerar válidos los latidos dentro del rango fisiológico
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        
        // Calcular promedio
        int sum = 0;
        for (byte x = 0; x < RATE_SIZE; x++) sum += rates[x];
        beatAvg = sum / RATE_SIZE;
        
        validBeatsCount++;
        
        // Considerar estabilizado después de 10 latidos válidos
        if (validBeatsCount >= 10) {
          isStabilized = true;
        }
      }
    }

    // Actualizar display cada 500ms para no saturar el I2C
    if (millis() - lastDisplayUpdate > 500) {
      display.clear();
      
      if (!isStabilized) {
        // Mostrar en pantalla
        display.setFont(ArialMT_Plain_10);
        display.drawString(25, 20, "Procesando...");
        display.drawString(45, 35, String(validBeatsCount) + " / 10"); // Contador de latidos
      } else {
        display.setFont(ArialMT_Plain_24);
        display.drawString(30, 25, String(beatAvg)); // HR estabilizado
        display.setFont(ArialMT_Plain_10);
        display.drawString(65, 35, "BPM");
      }
      
      display.display();
      lastDisplayUpdate = millis();
    }

  
    // COMENTADO: Serial.print elimina delays que afectan sincronización
    // Serial.print(", BPM=");
    // Serial.print(beatsPerMinute);
    // Serial.print(", Avg BPM=");
    // Serial.print(beatAvg);
    // Serial.print(", Latidos=");
    // Serial.println(validBeatsCount);


    // Envío de datos ----------------------------------------------------------------------------------
    const float maxADC = 262143.0;
    float ir1_norm = (ir1 / maxADC) * 100.0; // Normalización
    float ir2_norm = (ir2 / maxADC) * 100.0;

    // Filtrado de señal
    ir1_hp_out = alpha_hp * (ir1_hp_out + ir1_norm - ir1_prev_in);
    ir1_lp_out = alpha_lp * ir1_hp_out + (1.0 - alpha_lp) * ir1_lp_out;
    ir1_prev_in = ir1_norm;

    ir2_hp_out = alpha_hp * (ir2_hp_out + ir2_norm - ir2_prev_in);
    ir2_lp_out = alpha_lp * ir2_hp_out + (1.0 - alpha_lp) * ir2_lp_out;
    ir2_prev_in = ir2_norm;

    // JSON optimizado: usar snprintf para evitar fragmentación de memoria
    // Formato compacto: reducir de 6 a 3 decimales (ahorra ~30 bytes por frame)
    char json_buffer[80];  // Buffer fijo para evitar fragmentación
    snprintf(json_buffer, sizeof(json_buffer), 
             "{\"s1\":true,\"s2\":true,\"p\":%.3f,\"d\":%.3f,\"hr\":%d}",
             ir1_lp_out, ir2_lp_out, beatAvg);

    webSocket.broadcastTXT(json_buffer);
    yield();  // Alimentar watchdog después de broadcast
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    // Serial.printf("Cliente conectado [%u]\n", num);  // COMENTADO
  } else if (type == WStype_DISCONNECTED) {
    // Serial.printf("Cliente desconectado [%u]\n", num);  // COMENTADO
  }
}