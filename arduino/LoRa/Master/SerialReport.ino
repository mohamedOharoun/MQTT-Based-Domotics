#include <ArduinoJson.h>
#include "globals.h"

void serialbridge_report_packet(const char *packetStr)
{
	Serial.println(String(FRAME_PACKET_START_MARKER) + String(packetStr) + String(FRAME_PACKET_END_MARKER));
}

String serialbridge_node_hex_to_string(uint8_t node_hex)
{
	String node_str = "node_";
	String node_type;

	// Explicitly check addresses
	if (node_hex == SENSOR_ID_ULTRA)
	{
		node_type = "ULTRASONIC_";
	}
	else if (node_hex == SENSOR_ID_LIGHT)
	{
		node_type = "LIGHT_";
	}
	else
	{
		node_type = "UNKNOWN_";
	}

	node_str += node_type;

	// Add the hex ID for uniqueness
	if (node_hex < 0x10)
		node_str += "0";
	node_str += String(node_hex, HEX);

	return node_str;
}

String serialbridge_sensor_type_to_string(uint8_t node_hex)
{
	if (node_hex == SENSOR_ID_LIGHT)
		return "light";
	if (node_hex == SENSOR_ID_ULTRA)
		return "ultrasonic";
	return "unknown";
}

void serialbridge_report_ultrasonic_sensor_data(uint8_t node_hex_id, uint32_t msg_id, uint32_t distance_cm, const char *state)
{
	StaticJsonDocument<256> doc;
	doc["node_id"] = serialbridge_node_hex_to_string(node_hex_id).c_str();
	doc["msg_type"] = "sensor_data";
	doc["timestamp"] = rtc.getEpoch();
	doc["msg_id"] = msg_id;
	doc["sensor_type"] = "ultrasonic";

	JsonObject data = doc.createNestedObject("data");
	data["distance_cm"] = distance_cm;
	data["estado"] = state;

	String jsonString;
	serializeJson(doc, jsonString);
	serialbridge_report_packet(jsonString.c_str());
}

void serialbridge_report_light_sensor_data(uint8_t node_hex_id, uint32_t msg_id, double lux, int32_t als, const char *state)
{
	StaticJsonDocument<256> doc;
	doc["node_id"] = serialbridge_node_hex_to_string(node_hex_id).c_str();
	doc["msg_type"] = "sensor_data";
	doc["timestamp"] = rtc.getEpoch();
	doc["msg_id"] = msg_id;
	doc["sensor_type"] = "light";

	JsonObject data = doc.createNestedObject("data");
	data["lux"] = lux;
	data["als"] = als;
	data["estado"] = state;

	String jsonString;
	serializeJson(doc, jsonString);
	serialbridge_report_packet(jsonString.c_str());
}

void serialbridge_report_event_trigger(uint8_t node_hex_id, EventType_t *event)
{
	StaticJsonDocument<256> doc;
	doc["node_id"] = serialbridge_node_hex_to_string(node_hex_id).c_str();
	doc["msg_type"] = "alert";

	JsonObject event_doc = doc.createNestedObject("event");
	event_doc["msg_type"] = "event";
	event_doc["sensor_type"] = serialbridge_sensor_type_to_string(node_hex_id).c_str();
	event_doc["trigger_threshold"] = event->trigger_threshold;

	String trigger_type_str = (event->trigger_type == EventTriggerType::TRIGGER_ABOVE) ? "above" : (event->trigger_type == EventTriggerType::TRIGGER_BELOW) ? "below"
																																																																													: "equal";

	event_doc["trigger_type"] = trigger_type_str;
	event_doc["is_active"] = event->is_active;
	event_doc["alert_message"] = event->alert_message;

	String json;
	serializeJson(doc, json);
	serialbridge_report_packet(json.c_str());
}