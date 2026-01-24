#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "SparkFun_VEML6030_Ambient_Light_Sensor.h"

// ====== CONFIGURACIÓN ======
#define NODE_ID "LIGHT_01"
#define LORA_FREQUENCY 868E6  // Europa: 868 MHz

// Umbrales de luz para automatización
#define LUX_MIN_THRESHOLD 200   // Abrir cortinas si está por debajo
#define LUX_MAX_THRESHOLD 800   // Cerrar cortinas si está por encima

// ====== OBJETOS ======
SparkFun_Ambient_Light light(0x48);

// ====== VARIABLES GLOBALES ======
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000;  // Enviar cada 30 segundos
float currentLux = 0;
String lastCommand = "NONE";
bool autoMode = true;

// Control de transmisión
volatile bool txDoneFlag = true;
volatile bool transmitting = false;

// Configuración LoRa
uint8_t currentSF = 12;        // SF máximo para largo alcance
uint8_t currentPower = 20;     // Potencia máxima
const uint8_t localAddress = 0xCC;  // Dirección de este nodo
const uint8_t destination = 0xAA;   // Dirección del repetidor/gateway

// Estructura de configuración
struct Config {
  float luxMinThreshold = LUX_MIN_THRESHOLD;
  float luxMaxThreshold = LUX_MAX_THRESHOLD;
  unsigned long sendInterval = 30000;
  bool autoControlEnabled = true;
} config;

// Contadores
uint16_t msgCount = 0;
uint16_t successfulTx = 0;
uint16_t failedTx = 0;

// ====== CALLBACKS LoRa ======
void onTxDone() {
  txDoneFlag = true;
  Serial.println("  ✓ TX completado");
}

void onReceive(int packetSize) {
  if (packetSize == 0) return;
  
  Serial.println("\n← Paquete LoRa recibido (" + String(packetSize) + " bytes)");
  
  String received = "";
  while (LoRa.available()) {
    received += (char)LoRa.read();
  }
  
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  
  Serial.println("  Contenido: " + received);
  Serial.println("  RSSI: " + String(rssi) + " dBm | SNR: " + String(snr) + " dB");
  
  // Parsear JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, received);
  
  if (error) {
    Serial.println("  ERROR: JSON inválido");
    return;
  }
  
  // Verificar que el mensaje es para este nodo
  String targetNode = doc["node_id"];
  if (targetNode != NODE_ID) {
    Serial.println("  (Mensaje para otro nodo: " + targetNode + ")");
    return;
  }
  
  String msgType = doc["msg_type"];
  
  if (msgType == "command") {
    handleCommand(doc);
  } else if (msgType == "config") {
    handleConfig(doc);
  }
  
  // Enviar ACK
  sendAck(msgType);
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   NODO SENSOR DE LUZ + LoRa            ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("ID: " + String(NODE_ID));
  Serial.println();
  
  // ----------- SENSOR VEML6030 -----------
  Serial.println("Inicializando sensor VEML6030...");
  Wire.begin();
  
  if (!light.begin()) {
    Serial.println("ERROR: Sensor VEML6030 no detectado");
    Serial.println("Verifica las conexiones I2C");
    while(1) delay(1000);
  }
  
  Serial.println("✓ Sensor VEML6030 inicializado");
  light.setGain(1.0);
  light.setIntegTime(100);
  
  // ----------- LoRa -----------
  Serial.println("\nInicializando LoRa...");
  Serial.println("Frecuencia: " + String(LORA_FREQUENCY / 1E6) + " MHz");
  
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("ERROR: LoRa no inicializado");
    while(1) delay(1000);
  }
  
  Serial.println("✓ LoRa inicializado correctamente");
  
  // Configurar LoRa con parámetros robustos
  LoRa.setSignalBandwidth(125E3);           // 125 kHz
  LoRa.setSpreadingFactor(currentSF);       // SF12 = máximo alcance
  LoRa.setCodingRate4(8);                   // 4/8 = máxima corrección
  LoRa.setTxPower(currentPower);            // 20 dBm = potencia máxima
  LoRa.setPreambleLength(16);               // Preámbulo largo para SF alto
  LoRa.enableCrc();                         // CRC para integridad
  LoRa.setSyncWord(0x12);                   // Sync word estándar
  
  // Configurar callbacks
  LoRa.onTxDone(onTxDone);
  LoRa.onReceive(onReceive);
  
  // Modo recepción
  LoRa.receive();
  
  Serial.println("\nConfiguración LoRa aplicada:");
  Serial.println("  SF: " + String(currentSF));
  Serial.println("  BW: 125 kHz");
  Serial.println("  CR: 4/8");
  Serial.println("  Power: " + String(currentPower) + " dBm");
  Serial.println("  Alcance estimado: ~15 km");
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║          SISTEMA LISTO                 ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  delay(2000);
}

// ====== LOOP ======
void loop() {
  // Leer sensor
  currentLux = light.readLight();
  unsigned long als = light.readWhiteLight();
  String estado = getLightState(currentLux);
  
  // Mostrar lectura
  Serial.println("═══════════════════════════════════════");
  Serial.println("MEDICIÓN #" + String(msgCount));
  Serial.println("───────────────────────────────────────");
  Serial.println("  Luz: " + String(currentLux) + " lux");
  Serial.println("  ALS: " + String(als));
  Serial.println("  Estado: " + estado);
  Serial.println("═══════════════════════════════════════");
  
  // Control automático
  if (config.autoControlEnabled) {
    checkAutomationRules(currentLux);
  }
  
  // Enviar datos periódicamente
  if (millis() - lastSendTime > config.sendInterval) {
    sendSensorData(currentLux, als, estado);
    lastSendTime = millis();
  }
  
  delay(2000);
}

// ====== FUNCIONES DE COMUNICACIÓN ======

void sendSensorData(float lux, unsigned long als, String estado) {
  if (transmitting) {
    Serial.println("⚠ TX en curso, saltando envío");
    return;
  }
  
  StaticJsonDocument<256> doc;
  doc["node_id"] = NODE_ID;
  doc["msg_type"] = "sensor_data";
  doc["timestamp"] = millis() / 1000;
  doc["msg_id"] = msgCount;
  
  JsonObject data = doc.createNestedObject("data");
  data["lux"] = round(lux * 10) / 10.0;  // 1 decimal
  data["als"] = als;
  data["estado"] = estado;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("\n→ Enviando paquete #" + String(msgCount) + "...");
  Serial.println("  Datos: " + jsonString);
  Serial.println("  Tamaño: " + String(jsonString.length()) + " bytes");
  
  // Preparar transmisión
  transmitting = true;
  txDoneFlag = false;
  
  // Enviar paquete
  LoRa.beginPacket();
  LoRa.write(destination);        // Destinatario
  LoRa.write(localAddress);       // Remitente
  LoRa.write((msgCount >> 8));    // ID alto
  LoRa.write(msgCount & 0xFF);    // ID bajo
  LoRa.print(jsonString);         // Payload
  
  if (LoRa.endPacket(true)) {     // true = async
    Serial.println("Paquete enviado (async)");
    successfulTx++;
  } else {
    Serial.println("Error al enviar");
    failedTx++;
    transmitting = false;
  }
  
  msgCount++;
  
  // Esperar confirmación TX
  unsigned long txStart = millis();
  while (!txDoneFlag && (millis() - txStart < 5000)) {
    delay(10);
  }
  
  transmitting = false;
  LoRa.receive();  // Volver a modo RX
  
  Serial.println("  Éxitos: " + String(successfulTx) + " | Fallos: " + String(failedTx));
}

void handleCommand(JsonDocument& doc) {
  String action = doc["action"];
  
  Serial.println("→ Ejecutando comando: " + action);
  
  if (action == "open_curtains") {
    openCurtains();
    lastCommand = "OPEN";
  } else if (action == "close_curtains") {
    closeCurtains();
    lastCommand = "CLOSE";
  } else if (action == "request_data") {
    float lux = light.readLight();
    unsigned long als = light.readWhiteLight();
    sendSensorData(lux, als, getLightState(lux));
  } else if (action == "enable_auto") {
    config.autoControlEnabled = true;
    Serial.println("Modo automático ACTIVADO");
  } else if (action == "disable_auto") {
    config.autoControlEnabled = false;
    Serial.println("Modo automático DESACTIVADO");
  } else {
    Serial.println(" Acción desconocida: " + action);
  }
}

void handleConfig(JsonDocument& doc) {
  Serial.println("→ Actualizando configuración...");
  
  bool changed = false;
  
  if (doc.containsKey("lux_min")) {
    config.luxMinThreshold = doc["lux_min"];
    changed = true;
  }
  if (doc.containsKey("lux_max")) {
    config.luxMaxThreshold = doc["lux_max"];
    changed = true;
  }
  if (doc.containsKey("interval")) {
    config.sendInterval = doc["interval"];
    changed = true;
  }
  if (doc.containsKey("auto_control")) {
    config.autoControlEnabled = doc["auto_control"];
    changed = true;
  }
  
  if (changed) {
    Serial.println("    Nueva configuración:");
    Serial.println("    Umbral mín: " + String(config.luxMinThreshold) + " lux");
    Serial.println("    Umbral máx: " + String(config.luxMaxThreshold) + " lux");
    Serial.println("    Intervalo: " + String(config.sendInterval / 1000) + " seg");
    Serial.println("    Auto control: " + String(config.autoControlEnabled ? "ON" : "OFF"));
  }
}

void sendAck(String msgType) {
  StaticJsonDocument<128> doc;
  doc["node_id"] = NODE_ID;
  doc["msg_type"] = "ack";
  doc["ack_for"] = msgType;
  doc["timestamp"] = millis() / 1000;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("→ Enviando ACK para: " + msgType);
  
  LoRa.beginPacket();
  LoRa.print(jsonString);
  LoRa.endPacket();
  
  Serial.println("  ✓ ACK enviado");
}

// ====== LÓGICA DE AUTOMATIZACIÓN ======

void checkAutomationRules(float lux) {
  static unsigned long lastActionTime = 0;
  const unsigned long actionCooldown = 60000;  // 1 minuto entre acciones
  
  if (millis() - lastActionTime < actionCooldown) {
    return;
  }
  
  // Regla 1: Poca luz → Abrir cortinas
  if (lux < config.luxMinThreshold && lastCommand != "OPEN") {
    Serial.println("\n LUZ BAJA DETECTADA (" + String(lux) + " lux)");
    Serial.println("→ Acción automática: ABRIR CORTINAS");
    openCurtains();
    sendAutomationAlert("low_light", lux);
    lastCommand = "OPEN";
    lastActionTime = millis();
  }
  
  // Regla 2: Mucha luz → Cerrar cortinas
  else if (lux > config.luxMaxThreshold && lastCommand != "CLOSE") {
    Serial.println("\n LUZ ALTA DETECTADA (" + String(lux) + " lux)");
    Serial.println("→ Acción automática: CERRAR CORTINAS");
    closeCurtains();
    sendAutomationAlert("high_light", lux);
    lastCommand = "CLOSE";
    lastActionTime = millis();
  }
}

void sendAutomationAlert(String reason, float lux) {
  StaticJsonDocument<256> doc;
  doc["node_id"] = NODE_ID;
  doc["msg_type"] = "alert";
  doc["timestamp"] = millis() / 1000;
  doc["reason"] = reason;
  doc["lux"] = lux;
  doc["action_taken"] = lastCommand;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("→ Enviando alerta: " + reason);
  
  LoRa.beginPacket();
  LoRa.print(jsonString);
  LoRa.endPacket();
  
  Serial.println("Alerta enviada");
}

// ====== FUNCIONES DE ACTUACIÓN ======

void openCurtains() {
  Serial.println("┌─────────────────────────┐");
  Serial.println("│ >>> CORTINAS ABIERTAS <<<│");
  Serial.println("└─────────────────────────┘");
  // Aquí conectarías un relé, motor, servo, etc.
  // digitalWrite(RELAY_PIN, HIGH);
}

void closeCurtains() {
  Serial.println("┌─────────────────────────┐");
  Serial.println("│ >>> CORTINAS CERRADAS <<<│");
  Serial.println("└─────────────────────────┘");
  // digitalWrite(RELAY_PIN, LOW);
}

// ====== FUNCIONES AUXILIARES ======

String getLightState(float lux) {
  if (lux < 50) return "Oscuro";
  else if (lux < 200) return "Poca luz";
  else if (lux < 500) return "Interior";
  else if (lux < 1000) return "Brillante";
  else return "Muy brillante";
}