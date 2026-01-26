/* ---------------------------------------------------------------------
 * Grado Ingeniería Informática. Cuarto curso. Internet de las Cosas, IC
 * Grupo 41
 * Autores:
 *      - Wail Ben El Hassane Boudhar
 *      - Mohamed O. Haroun Zarkik
 * NODO ESCLAVO - Modo Largo Alcance + Sensor Ultrasónico SRF02
 * ---------------------------------------------------------------------
*/

#include <SPI.h>
#include <LoRa.h>
#include <Arduino_PMIC.h>
#include <Wire.h>
#include <ArduinoJson.h>

#define MSG_TYPE_DATA 0x01
#define MSG_TYPE_ECHO 0x02
#define MSG_TYPE_CONFIG 0x03
#define MSG_TYPE_CONFIG_ACK 0x04
#define MSG_TYPE_SENSOR 0x05

// ========== CONFIGURACIÓN SRF02 ==========
// Dirección I2C del sensor SRF02 (7-bit)
// Datasheet usa 0xE2 (8-bit), Arduino Wire usa 0x71 (7-bit)
const uint8_t SRF02_ADDR = 0x71;
const uint8_t SRF02_CMD_REG = 0x00;
const uint8_t SRF02_RESULT_REG = 0x02;
const uint8_t SRF02_RANGING_CM = 0x51;  // Resultado en centímetros
const uint16_t SRF02_MEASUREMENT_DELAY = 70;  // Tiempo máximo de medición (datasheet: 65ms)

// ========== CONFIGURACIÓN LORA ==========
const uint8_t localAddress = 0xBB;
uint8_t destination = 0xAA;

volatile bool txDoneFlag = true;
volatile bool transmitting = false;

// ------------ PARÁMETROS DINÁMICOS LORA -----------
uint8_t currentSF = 12;
uint8_t currentBW = 7;
uint8_t currentCR = 8;
uint8_t currentPower = 20;

uint8_t newSF = 0;
uint8_t newBW = 0;
uint8_t newCR = 0;
uint8_t newPower = 0;
bool configReceived = false;

double bandwidth_kHz[10] = {
  7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
  41.7E3, 62.5E3, 125E3, 250E3, 500E3
};

// ========== MÉTRICAS Y ESTADO ==========
uint32_t lastRxTime = 0;
uint32_t txStartTime = 0;
uint32_t lastSensorReadTime = 0;
uint16_t packetsReceived = 0;
uint16_t echosSent = 0;
uint16_t sensorReadings = 0;

String lastReceivedMsg = "";
int lastRSSI = 0;
float lastSNR = 0;

// Datos del sensor
float lastDistance = 0.0;
String lastDistanceState = "Lejos";

// Sensor reading interval (ms)
const uint16_t SENSOR_READ_INTERVAL = 5000;

enum SlaveState {
  STATE_NORMAL,
  STATE_SENDING_ACK,
  STATE_APPLYING_CONFIG,
  STATE_READING_SENSOR,
  STATE_SENDING_SENSOR_DATA
};
SlaveState currentState = STATE_NORMAL;

// ========== FUNCIONES SENSOR SRF02 ==========

/**
 * Detecta si el sensor está disponible en la dirección I2C
 */
bool sensorDetect() {
  Wire.beginTransmission(SRF02_ADDR);
  byte error = Wire.endTransmission();
  return (error == 0);
}

/**
 * Lee la distancia del sensor SRF02
 * Retorna la distancia en cm, o -1 si hay error
 */
float readSRF02Distance() {
  // 1. Enviar comando de medición
  Wire.beginTransmission(SRF02_ADDR);
  Wire.write(SRF02_CMD_REG);
  Wire.write(SRF02_RANGING_CM);
  byte error = Wire.endTransmission();

  if (error != 0) {
    Serial.println("  ✗ SRF02: Error enviando comando");
    return -1.0;
  }

  // 2. Esperar a que termine la medición
  delay(SRF02_MEASUREMENT_DELAY);

  // 3. Leer el resultado (2 bytes: High y Low)
  Wire.beginTransmission(SRF02_ADDR);
  Wire.write(SRF02_RESULT_REG);
  Wire.endTransmission();

  Wire.requestFrom(SRF02_ADDR, (uint8_t)2);

  if (Wire.available() < 2) {
    Serial.println("  ✗ SRF02: No hay datos disponibles");
    return -1.0;
  }

  byte highByte = Wire.read();
  byte lowByte = Wire.read();

  // Combinar bytes
  int distanceCm = (highByte << 8) | lowByte;

  return (float)distanceCm;
}

/**
 * Determina el estado de distancia basado en el valor
 */
String getDistanceState(float distance) {
  if (distance < 0) return "Error";
  if (distance < 30) return "Cerca";
  if (distance < 100) return "Medio";
  return "Lejos";
}

/**
 * Envía datos del sensor al maestro en formato JSON
 */
void sendSensorData() {
  if (transmitting) {
    Serial.println("  ⚠ Transmisión en progreso, saltando envío de sensor");
    return;
  }

  StaticJsonDocument<256> doc;
  doc["node_id"] = "ULTRA_01";
  doc["msg_type"] = "sensor_data";
  doc["timestamp"] = millis() / 1000;
  doc["msg_id"] = sensorReadings;
  doc["sensor_type"] = "ultrasonic";

  JsonObject data = doc.createNestedObject("data");
  data["distance_cm"] = lastDistance;
  data["estado"] = lastDistanceState;

  String jsonString;
  serializeJson(doc, jsonString);

  // Convertir JSON a buffer para transmisión
  uint8_t payload[200];
  uint8_t len = 0;

  payload[len++] = MSG_TYPE_SENSOR;

  // Copiar JSON al payload
  for (int i = 0; i < jsonString.length() && len < 199; i++) {
    payload[len++] = jsonString.charAt(i);
  }

  transmitting = true;
  txDoneFlag = false;
  txStartTime = millis();

  Serial.println("→ Enviando datos de sensor #" + String(sensorReadings));
  Serial.println("  JSON: " + jsonString);

  sendMessage(payload, len, sensorReadings);

  sensorReadings++;
}

// ========== FUNCIONES LORA ==========

void applyConfiguration() {
  LoRa.idle();
  delay(50);

  LoRa.setSignalBandwidth(long(bandwidth_kHz[currentBW]));
  LoRa.setSpreadingFactor(currentSF);
  LoRa.setCodingRate4(currentCR);
  LoRa.setTxPower(currentPower, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setPreambleLength(currentSF >= 11 ? 16 : (currentSF >= 9 ? 12 : 8));
  LoRa.enableCrc();
  LoRa.setSyncWord(0x12);

  delay(50);
  LoRa.receive();

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   CONFIGURACIÓN SINCRONIZADA          ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("  SF: " + String(currentSF));
  Serial.println("  BW: " + String(bandwidth_kHz[currentBW]/1000.0) + " kHz");
  Serial.println("  CR: 4/" + String(currentCR));
  Serial.println("  Power: " + String(currentPower) + " dBm");

  float rangeKm = 0.5;
  if(currentSF >= 12 && currentPower >= 17) rangeKm = 15.0;
  else if(currentSF >= 11) rangeKm = 8.0;
  else if(currentSF >= 10) rangeKm = 4.0;
  else if(currentSF >= 9) rangeKm = 2.0;
  else if(currentSF >= 8) rangeKm = 1.0;

  Serial.println("  Alcance estimado: ~" + String(rangeKm) + " km");
  Serial.println("========================================\n");
}

void sendConfigAck() {
  Serial.println("→ Enviando ACK de configuración...");

  uint8_t payload[1];
  payload[0] = MSG_TYPE_CONFIG_ACK;

  transmitting = true;
  txDoneFlag = false;

  sendMessage(payload, 1, 0xFFFE);

  currentState = STATE_APPLYING_CONFIG;
}

String evaluateSignalQuality(int rssi, float snr) {
  if(rssi > -50 && snr > 12) return "EXCELENTE";
  if(rssi > -70 && snr > 8) return "BUENA";
  if(rssi > -90 && snr > 3) return "ACEPTABLE";
  if(rssi > -110 && snr > -3) return "POBRE";
  return "MUY POBRE";
}

void setup() {
  Serial.begin(115200);
  while(!Serial);

  // ========== INICIALIZAR I2C ==========
  Serial.println("\nInicializando I2C...");
  Wire.begin();
  delay(100);

  // Detectar sensor
  Serial.print("Detectando SRF02 en 0x71... ");
  if (sensorDetect()) {
    Serial.println("✓ Sensor detectado");
  } else {
    Serial.println("✗ Error: Sensor no detectado");
    Serial.println("Revisa conexiones SDA/SCL y alimentación");
    // Continuar de todos modos para no bloquear LoRa
  }

  // ========== INICIALIZAR LORA ==========
  Serial.println("\nInicializando LoRa...");
  if (!LoRa.begin(868E6)) {
    Serial.println("Error LoRa");
    while(1);
  }

  applyConfiguration();

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(TxFinished);
  LoRa.receive();

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   ESCLAVO + SENSOR ULTRASÓNICO        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("LoRa: SF:12 BW:125kHz CR:4/8 Power:20dBm");
  Serial.println("SRF02: Adquiriendo lecturas cada 5s");
  Serial.println("Esperando comandos del maestro...\n");

  lastSensorReadTime = millis();
}

void loop() {
  // Aplicar configuración pendiente
  if(currentState == STATE_APPLYING_CONFIG && !transmitting) {
    delay(500);

    Serial.println("→ Aplicando nueva configuración...");
    currentSF = newSF;
    currentBW = newBW;
    currentCR = newCR;
    currentPower = newPower;
    applyConfiguration();

    currentState = STATE_NORMAL;
    configReceived = false;
  }

  // Lectura periódica del sensor
  if (currentState == STATE_NORMAL && !transmitting && 
      (millis() - lastSensorReadTime) >= SENSOR_READ_INTERVAL) {
    
    currentState = STATE_READING_SENSOR;
    lastSensorReadTime = millis();
    
    Serial.println("\n======== LECTURA SENSOR SRF02 ========");
    lastDistance = readSRF02Distance();
    lastDistanceState = getDistanceState(lastDistance);
    
    if (lastDistance >= 0) {
      Serial.println("✓ Distancia: " + String(lastDistance) + " cm");
      Serial.println("  Estado: " + lastDistanceState);
    } else {
      Serial.println("✗ Error en la lectura del sensor");
    }
    Serial.println("=====================================");
    
    currentState = STATE_SENDING_SENSOR_DATA;
  }

  // Enviar datos del sensor
  if (currentState == STATE_SENDING_SENSOR_DATA && !transmitting) {
    sendSensorData();
    currentState = STATE_NORMAL;
  }

  // Enviar eco si hay mensaje pendiente
  if (currentState == STATE_NORMAL && !transmitting && lastReceivedMsg.length() > 0) {
    uint8_t payload[50];
    uint8_t len = 0;

    payload[len++] = MSG_TYPE_ECHO;
    payload[len++] = (uint8_t)lastRSSI;
    payload[len++] = (uint8_t)lastSNR;

    for(int i = 0; i < lastReceivedMsg.length() && len < 48; i++) {
      payload[len++] = lastReceivedMsg.charAt(i);
    }

    transmitting = true;
    txDoneFlag = false;
    txStartTime = millis();

    sendMessage(payload, len, echosSent);

    Serial.println("→ ECO #" + String(echosSent) + " | " + lastReceivedMsg +
                   " | RSSI:" + String(lastRSSI) + " SNR:" + String(lastSNR));

    echosSent++;
    lastReceivedMsg = "";
  }

  if (transmitting && txDoneFlag) {
    uint32_t txDuration = millis() - txStartTime;
    Serial.println("  ✓ Enviado en " + String(txDuration) + " ms\n");
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

void onReceive(int size) {
  if (transmitting && !txDoneFlag) txDoneFlag = true;
  if (size==0) return;

  uint32_t rxTime = millis();

  uint8_t recipient = LoRa.read();
  uint8_t sender = LoRa.read();
  uint16_t msgId = (LoRa.read() << 8) | LoRa.read();
  uint8_t length = LoRa.read();

  uint8_t buffer[50];
  for (uint8_t i=0; i<length && i<50; i++) buffer[i]=LoRa.read();

  uint8_t msgType = buffer[0];

  // Nueva configuración del maestro
  if(msgType == MSG_TYPE_CONFIG) {
    newSF = buffer[1];
    newBW = buffer[2];
    newCR = buffer[3];
    newPower = buffer[4];

    Serial.println("========================================");
    Serial.println("✓ NUEVA CONFIGURACIÓN RECIBIDA");
    Serial.println("  SF: " + String(newSF));
    Serial.println("  BW: " + String(bandwidth_kHz[newBW]/1000.0) + " kHz");
    Serial.println("  CR: 4/" + String(newCR));
    Serial.println("  Power: " + String(newPower) + " dBm");
    Serial.println("========================================");

    configReceived = true;
    sendConfigAck();

    return;
  }

  // Datos normales del maestro
  if(msgType == MSG_TYPE_DATA) {
    String receivedMsg = "";
    if(length > 1) {
      for(uint8_t i=1; i<length; i++) {
        receivedMsg += (char)buffer[i];
      }
    }

    lastRSSI = LoRa.packetRssi();
    lastSNR = LoRa.packetSnr();
    long freqError = LoRa.packetFrequencyError();

    lastReceivedMsg = receivedMsg;
    packetsReceived++;
    lastRxTime = rxTime;

    String quality = evaluateSignalQuality(lastRSSI, lastSNR);

    Serial.println("========================================");
    Serial.println("✓ RX MAESTRO #" + String(msgId) + " | " + receivedMsg);
    Serial.println("RSSI:" + String(lastRSSI) + "dBm SNR:" + String(lastSNR) +
                   "dB FErr:" + String(freqError) + "Hz");
    Serial.println("Calidad: " + quality);
    Serial.println("RX:" + String(packetsReceived) + " Echo:" + String(echosSent));
    Serial.println("========================================");

    if(packetsReceived % 10 == 0) {
      Serial.println("\n★ Estadísticas:");
      Serial.println("  Total recibidos: " + String(packetsReceived));
      Serial.println("  Total ecos: " + String(echosSent));
      Serial.println("  Lecturas sensor: " + String(sensorReadings));
      Serial.println("  Config: SF:" + String(currentSF) +
                     " BW:" + String(bandwidth_kHz[currentBW]/1000.0) +
                     "kHz P:" + String(currentPower) + "dBm\n");
    }
  }
}

void TxFinished() {
  txDoneFlag = true;
}
ttyAMA10