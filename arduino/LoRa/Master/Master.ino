/* ---------------------------------------------------------------------
 * Grado Ingeniería Informática. Cuarto curso. Internet de las Cosas, IC
 * Grupo 41
 * Autores:
 *      - Wail Ben El Hassane Boudhar
 *      - Mohamed O. Haroun Zarkik
 *
 * NODO MAESTRO
 * Inicio con Máximo Alcance + Optimización Progresiva
 * Payload BINARIO desde el esclavo
 * ---------------------------------------------------------------------
 */

#include <SPI.h>
#include <LoRa.h>
#include <Arduino_PMIC.h>
#include <Wire.h>
#include <RTCZero.h>

#include "globals.h"

/* ===================== CONFIG ===================== */
#define TX_LAPSE_MS 5000
#define ADJUSTMENT_SAMPLES 5
#define CONFIG_WAIT_TIME 4000

/* ===================== MESSAGE TYPES ===================== */
#define MSG_TYPE_DATA        0x01
#define MSG_TYPE_ECHO        0x02
#define MSG_TYPE_CONFIG      0x03
#define MSG_TYPE_CONFIG_ACK  0x04
#define MSG_TYPE_SENSOR      0x05

/* ===================== SENSOR PROTOCOL ===================== */
#define SENSOR_ID_ULTRA_01        0x01
#define SENSOR_TYPE_ULTRASONIC   0x01

#define DIST_STATE_ERROR   0x00
#define DIST_STATE_CLOSE   0x01
#define DIST_STATE_MEDIUM  0x02
#define DIST_STATE_FAR     0x03

/* ===================== ADDRESSES ===================== */
const uint8_t localAddress = 0xAA;
uint8_t destination = 0xBB;

/* ===================== LORA STATE ===================== */
volatile bool txDoneFlag = true;
volatile bool transmitting = false;

/* ===================== QUALITY THRESHOLDS ===================== */
#define RSSI_EXCELLENT -50
#define RSSI_GOOD      -70
#define RSSI_FAIR      -90
#define RSSI_POOR      -110

#define SNR_EXCELLENT  12.0
#define SNR_GOOD       8.0
#define SNR_FAIR       3.0
#define SNR_POOR      -3.0

/* ===================== DYNAMIC PARAMETERS ===================== */
uint8_t currentSF = 12;
const uint8_t minSF = 7;
const uint8_t maxSF = 12;

uint8_t currentBW = 7;
const uint8_t minBW = 6;
const uint8_t maxBW = 9;

uint8_t currentCR = 8;
const uint8_t minCR = 5;
const uint8_t maxCR = 8;

uint8_t currentPower = 20;
const uint8_t minPower = 2;
const uint8_t maxPower = 20;

/* Pending configuration */
uint8_t pendingSF, pendingBW, pendingCR, pendingPower;
bool configPending = false;
uint32_t configSentTime = 0;

double bandwidth_kHz[10] = {
  7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
  41.7E3, 62.5E3, 125E3, 250E3, 500E3
};

/* ===================== METRICS ===================== */
struct SignalMetrics {
  int rssi_sum;
  float snr_sum;
  uint8_t samples;
} slaveMetrics = {0, 0.0, 0};

uint32_t txStartTime = 0;
uint32_t lastSuccessfulRx = 0;
uint16_t successfulPackets = 0;
uint8_t consecutiveFails = 0;
bool linkEstablished = false;

/* ===================== STATE ===================== */
enum State {
  STATE_NORMAL,
  STATE_WAITING_CONFIG_ACK
};
State currentState = STATE_NORMAL;

/* ===================== RTC ===================== */
RTCZero rtc;

/* ===================== APPLY CONFIG ===================== */
void applyConfiguration() {
  LoRa.idle();
  delay(50);

  LoRa.setSignalBandwidth(long(bandwidth_kHz[currentBW]));
  LoRa.setSpreadingFactor(currentSF);
  LoRa.setCodingRate4(currentCR);
  LoRa.setTxPower(currentPower, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setPreambleLength(currentSF >= 11 ? 16 : 8);
  LoRa.enableCrc();
  LoRa.setSyncWord(0x12);

  LoRa.receive();

  Serial.println("\nCONFIG APLICADA:");
  Serial.println(" SF: " + String(currentSF));
  Serial.println(" BW: " + String(bandwidth_kHz[currentBW] / 1000.0) + " kHz");
  Serial.println(" CR: 4/" + String(currentCR));
  Serial.println(" Power: " + String(currentPower) + " dBm\n");
}

/* ===================== SEND MESSAGE ===================== */
void sendMessage(uint8_t *payload, uint8_t length, uint16_t id) {
  while (!LoRa.beginPacket()) delay(5);

  LoRa.write(destination);
  LoRa.write(localAddress);
  LoRa.write(id >> 8);
  LoRa.write(id & 0xFF);
  LoRa.write(length);
  LoRa.write(payload, length);

  LoRa.endPacket(true);
}

/* ===================== QUALITY ===================== */
String evaluateSignalQuality(int rssi, float snr) {
  if (rssi > RSSI_EXCELLENT && snr > SNR_EXCELLENT) return "EXCELENTE";
  if (rssi > RSSI_GOOD && snr > SNR_GOOD) return "BUENA";
  if (rssi > RSSI_FAIR && snr > SNR_FAIR) return "ACEPTABLE";
  if (rssi > RSSI_POOR && snr > SNR_POOR) return "POBRE";
  return "MUY POBRE";
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (!LoRa.begin(868E6)) {
    Serial.println("Error LoRa");
    while (1);
  }

  applyConfiguration();

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(TxFinished);
  LoRa.receive();

  Serial.println("MAESTRO LISTO (BINARIO)");
}

/* ===================== LOOP ===================== */
void loop() {
  static uint32_t lastSendTime = 0;
  static uint16_t msgCount = 0;

  if (currentState == STATE_NORMAL &&
      !transmitting &&
      (millis() - lastSendTime) > TX_LAPSE_MS) {

    uint8_t payload[8];
    uint8_t len = 0;

    payload[len++] = MSG_TYPE_DATA;
    payload[len++] = 'M';
    payload[len++] = '0' + (msgCount % 10);

    transmitting = true;
    txDoneFlag = false;
    txStartTime = millis();

    sendMessage(payload, len, msgCount++);

    lastSendTime = millis();
  }

  if (transmitting && txDoneFlag) {
    transmitting = false;
    LoRa.receive();
  }
}

/* ===================== RECEIVE ===================== */
void onReceive(int size) {
  if (size == 0) return;

  uint32_t rxTime = millis();

  LoRa.read(); // recipient
  LoRa.read(); // sender
  uint16_t msgId = (LoRa.read() << 8) | LoRa.read();
  uint8_t length = LoRa.read();

  uint8_t buffer[32];
  for (uint8_t i = 0; i < length && i < sizeof(buffer); i++)
    buffer[i] = LoRa.read();

  uint8_t msgType = buffer[0];

  /* ===== SENSOR DATA (BINARY) ===== */
  if (msgType == MSG_TYPE_SENSOR && length >= 6) {

    uint8_t sensorId = buffer[1];
    uint16_t distanceCm = (buffer[3] << 8) | buffer[4];
    uint8_t state = buffer[5];

    String stateStr =
      (state == DIST_STATE_CLOSE)  ? "Cerca"  :
      (state == DIST_STATE_MEDIUM) ? "Medio"  :
      (state == DIST_STATE_FAR)    ? "Lejos"  :
                                     "Error";

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   SENSOR ULTRASÓNICO (BINARIO)         ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println("Sensor ID: " + String(sensorId));
    Serial.println("Distancia: " + String(distanceCm) + " cm");
    Serial.println("Estado: " + stateStr);
    Serial.println("RSSI: " + String(rssi) + " dBm");
    Serial.println("SNR: " + String(snr) + " dB");
    Serial.println("════════════════════════════════════════\n");

    serialbridge_report_ultrasonic_sensor_data(
      sensorId, msgId, distanceCm, stateStr.c_str()
    );

    lastSuccessfulRx = rxTime;
    successfulPackets++;
    return;
  }
}

/* ===================== TX DONE ===================== */
void TxFinished() {
  txDoneFlag = true;
}