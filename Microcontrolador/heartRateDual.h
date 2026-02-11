/*
 * heartRateDual.h
 * Detección de latidos adaptativa para S1 (carótida)
 * Basado en algoritmo SparkFun pero con variables independientes
 * para no interferir con la detección de S2 (muñeca)
 */

#ifndef HEARTRATE_DUAL_H
#define HEARTRATE_DUAL_H

// ========== DETECTOR S1 (Carótida) - Variables independientes ==========
static long lastBeatS1 = 0;
static long IR_AC_Max_S1 = 20;
static long IR_AC_Min_S1 = -20;
static long IR_AC_Signal_Current_S1 = 0;
static long IR_AC_Signal_Previous_S1 = 0;
static long IR_Average_Estimated_S1 = 0;
static int positiveEdge_S1 = 0;
static int negativeEdge_S1 = 0;
static long ir_avg_reg_S1 = 0;

static const int MIN_THRESHOLD_S1 = 20000; // Umbral mínimo de señal

/*
 * checkForBeatS1() - Detector para sensor S1 (carótida) con variables independientes
 * 
 * @param sample: Valor IR RAW del sensor PPG
 * @return: true si detectó un latido, false si no
 * 
 * Algoritmo idéntico al de S2 pero con estado separado
 */
boolean checkForBeatS1(long sample)
{
  boolean beatDetected = false;

  // Guardar valor anterior
  IR_AC_Signal_Previous_S1 = IR_AC_Signal_Current_S1;
  IR_AC_Signal_Current_S1 = sample;

  // Remover componente DC (promedio móvil lento)
  IR_Average_Estimated_S1 = (ir_avg_reg_S1 / 16);
  ir_avg_reg_S1 += sample;
  ir_avg_reg_S1 -= IR_Average_Estimated_S1;

  // Señal AC = IR actual - promedio DC
  long IR_AC = sample - IR_Average_Estimated_S1;

  // Actualizar máximos y mínimos con decaimiento
  if (IR_AC > IR_AC_Max_S1) IR_AC_Max_S1 = IR_AC;
  if (IR_AC < IR_AC_Min_S1) IR_AC_Min_S1 = IR_AC;

  // Decaer lentamente máximos y mínimos (adaptación)
  IR_AC_Max_S1 -= (IR_AC_Max_S1 / 32);
  IR_AC_Min_S1 += (15 - IR_AC_Min_S1 / 32);

  // Calcular umbral dinámico (punto medio)
  long IR_AC_Threshold = (IR_AC_Max_S1 + IR_AC_Min_S1) / 2;

  // Detectar flancos (cruces del umbral)
  if (IR_AC > IR_AC_Threshold) {
    positiveEdge_S1 = 1;
    negativeEdge_S1 = 0;
  }
  
  if (IR_AC < IR_AC_Threshold) {
    positiveEdge_S1 = 0;
    negativeEdge_S1 = 1;
  }

  // Detectar latido en flanco positivo
  if (positiveEdge_S1 && !negativeEdge_S1) {
    // Validar amplitud mínima
    long IR_amplitude = IR_AC_Max_S1 - IR_AC_Min_S1;
    
    if (IR_amplitude > MIN_THRESHOLD_S1) {
      // Periodo refractario: 300ms (200 BPM max)
      long now = millis();
      if ((now - lastBeatS1) > 300) {
        beatDetected = true;
        lastBeatS1 = now;
        
        // Reset detección de flancos para próximo ciclo
        positiveEdge_S1 = 0;
        negativeEdge_S1 = 0;
      }
    }
  }

  return beatDetected;
}

#endif
