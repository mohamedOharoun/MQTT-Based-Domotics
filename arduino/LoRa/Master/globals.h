#ifndef GLOBALS_H
#define GLOBALS_H

enum EventTriggerType
{
	TRIGGER_ABOVE,
	TRIGGER_BELOW,
	TRIGGER_EQUAL
};

struct EventType_t
{
	char *sensor_type;
	float trigger_threshold;
	EventTriggerType trigger_type;
	bool is_active;
};

#endif