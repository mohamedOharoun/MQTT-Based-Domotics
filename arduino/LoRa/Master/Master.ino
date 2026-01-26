/* ---------------------------------------------------------------------
 * NODO MAESTRO (FINAL STABLE VERSION)
 * Fixes: Removes Serial Blocking risks & LoRa Mode conflicts
 * ---------------------------------------------------------------------
 */

#include <SPI.h>
#include <LoRa.h>
#include <Arduino_PMIC.h>
#include <Wire.h>
#include <RTCZero.h>
#include <ArduinoJson.h>

// OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "globals.h"

/* ===================== OLED CONFIG ===================== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ===================== CONFIG ===================== */
#define MSG_TYPE_DATA 0x01
#define MSG_TYPE_SENSOR 0x05

#define SENSOR_ID_ULTRA 0x01
#define SENSOR_ID_LIGHT 0x02

#define DIST_STATE_ERROR 0x00
#define DIST_STATE_CLOSE 0x01
#define DIST_STATE_MEDIUM 0x02
#define DIST_STATE_FAR 0x03

#define LIGHT_STATE_DARK 0x00
#define LIGHT_STATE_DIM 0x01
#define LIGHT_STATE_BRIGHT 0x02

/* ===================== ADDRESSES ===================== */
const uint8_t localAddress = 0xAA;

/* ===================== LORA PARAMETERS ===================== */
uint8_t currentSF = 12;
uint8_t currentBW = 7;
uint8_t currentCR = 8;
uint8_t currentPower = 20;

double bandwidth_kHz[10] = {
		7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
		41.7E3, 62.5E3, 125E3, 250E3, 500E3};

/* ===================== RTC & VARS ===================== */
RTCZero rtc;

// Display Management
enum DisplayState
{
	DISPLAY_IDLE,
	DISPLAY_EVENT,
	DISPLAY_SENSOR
};
DisplayState currentDisplayState = DISPLAY_IDLE;
uint32_t displayTimer = 0;
String dispLine1, dispLine2, dispLine3;

// Event Management
const uint8_t MAX_EVENTS = 10;
struct Event
{
	char sensorType[16];
	uint8_t triggerType; // 0:Above, 1:Below, 2:Equal
	float threshold;
	char action[32];
	bool active;
	bool triggered;
	uint32_t lastTrigger;
};
Event events[MAX_EVENTS];
uint8_t eventCount = 0;

// Serial Buffer
#define FRAME_START "comm:start$"
#define FRAME_END "$comm:end"
char serialBuf[512];
uint16_t bufIdx = 0;
bool frameInprog = false;

/* ===================== HELPERS ===================== */
void updateDisplay(String l1, String l2, String l3, int duration = 0)
{
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(0, 0);
	display.println(l1);
	display.println(l2);
	display.println(l3);
	display.display();

	if (duration > 0)
	{
		displayTimer = millis() + duration;
		currentDisplayState = DISPLAY_EVENT;
	}
}

void applyConfiguration()
{
	LoRa.idle(); // Put radio in standby
	delay(50);
	LoRa.setSignalBandwidth(long(bandwidth_kHz[currentBW]));
	LoRa.setSpreadingFactor(currentSF);
	LoRa.setCodingRate4(currentCR);
	LoRa.setTxPower(currentPower, PA_OUTPUT_PA_BOOST_PIN);
	LoRa.setPreambleLength(16);
	LoRa.enableCrc();
	LoRa.setSyncWord(0x12);
	// NOTE: Do NOT call LoRa.receive() here for Polling Mode.
	// parsePacket() handles the mode switch automatically.
}

/* ===================== SETUP ===================== */
void setup()
{
	Serial.begin(115200);
	// Removed "while(!Serial)" to prevent battery hang,
	// but ensure you open monitor quickly!
	delay(2000);

	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D))
	{
		Serial.println("OLED Failed");
		while (1)
			;
	}
	updateDisplay("MAESTRO", "INICIANDO...", "");

	rtc.begin();
	// Set a default date/time if not set
	rtc.setTime(12, 0, 0);
	rtc.setDate(1, 1, 24);

	if (!LoRa.begin(868E6))
	{
		Serial.println("LoRa Failed");
		updateDisplay("ERROR", "LORA FAIL", "");
		while (1)
			;
	}

	applyConfiguration();

	Serial.println("\nMAESTRO LISTO (PURE POLLING)");
	updateDisplay("MAESTRO OK", "Esperando...", "");
}

/* ===================== LOOP ===================== */
void loop()
{
	static uint32_t lastBeat = 0;

	// 1. Heartbeat (Visual Debug)
	if (millis() - lastBeat > 1000)
	{
		lastBeat = millis();
		// If these dots stop, your code is FROZEN (Serial buffer full or I2C hang)
		Serial.print(".");
	}

	// 2. LoRa Polling
	int packetSize = LoRa.parsePacket();
	if (packetSize)
	{
		Serial.println("\nRX!"); // Debug print
		processPacket(packetSize);
	}

	// 3. Serial Input (Python)
	while (Serial.available())
	{
		char c = Serial.read();
		if (!frameInprog)
		{
			if (c == 'c')
			{ // Quick check for start of "comm:start$"
				serialBuf[bufIdx++] = c;
			}
			else if (bufIdx > 0)
			{
				serialBuf[bufIdx++] = c;
				if (strstr(serialBuf, FRAME_START))
				{
					frameInprog = true;
					bufIdx = 0;
					memset(serialBuf, 0, sizeof(serialBuf));
				}
			}
			else
			{
				bufIdx = 0; // Reset noise
			}
		}
		else
		{
			serialBuf[bufIdx++] = c;
			if (bufIdx >= 511)
				bufIdx = 0; // Prevent overflow
			if (c == 'd' && strstr(serialBuf, FRAME_END))
			{ // End detected
				char *end = strstr(serialBuf, FRAME_END);
				*end = 0;
				handleJsonCommand(serialBuf);
				frameInprog = false;
				bufIdx = 0;
				memset(serialBuf, 0, sizeof(serialBuf));
			}
		}
	}

	// 4. Display Timeout
	if (currentDisplayState != DISPLAY_IDLE && millis() > displayTimer && displayTimer != 0)
	{
		currentDisplayState = DISPLAY_IDLE;
		displayTimer = 0;
		updateDisplay("MAESTRO OK", "Eventos: " + String(eventCount), "Esperando...");
	}
}

/* ===================== LORA PROCESSING ===================== */
void processPacket(int size)
{
	if (size == 0)
		return;

	// Read Header
	LoRa.read(); // Recipient
	uint8_t sender = LoRa.read();
	uint16_t msgId = (LoRa.read() << 8) | LoRa.read();
	uint8_t len = LoRa.read();

	// Safety check
	if (len > 30)
		len = 30;

	uint8_t buf[32];
	for (int i = 0; i < len; i++)
		buf[i] = LoRa.read();

	// Check Type
	if (buf[0] != MSG_TYPE_SENSOR)
		return;

	uint8_t id = buf[1]; // Sensor ID

	// --- ULTRASONIC ---
	if (id == SENSOR_ID_ULTRA)
	{
		uint16_t dist = (buf[3] << 8) | buf[4];
		uint8_t st = buf[5];
		String sStr = (st == 1) ? "Cerca" : (st == 2) ? "Medio"
																		: (st == 3)		? "Lejos"
																									: "Err";

		Serial.print(" ULTRA [0x");
		Serial.print(sender, HEX);
		Serial.print("] ");
		Serial.print(dist);
		Serial.println("cm");

		if (currentDisplayState == DISPLAY_IDLE)
			updateDisplay("ULTRA", String(dist) + "cm", sStr);

		serialbridge_report_ultrasonic_sensor_data(sender, msgId, dist, sStr.c_str());
		checkEvents(id, "ultrasonic", (float)dist);
	}

	// --- LIGHT ---
	else if (id == SENSOR_ID_LIGHT)
	{
		uint16_t lux = (buf[3] << 8) | buf[4];
		uint8_t st = buf[5];
		String sStr = (st == 2) ? "Bright" : (st == 1) ? "Dim"
																									 : "Dark";

		Serial.print(" LIGHT [0x");
		Serial.print(sender, HEX);
		Serial.print("] ");
		Serial.print(lux);
		Serial.println("lx");

		if (currentDisplayState == DISPLAY_IDLE)
			updateDisplay("LIGHT", String(lux) + "lx", sStr);

		serialbridge_report_light_sensor_data(sender, msgId, (double)lux, (int32_t)lux, sStr.c_str());
		checkEvents(id, "light", (float)lux);
	}
}

/* ===================== LOGIC & EVENTS ===================== */
void handleJsonCommand(char *json)
{
	StaticJsonDocument<512> doc;
	if (deserializeJson(doc, json))
		return;

	const char *type = doc["msg_type"];
	if (strcmp(type, "event") == 0)
	{
		if (eventCount >= MAX_EVENTS)
			return;
		Event *e = &events[eventCount++];

		strlcpy(e->sensorType, doc["sensor_type"], 16);
		String trig = doc["trigger_type"];
		e->triggerType = (trig == "below") ? 1 : (trig == "equal" ? 2 : 0);
		e->threshold = doc["trigger_threshold"];
		strlcpy(e->action, doc["action"], 32);
		e->active = true;

		Serial.println("Event Added!");
		updateDisplay("CONFIG", "Event Added", "", 2000);
	}
	else if (strcmp(type, "clear_events") == 0)
	{
		eventCount = 0;
		Serial.println("Events Cleared");
		updateDisplay("CONFIG", "Events Cleared", "", 2000);
	}
}

void checkEvents(uint8_t nodeId, const char *type, float val)
{
	for (int i = 0; i < eventCount; i++)
	{
		Event *e = &events[i];
		if (strcmp(e->sensorType, type) != 0 || !e->active)
			continue;

		bool hit = false;
		if (e->triggerType == 0 && val > e->threshold)
			hit = true;
		if (e->triggerType == 1 && val < e->threshold)
			hit = true;
		if (e->triggerType == 2 && abs(val - e->threshold) < 0.1)
			hit = true;

		if (hit && (!e->triggered || (millis() - e->lastTrigger > 5000)))
		{
			e->triggered = true;
			e->lastTrigger = millis();

			// TRIGGER!
			Serial.print(">>> EVENT: ");
			Serial.println(e->action);
			updateDisplay("ALERT!", e->action, String(val), 4000);

			EventType_t evtRpt;
			evtRpt.trigger_threshold = e->threshold;
			evtRpt.is_active = true;
			evtRpt.trigger_type = (e->triggerType == 0) ? TRIGGER_ABOVE : (e->triggerType == 1 ? TRIGGER_BELOW : TRIGGER_EQUAL);
			serialbridge_report_event_trigger(nodeId, &evtRpt);
		}
		else if (!hit)
		{
			e->triggered = false;
		}
	}
}