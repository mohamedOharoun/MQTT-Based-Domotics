#ifndef GLOBALS_H
#define GLOBALS_H

/* ===================== OLED CONFIG ===================== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ===================== CONFIG ===================== */
#define MSG_TYPE_DATA 0x01
#define MSG_TYPE_SENSOR 0x05

#define SENSOR_ID_ULTRA 0xBB
#define SENSOR_ID_LIGHT 0xCC

#define DIST_STATE_ERROR 0x00
#define DIST_STATE_CLOSE 0x01
#define DIST_STATE_MEDIUM 0x02
#define DIST_STATE_FAR 0x03

#define LIGHT_STATE_DARK 0x00
#define LIGHT_STATE_DIM 0x01
#define LIGHT_STATE_BRIGHT 0x02

#define FRAME_PACKET_START_MARKER "comm:start$"
#define FRAME_PACKET_END_MARKER "$comm:end"

enum EventTriggerType
{
	TRIGGER_ABOVE,
	TRIGGER_BELOW,
	TRIGGER_EQUAL
};

struct EventType_t
{
	char *event_id;
	char *sensor_type;
	float trigger_threshold;
	EventTriggerType trigger_type;
	char *alert_message;
	bool is_active;
	char *target_device;
	float target_device_value;
};

#endif