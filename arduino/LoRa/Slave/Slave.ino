/* ---------------------------------------------------------------------
 * Grado Ingeniería Informática. Cuarto curso. Internet de las Cosas, IC
 * Grupo 41
 * Autores:
 *      - Wail Ben El Hassane Boudhar
 *      - Mohamed O. Haroun Zarkik
 *
 * NODO ESCLAVO
 * LoRa Largo Alcance + Sensor Ultrasónico SRF02
 * Payload BINARIO (compacto, sin JSON)
 * ---------------------------------------------------------------------
 */

#include <SPI.h>
#include <LoRa.h>
#include <Arduino_PMIC.h>
#include <Wire.h>

/* ===================== MENSAJES ===================== */
#define MSG_TYPE_DATA        0x01
#define MSG_TYPE_ECHO        0x02
#define MSG_TYPE_CONFIG      0x03
#define MSG_TYPE_CONFIG_ACK  0x04
#define MSG_TYPE_SENSOR      0x05

/* ===================== SENSOR IDS ===================== */
#define SENSOR_ID_ULTRA_01        0x01
#define SENSOR_TYPE_ULTRASONIC   0x01

#define DIST_STATE_ERROR   0x00
#define DIST_STATE_CLOSE   0x01
#define DIST_STATE_MEDIUM  0x02
#define DIST_STATE_FAR     0x03

/* ===================== SRF02 ===================== */
const uint8_t SRF02_ADDR = 0x71;
const uint8_t SRF02_CMD_REG = 0x00;
const uint8_t SRF02_RESULT_REG = 0x02;
const uint8_t SRF02_RANGING_CM = 0x51;
const uint16_t SRF02_MEASUREMENT_DELAY = 70;

/* ===================== LORA ===================== */
const uint8_t localAddress = 0xBB;
uint8_t destination = 0xAA;

volatile bool txDoneFlag = true;
volatile bool transmitting = false;

/* ----------- PARÁMETROS DINÁMICOS ----------- */
uint8_t currentSF = 12;
uint8_t currentBW = 7;
uint8_t currentCR = 8;
uint8_t currentPower = 20;

uint8_t newSF, newBW, newCR, newPower;

double bandwidth_kHz[10] = {
  7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
  41.7E3, 62.5E3, 125E3, 250E3, 500E3
};

/* ===================== ESTADO ===================== */
uint32_t lastSensorReadTime = 0;
uint16_t sensorReadings = 0;
uint16_t packetsReceived = 0;
uint16_t echosSent = 0;

float lastDistance = -1;
uint8_t lastDistanceState = DIST_STATE_ERROR;

String lastReceivedMsg = "";
int lastRSSI = 0;
float lastSNR = 0;

const uint16_t SENSOR_READ_INTERVAL = 5000;

enum SlaveState {
  STATE_NORMAL,
  STATE_APPLYING_CONFIG,
  STATE_READING_SENSOR,
  STATE_SENDING_SENSOR_DATA
};

SlaveState currentState = STATE_NORMAL;

/* ===================== SENSOR ===================== */
bool sensorDetect() {
  Wire.beginTransmission(SRF02_ADDR);
  return (Wire.endTransmission() == 0);
}

float readSRF02Distance() {
  Wire.beginTransmission(SRF02_ADDR);
  Wire.write(SRF02_CMD_REG);
  Wire.write(SRF02_RANGING_CM);
  if (Wire.endTransmission() != 0) return -1;

  delay(SRF02_MEASUREMENT_DELAY);

  Wire.beginTransmission(SRF02_ADDR);
  Wire.write(SRF02_RESULT_REG);
  Wire.endTransmission();

  Wire.requestFrom(SRF02_ADDR, (uint8_t)2);
  if (Wire.available() < 2) return -1;

  uint16_t value = (Wire.read() << 8) | Wire.read();
  return (float)value;
}

uint8_t getDistanceStateCode(float d) {
  if (d < 0)   return DIST_STATE_ERROR;
  if (d < 30)  return DIST_STATE_CLOSE;
  if (d < 100) return DIST_STATE_MEDIUM;
  return DIST_STATE_FAR;
}

/* ===================== LORA ===================== */
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

  Serial.println("Configuración aplicada");
}

void sendMessage(uint8_t* payload, uint8_t length, uint16_t id) {
  while (!LoRa.beginPacket()) delay(5);

  LoRa.write(destination);
  LoRa.write(localAddress);
  LoRa.write(id >> 8);
  LoRa.write(id & 0xFF);
  LoRa.write(length);
  LoRa.write(payload, length);

  LoRa.endPacket(true);
}

void sendSensorData() {
  if (transmitting) return;

  uint16_t distanceCm = (lastDistance < 0) ? 0 : (uint16_t)lastDistance;
  uint8_t state = getDistanceStateCode(lastDistance);

  uint8_t payload[6];
  uint8_t len = 0;

  payload[len++] = MSG_TYPE_SENSOR;
  payload[len++] = SENSOR_ID_ULTRA_01;
  payload[len++] = SENSOR_TYPE_ULTRASONIC;
  payload[len++] = distanceCm >> 8;
  payload[len++] = distanceCm & 0xFF;
  payload[len++] = state;

  transmitting = true;
  txDoneFlag = false;

  Serial.print("→ TX SENSOR: ");
  Serial.print(distanceCm);
  Serial.print(" cm | state ");
  Serial.println(state);

  sendMessage(payload, len, sensorReadings++);
}

/* ===================== CALLBACKS ===================== */
void TxFinished() {
  txDoneFlag = true;
}

void onReceive(int size) {
  if (size == 0) return;

  LoRa.read(); // recipient
  LoRa.read(); // sender
  uint16_t msgId = (LoRa.read() << 8) | LoRa.read();
  uint8_t length = LoRa.read();

  uint8_t buffer[32];
  for (uint8_t i = 0; i < length && i < sizeof(buffer); i++)
    buffer[i] = LoRa.read();

  uint8_t type = buffer[0];

  if (type == MSG_TYPE_CONFIG) {
    newSF = buffer[1];
    newBW = buffer[2];
    newCR = buffer[3];
    newPower = buffer[4];

    uint8_t ack = MSG_TYPE_CONFIG_ACK;
    sendMessage(&ack, 1, 0xFFFF);

    currentSF = newSF;
    currentBW = newBW;
    currentCR = newCR;
    currentPower = newPower;

    applyConfiguration();
  }

  if (type == MSG_TYPE_DATA) {
    lastRSSI = LoRa.packetRssi();
    lastSNR = LoRa.packetSnr();

    lastReceivedMsg = "";
    for (uint8_t i = 1; i < length; i++)
      lastReceivedMsg += (char)buffer[i];

    packetsReceived++;
  }

  if (type == MSG_TYPE_DATA) {
  lastRSSI = LoRa.packetRssi();
  lastSNR = LoRa.packetSnr();

  lastReceivedMsg = "";
  for (uint8_t i = 1; i < length; i++)
    lastReceivedMsg += (char)buffer[i];

  packetsReceived++;
  
  // ADD THIS: Trigger sensor reading and response
  if (!transmitting) {
    lastDistance = readSRF02Distance();
    lastDistanceState = getDistanceStateCode(lastDistance);
    sendSensorData();
  }
}
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);
  while (!Serial);

  Wire.begin();
  Serial.print("SRF02: ");
  Serial.println(sensorDetect() ? "OK" : "NO DETECTADO");

  if (!LoRa.begin(868E6)) {
    Serial.println("Error LoRa");
    while (1);
  }

  applyConfiguration();
  LoRa.onReceive(onReceive);
  LoRa.onTxDone(TxFinished);
  LoRa.receive();

  lastSensorReadTime = millis();
  Serial.println("Esclavo listo (payload binario)");
}

/* ===================== LOOP ===================== */
void loop() {
  if (!transmitting && (millis() - lastSensorReadTime) > SENSOR_READ_INTERVAL) {
    lastSensorReadTime = millis();

    lastDistance = readSRF02Distance();
    lastDistanceState = getDistanceStateCode(lastDistance);

    sendSensorData();
  }

  if (transmitting && txDoneFlag) {
    transmitting = false;
    LoRa.receive();
  }
}
