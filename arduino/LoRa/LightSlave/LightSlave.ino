/* ---------------------------------------------------------------------
 * NODO ESCLAVO - SENSOR DE LUZ (VEML6030)
 * FORMATO: BINARIO
 * ---------------------------------------------------------------------
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include "SparkFun_VEML6030_Ambient_Light_Sensor.h"

// ========== PROTOCOLO BINARIO ==========
#define MSG_TYPE_SENSOR 0x05
#define SENSOR_ID_LIGHT 0xCC
#define SENSOR_TYPE_LUX 0x02

#define LIGHT_STATE_DARK 0x00
#define LIGHT_STATE_DIM 0x01
#define LIGHT_STATE_BRIGHT 0x02

// ========== CONFIGURACIÓN LORA ==========
const uint8_t localAddress = SENSOR_ID_LIGHT;
const uint8_t destination = 0xAA;

#define TX_LAPSE_MS 5000 // Enviar cada 5 segundos

SparkFun_Ambient_Light light(0x48);

volatile bool txDoneFlag = true;
volatile bool transmitting = false;
uint16_t msgCount = 0;

uint8_t currentSF = 12;
uint8_t currentBW = 7;
uint8_t currentCR = 8;
uint8_t currentPower = 20;

double bandwidth_kHz[10] = {
		7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
		41.7E3, 62.5E3, 125E3, 250E3, 500E3};

uint32_t lastSendTime = 0;

void setup()
{
	Serial.begin(115200);

	delay(2000);
	Serial.println("\n=== INICIANDO SISTEMA ===");

	Wire.begin();
	delay(100);

	Serial.print("Iniciando VEML6030... ");
	bool lightSensorStarted = light.begin();
	delay(100);
	Serial.println("Estado de arranque de sensor de luz: " + String(lightSensorStarted ? "Encendido" : "Estado Desconocido"));
	if (!lightSensorStarted)
	{
		Serial.println("FALLO");
		Serial.println("Revisa conexiones: SDA, SCL, 3.3V, GND");
		while (1)
			;
	}
	Serial.println("OK");

	light.setGain(1.0);			 // Use normal gain (0.125 is too low and causes saturation)
	light.setIntegTime(100); // 100ms integration

	if (!LoRa.begin(868E6))
	{
		Serial.println("FALLO");
		Serial.println("Revisa conexiones SPI: MOSI, MISO, SCK, CS, RST, DIO0");
		while (1)
			;
	}

	LoRa.setSignalBandwidth(long(bandwidth_kHz[currentBW]));
	LoRa.setSpreadingFactor(currentSF);
	LoRa.setCodingRate4(currentCR);
	LoRa.setTxPower(currentPower, PA_OUTPUT_PA_BOOST_PIN);
	LoRa.setPreambleLength(16);
	LoRa.enableCrc();
	LoRa.setSyncWord(0x12);
	LoRa.onTxDone(TxFinished);
	LoRa.receive();

	Serial.println("ESCLAVO LUZ (BINARIO) LISTO");

	lastSendTime = millis() - TX_LAPSE_MS;
}

void loop()
{
	if (!transmitting && (millis() - lastSendTime) > TX_LAPSE_MS)
	{
		lastSendTime = millis();

		float luxVal = light.readLight();
		uint16_t rawALS = light.readALS();

		Serial.print("Lectura Sensor: ");
		Serial.print(luxVal);
		Serial.print(" lux | ALS: ");
		Serial.println(rawALS);

		uint16_t luxInt = (uint16_t)luxVal;

		uint8_t state;
		if (luxVal < 200)
			state = LIGHT_STATE_DARK;
		else if (luxVal < 800)
			state = LIGHT_STATE_DIM;
		else
			state = LIGHT_STATE_BRIGHT;

		uint8_t payload[8];
		payload[0] = MSG_TYPE_SENSOR;
		payload[1] = SENSOR_ID_LIGHT;
		payload[2] = SENSOR_TYPE_LUX;
		payload[3] = (luxInt >> 8) & 0xFF;
		payload[4] = luxInt & 0xFF;
		payload[5] = state;
		payload[6] = (rawALS >> 8) & 0xFF;
		payload[7] = rawALS & 0xFF;

		Serial.println("Enviando paquete LoRa...");

		transmitting = true;
		txDoneFlag = false;
		sendMessage(payload, 8, msgCount++);
	}

	if (transmitting && txDoneFlag)
	{
		Serial.println("Tx Finalizada. Esperando...");
		transmitting = false;
		LoRa.receive();
	}
}

void sendMessage(uint8_t *payload, uint8_t length, uint16_t id)
{
	while (!LoRa.beginPacket())
		delay(5);
	LoRa.write(destination);
	LoRa.write(localAddress);
	LoRa.write(id >> 8);
	LoRa.write(id & 0xFF);
	LoRa.write(length);
	LoRa.write(payload, length);
	LoRa.endPacket(true);
}

void TxFinished()
{
	txDoneFlag = true;
}