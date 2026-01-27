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

bool setDateTime(const char *date_str, const char *time_str)
{
	char month_str[4];
	char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	uint16_t i, mday, month, hour, min, sec, year;

	if (sscanf(date_str, "%3s %hu %hu", month_str, &mday, &year) != 3)
		return false;
	if (sscanf(time_str, "%hu:%hu:%hu", &hour, &min, &sec) != 3)
		return false;

	for (i = 0; i < 12; i++)
	{
		if (!strncmp(month_str, months[i], 3))
		{
			month = i + 1;
			break;
		}
	}
	if (i == 12)
		return false;

	rtc.setTime((uint8_t)hour, (uint8_t)min, (uint8_t)sec + 8);
	rtc.setDate((uint8_t)mday, (uint8_t)month, (uint8_t)(year - 2000));
	return true;
}

/* ===================== VARS ===================== */

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
	char event_id[16];
	char sensorType[16];
	uint8_t triggerType; // 0:Above, 1:Below, 2:Equal
	float threshold;
	bool active;
	bool triggered;
	uint32_t lastTrigger;
	char alertMessage[64];
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
	LoRa.idle();
	delay(50);
	LoRa.setSignalBandwidth(long(bandwidth_kHz[currentBW]));
	LoRa.setSpreadingFactor(currentSF);
	LoRa.setCodingRate4(currentCR);
	LoRa.setTxPower(currentPower, PA_OUTPUT_PA_BOOST_PIN);
	LoRa.setPreambleLength(16);
	LoRa.enableCrc();
	LoRa.setSyncWord(0x12);
}

/* ===================== SETUP ===================== */
void setup()
{
	Serial.begin(115200);
	delay(2000);

	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D))
	{
		Serial.println("OLED Failed");
		while (1)
			;
	}
	updateDisplay("MAESTRO", "INICIANDO...", "");

	SerialUSB.print(__DATE__);
	SerialUSB.print(" ");
	SerialUSB.println(__TIME__);
	rtc.begin();

	if (!setDateTime(__DATE__, __TIME__))
	{
		SerialUSB.println("RTC setDateTime() failed!\nExiting ...");
		while (1)
		{
			;
		}
	}

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

	if (millis() - lastBeat > 1000)
	{
		lastBeat = millis();
		Serial.print("."); // Debug heartbeat
	}

	int packetSize = LoRa.parsePacket();
	if (packetSize)
	{
		Serial.println("\nRX!");
		processPacket(packetSize);
	}

	// Serial bridge receive
	while (Serial.available())
	{
		char c = Serial.read();
		if (!frameInprog)
		{
			if (c == 'c') // Check for start of frame marker
			{
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
			{
				char *end = strstr(serialBuf, FRAME_END);
				*end = 0;
				handleJsonCommand(serialBuf);
				frameInprog = false;
				bufIdx = 0;
				memset(serialBuf, 0, sizeof(serialBuf));
			}
		}
	}

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

		strlcpy(e->event_id, doc["event_id"], 16);
		strlcpy(e->sensorType, doc["sensor_type"], 16);
		String trig = doc["trigger_type"];
		e->triggerType = (trig == "below") ? 1 : (trig == "equal" ? 2 : 0);
		e->threshold = doc["trigger_threshold"];
		e->active = true;
		strlcpy(e->alertMessage, doc["alert_message"], 64);

		Serial.println("Event Added!");
		updateDisplay("CONFIG", "Event Added", "", 2000);
	}
	else if (strcmp(type, "clear_event") == 0)
	{
		const char *eventId = doc["event_id"];
		for (int i = 0; i < eventCount; i++)
		{
			if (strcmp(events[i].event_id, eventId) == 0)
			{
				for (int j = i; j < eventCount - 1; j++)
					events[j] = events[j + 1];

				eventCount--;
				Serial.println("Event Removed");
				updateDisplay("CONFIG", "Event Removed", "", 2000);
				break;
			}
		}
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
			Serial.println(e->alertMessage);
			updateDisplay("ALERT!", e->alertMessage, String(val), 4000);

			EventType_t evtRpt;
			evtRpt.trigger_threshold = e->threshold;
			evtRpt.is_active = true;
			evtRpt.trigger_type = (e->triggerType == 0) ? TRIGGER_ABOVE : (e->triggerType == 1 ? TRIGGER_BELOW : TRIGGER_EQUAL);
			evtRpt.sensor_type = e->sensorType;
			evtRpt.alert_message = e->alertMessage;
			evtRpt.event_id = e->event_id;

			serialbridge_report_event_trigger(nodeId, &evtRpt);
		}
		else if (!hit)
		{
			e->triggered = false;
		}
	}
}