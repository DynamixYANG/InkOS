#include "Hardware.h"
#include "Domain.h"
#include <WiFi.h>
#include <algorithm>
#include <esp_timer.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
namespace ink {
namespace {
constexpr int EncoderA = 41, EncoderB = 42, BatteryAdc = 1, AdcEnable = 6;
// Capture both quadrature bits from one GPIO register read; sequential pin
// reads can combine states from two different edges during fast rotation.
inline __attribute__((always_inline)) uint8_t encoderState() {
  uint32_t pins = REG_READ(GPIO_IN1_REG);
  return (((pins >> (EncoderA-32)) & 1) << 1) | ((pins >> (EncoderB-32)) & 1);
}
EncoderInput encoder;
TaskHandle_t inputTaskHandle = nullptr;
portMUX_TYPE encoderLock = portMUX_INITIALIZER_UNLOCKED;
void ARDUINO_ISR_ATTR encoderEdge() {
  portENTER_CRITICAL_ISR(&encoderLock);
  bool moved = encoder.edge(encoderState(), esp_timer_get_time());
  portEXIT_CRITICAL_ISR(&encoderLock);
  BaseType_t wake = pdFALSE;
  if (moved && inputTaskHandle) vTaskNotifyGiveFromISR(inputTaskHandle, &wake);
  if (wake) portYIELD_FROM_ISR();
}
}
int wifiLevel(int rssi) { return rssi >= -60 ? 3 : rssi >= -72 ? 2 : 1; }
int batteryPercent(float v) {
  if (!isfinite(v) || v < 2.8f || v > 4.45f) return -1;
  // Approximate single-cell Li-ion open-circuit curve, not a coulomb counter.
  constexpr float volts[] = {3.2f,3.4f,3.6f,3.7f,3.75f,3.8f,3.85f,3.9f,4.0f,4.1f,4.2f};
  constexpr int percent[] = {0,3,8,15,25,40,55,65,80,90,100};
  if (v <= volts[0]) return 0;
  for (int i=1;i<11;i++) if (v <= volts[i])
    return int(lroundf(percent[i-1]+(v-volts[i-1])/(volts[i]-volts[i-1])*(percent[i]-percent[i-1])));
  return 100;
}
void beginHardware() {
  inputTaskHandle = xTaskGetCurrentTaskHandle();
  pinMode(EncoderA, INPUT_PULLUP); pinMode(EncoderB, INPUT_PULLUP);
  encoder.begin(encoderState());
  attachInterrupt(digitalPinToInterrupt(EncoderA),encoderEdge,CHANGE);
  attachInterrupt(digitalPinToInterrupt(EncoderB),encoderEdge,CHANGE);
  pinMode(AdcEnable,OUTPUT); digitalWrite(AdcEnable,LOW);
  analogReadResolution(12); analogSetPinAttenuation(BatteryAdc,ADC_11db);
}
int takeEncoderMovement() {
  portENTER_CRITICAL(&encoderLock); int steps=encoder.take(esp_timer_get_time()); portEXIT_CRITICAL(&encoderLock);
  return steps;
}
void lockEncoder(uint32_t milliseconds) {
  portENTER_CRITICAL(&encoderLock);
  encoder.lock(encoderState(), esp_timer_get_time(), milliseconds);
  portEXIT_CRITICAL(&encoderLock);
}
bool encoderLocked() {
  portENTER_CRITICAL(&encoderLock); bool locked=encoder.locked(esp_timer_get_time()); portEXIT_CRITICAL(&encoderLock);
  return locked;
}
void updateHardwareStatus() {
  static uint32_t lastSample = 0;
  if (lastSample && millis()-lastSample < 60000) return;
  lastSample = millis();
  digitalWrite(AdcEnable,HIGH); delay(10);
  uint32_t sum=0;
  for (int i=0;i<30;i++) { sum+=analogReadMilliVolts(BatteryAdc); delayMicroseconds(100); }
  digitalWrite(AdcEnable,LOW);
  // eFuse-calibrated ADC millivolts, 1:2 board divider (no uncalibrated 3.6 V scale).
  float voltage = float(sum)/30 * 2 / 1000;
  int percent=batteryPercent(voltage), rssi=WiFi.status()==WL_CONNECTED?WiFi.RSSI():-127;
  Guard g(modelMutex);
  bool changed=model->batteryPercent!=percent || wifiLevel(model->wifiRssi)!=wifiLevel(rssi);
  model->batteryVoltage=voltage;model->batteryPercent=percent;model->wifiRssi=rssi;
  if (changed) {model->revision++;invalidate();}
}
} // namespace ink
