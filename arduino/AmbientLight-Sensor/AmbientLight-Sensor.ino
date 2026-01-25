#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "SparkFun_VEML6030_Ambient_Light_Sensor.h"

// ====== CONFIGURACION ======
#define NODE_ID "LIGHT_01"
#define NODE_ID_HEX 0b10000001 // Nodo 1 tipo luz [luz(1)/ultrasonico(0), resto de bits ID]
#define LORA_FREQUENCY 868E6	 // Europa: 868 MHz
#define SENSOR_TYPE "light"

// ====== OBJETOS ======
SparkFun_Ambient_Light light(0x48);

// ====== VARIABLES GLOBALES ======
unsigned long lastSendTime = 0;
unsigned long sendInterval = 30000; // Enviar cada 30 segundos
float currentLux = 0;
String lastCommand = "NONE";

// Control de transmision
volatile bool txDoneFlag = true;
volatile bool transmitting = false;

// Configuracion LoRa
uint8_t currentSF = 12;
uint8_t currentPower = 20;
const uint8_t localAddress = 0xCC;
const uint8_t destination = 0xAA;

// Contadores
uint16_t msgCount = 0;
uint16_t successfulTx = 0;
uint16_t failedTx = 0;

// ====== CALLBACKS LoRa ======
void onTxDone()
{
	txDoneFlag = true;
	Serial.println("  TX completado");
}

void onReceive(int packetSize)
{
	if (packetSize == 0)
		return;

	Serial.println("\nPaquete LoRa recibido (" + String(packetSize) + " bytes)");

	String received = "";
	while (LoRa.available())
	{
		received += (char)LoRa.read();
	}

	int rssi = LoRa.packetRssi();
	float snr = LoRa.packetSnr();

	Serial.println("  Contenido: " + received);
	Serial.println("  RSSI: " + String(rssi) + " dBm | SNR: " + String(snr) + " dB");

	// Parsear JSON
	StaticJsonDocument<512> doc;
	DeserializationError error = deserializeJson(doc, received);

	if (error)
	{
		Serial.println("  ERROR: JSON invalido");
		return;
	}

	// Verificar que el mensaje es para este nodo
	String targetNode = doc["node_id"];
	if (targetNode != "" && targetNode != NODE_ID)
	{
		Serial.println("  (Mensaje para otro nodo: " + targetNode + ")");
		return;
	}

	String msgType = doc["msg_type"];

	if (msgType == "command")
	{
		handleCommand(doc);
	}
	else if (msgType == "config")
	{
		handleConfig(doc);
	}

	// Enviar ACK
	sendAck(msgType);
}

// ====== SETUP ======
void setup()
{
	Serial.begin(115200);
	while (!Serial)
		;

	Serial.println("\n========================================");
	Serial.println("   NODO SENSOR DE LUZ + LoRa");
	Serial.println("========================================");
	Serial.println("ID: " + String(NODE_ID));
	Serial.println("Tipo: " + String(SENSOR_TYPE));
	Serial.println();

	// ----------- SENSOR VEML6030 -----------
	Serial.println("Inicializando sensor VEML6030...");
	Wire.begin();

	if (!light.begin())
	{
		Serial.println("ERROR: Sensor VEML6030 no detectado");
		Serial.println("Verifica las conexiones I2C");
		while (1)
			delay(1000);
	}

	Serial.println("Sensor VEML6030 inicializado");
	light.setGain(1.0);
	light.setIntegTime(100);

	// ----------- LoRa -----------
	Serial.println("\nInicializando LoRa...");
	Serial.println("Frecuencia: " + String(LORA_FREQUENCY / 1E6) + " MHz");

	if (!LoRa.begin(LORA_FREQUENCY))
	{
		Serial.println("ERROR: LoRa no inicializado");
		while (1)
			delay(1000);
	}

	Serial.println("LoRa inicializado correctamente");

	// Configurar LoRa
	LoRa.setSignalBandwidth(125E3);
	LoRa.setSpreadingFactor(currentSF);
	LoRa.setCodingRate4(8);
	LoRa.setTxPower(currentPower);
	LoRa.setPreambleLength(16);
	LoRa.enableCrc();
	LoRa.setSyncWord(0x12);

	// Configurar callbacks
	LoRa.onTxDone(onTxDone);
	LoRa.onReceive(onReceive);

	// Modo recepcion
	LoRa.receive();

	Serial.println("\nConfiguracion LoRa aplicada:");
	Serial.println("  SF: " + String(currentSF));
	Serial.println("  BW: 125 kHz");
	Serial.println("  CR: 4/8");
	Serial.println("  Power: " + String(currentPower) + " dBm");

	Serial.println("\n========================================");
	Serial.println("          SISTEMA LISTO");
	Serial.println("========================================\n");

	delay(2000);
}

// ====== LOOP ======
void loop()
{
	// Leer sensor
	currentLux = light.readLight();
	unsigned long als = light.readWhiteLight();
	String estado = getLightState(currentLux);

	// Mostrar lectura
	Serial.println("=======================================");
	Serial.println("MEDICION #" + String(msgCount));
	Serial.println("---------------------------------------");
	Serial.println("  Luz: " + String(currentLux) + " lux");
	Serial.println("  ALS: " + String(als));
	Serial.println("  Estado: " + estado);
	Serial.println("=======================================");

	// Enviar datos periodicamente al maestro
	if (millis() - lastSendTime > sendInterval)
	{
		sendSensorData(currentLux, als, estado);
		lastSendTime = millis();
	}

	delay(2000);
}

// ====== FUNCIONES DE COMUNICACION ======

void sendSensorData(float lux, unsigned long als, String estado)
{
	if (transmitting)
	{
		Serial.println("TX en curso, saltando envio");
		return;
	}

	StaticJsonDocument<256> doc;
	doc["node_id"] = NODE_ID;
	doc["msg_type"] = "sensor_data";
	doc["timestamp"] = millis() / 1000;
	doc["msg_id"] = msgCount;
	doc["sensor_type"] = SENSOR_TYPE;

	JsonObject data = doc.createNestedObject("data");
	data["lux"] = round(lux * 10) / 10.0;
	data["als"] = als;
	data["estado"] = estado;

	String jsonString;
	serializeJson(doc, jsonString);

	Serial.println("\nEnviando datos al maestro #" + String(msgCount) + "...");
	Serial.println("  Datos: " + jsonString);

	sendLoRaPacket(jsonString);

	msgCount++;
}

void sendAck(String msgType)
{
	StaticJsonDocument<128> doc;
	doc["node_id"] = NODE_ID;
	doc["msg_type"] = "ack";
	doc["ack_for"] = msgType;
	doc["timestamp"] = millis() / 1000;

	String jsonString;
	serializeJson(doc, jsonString);

	Serial.println("Enviando ACK para: " + msgType);

	LoRa.beginPacket();
	LoRa.print(jsonString);
	LoRa.endPacket();

	Serial.println("  ACK enviado");
}

void sendLoRaPacket(String jsonString)
{
	transmitting = true;
	txDoneFlag = false;

	LoRa.beginPacket();
	LoRa.write(destination);
	LoRa.write(localAddress);
	LoRa.write((msgCount >> 8));
	LoRa.write(msgCount & 0xFF);
	LoRa.print(jsonString);
	LoRa.write(NODE_ID_HEX); // ID del nodo

	if (LoRa.endPacket(true))
	{
		Serial.println("  Paquete enviado");
		successfulTx++;
	}
	else
	{
		Serial.println("  Error al enviar");
		failedTx++;
		transmitting = false;
		return;
	}

	// Esperar confirmacion TX
	unsigned long txStart = millis();
	while (!txDoneFlag && (millis() - txStart < 5000))
	{
		delay(10);
	}

	transmitting = false;
	LoRa.receive();

	Serial.println("  Estadisticas - Exitos: " + String(successfulTx) + " | Fallos: " + String(failedTx));
}

// ====== MANEJO DE COMANDOS Y CONFIGURACION ======

void handleCommand(JsonDocument &doc)
{
	String action = doc["action"];

	Serial.println("\nComando recibido del maestro: " + action);

	if (action == "open_curtains")
	{
		openCurtains();
		lastCommand = "OPEN";
	}
	else if (action == "close_curtains")
	{
		closeCurtains();
		lastCommand = "CLOSE";
	}
	else if (action == "request_data")
	{
		Serial.println("  Solicitando envio inmediato de datos");
		float lux = light.readLight();
		unsigned long als = light.readWhiteLight();
		sendSensorData(lux, als, getLightState(lux));
	}
	else
	{
		Serial.println("  Accion desconocida: " + action);
	}
}

void handleConfig(JsonDocument &doc)
{
	Serial.println("\nActualizando configuracion...");

	bool changed = false;

	if (doc.containsKey("interval"))
	{
		sendInterval = doc["interval"];
		changed = true;
		Serial.println("  Intervalo: " + String(sendInterval / 1000) + " seg");
	}

	if (changed)
	{
		Serial.println("  Configuracion actualizada");
	}
}

// ====== FUNCIONES DE ACTUACION ======

void openCurtains()
{
	Serial.println("\n+-------------------------+");
	Serial.println("| ABRIENDO CORTINAS       |");
	Serial.println("+-------------------------+");
	lastCommand = "OPEN";
}

void closeCurtains()
{
	Serial.println("\n+-------------------------+");
	Serial.println("| CERRANDO CORTINAS       |");
	Serial.println("+-------------------------+");
	lastCommand = "CLOSE";
}

// ====== FUNCIONES AUXILIARES ======

String getLightState(float lux)
{
	if (lux < 200)
	{
		return "Oscuro";
	}
	else if (lux < 800)
	{
		return "Medio";
	}
	else
	{
		return "Brillante";
	}
}