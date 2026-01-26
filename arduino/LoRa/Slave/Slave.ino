/* ---------------------------------------------------------------------
 * NODO ESCLAVO - ULTRASONIDO (SRF02) - FIXED
 * ---------------------------------------------------------------------
 */
#include <SPI.h>
#include <LoRa.h>
#include <Arduino_PMIC.h>
#include <Wire.h>

// ========== PROTOCOLO BINARIO ==========
#define MSG_TYPE_SENSOR 0x05
#define SENSOR_ID_ULTRA 0x01
#define SENSOR_TYPE_DIST 0x01

#define DIST_STATE_ERROR   0x00
#define DIST_STATE_CLOSE   0x01
#define DIST_STATE_MEDIUM  0x02
#define DIST_STATE_FAR     0x03

// ========== HARDWARE ==========
const uint8_t SRF02_ADDR = 0x71; 
const uint8_t SRF02_CMD_REG = 0x00;
const uint8_t SRF02_RANGING_CM = 0x51;
const uint16_t SRF02_MEASUREMENT_DELAY = 70;

const uint8_t localAddress = 0xBB;
const uint8_t destination = 0xAA;

volatile bool txDoneFlag = true;
volatile bool transmitting = false;
uint16_t msgCount = 0;

// Configuración LoRa (Igual al Maestro)
uint8_t currentSF = 12;
uint8_t currentBW = 7;
uint8_t currentCR = 8;
uint8_t currentPower = 20;

double bandwidth_kHz[10] = {
  7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
  41.7E3, 62.5E3, 125E3, 250E3, 500E3
};

const uint16_t SENSOR_READ_INTERVAL = 5000;
uint32_t lastSensorReadTime = 0;

void setup() {
  Serial.begin(115200);
  // REMOVED: while(!Serial);  <-- CAUSA DEL BLOQUEO
  delay(2000); 

  Wire.begin();
  
  if (!LoRa.begin(868E6)) {
    Serial.println("Error LoRa");
    while(1);
  }

  LoRa.setSignalBandwidth(long(bandwidth_kHz[currentBW]));
  LoRa.setSpreadingFactor(currentSF);
  LoRa.setCodingRate4(currentCR);
  LoRa.setTxPower(currentPower, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setPreambleLength(16);
  LoRa.enableCrc();
  LoRa.setSyncWord(0x12);
  LoRa.onTxDone(TxFinished);
  LoRa.receive(); // Put in receive mode (standard standby)

  Serial.println("ESCLAVO ULTRASONICO (BINARIO) LISTO");
  
  // Force immediate read
  lastSensorReadTime = millis() - SENSOR_READ_INTERVAL;
}

float readSRF02Distance() {
  Wire.beginTransmission(SRF02_ADDR);
  Wire.write(SRF02_CMD_REG);
  Wire.write(SRF02_RANGING_CM);
  if (Wire.endTransmission() != 0) return -1.0;

  delay(SRF02_MEASUREMENT_DELAY);

  Wire.beginTransmission(SRF02_ADDR);
  Wire.write(0x02); // Result Register
  Wire.endTransmission();

  Wire.requestFrom(SRF02_ADDR, (uint8_t)2);
  if (Wire.available() < 2) return -1.0;

  byte highByte = Wire.read();
  byte lowByte = Wire.read();
  return (float)((highByte << 8) | lowByte);
}

void loop() {
  if (!transmitting && (millis() - lastSensorReadTime) >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = millis();
    
    float distVal = readSRF02Distance();
    int distInt = (int)distVal;

    uint8_t state;
    if (distInt < 0) state = DIST_STATE_ERROR;
    else if (distInt < 30) state = DIST_STATE_CLOSE;
    else if (distInt < 100) state = DIST_STATE_MEDIUM;
    else state = DIST_STATE_FAR;

    if(distInt < 0) distInt = 0; 

    uint8_t payload[6];
    payload[0] = MSG_TYPE_SENSOR;
    payload[1] = SENSOR_ID_ULTRA; // 0x01
    payload[2] = SENSOR_TYPE_DIST;
    payload[3] = (distInt >> 8) & 0xFF;
    payload[4] = distInt & 0xFF;
    payload[5] = state;

    Serial.println("TX Ultra: " + String(distInt) + " cm");

    transmitting = true;
    txDoneFlag = false;
    sendMessage(payload, 6, msgCount++);
  }

  if (transmitting && txDoneFlag) {
    transmitting = false;
    LoRa.receive();
  }
}

void sendMessage(uint8_t* payload, uint8_t length, uint16_t id) {
  while(!LoRa.beginPacket()) delay(5);
  LoRa.write(destination);
  LoRa.write(localAddress);
  LoRa.write((uint8_t)(id >> 8));
  LoRa.write((uint8_t)(id & 0xFF));
  LoRa.write(length);
  LoRa.write(payload, length);
  LoRa.endPacket(true);
}

void TxFinished() {
  txDoneFlag = true;
}