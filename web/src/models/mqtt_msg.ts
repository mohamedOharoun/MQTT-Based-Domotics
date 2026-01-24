import type { EventTriggerMessage, EventType } from "./events";
import type { SensorDataMessage } from "./sensor_data";

export type MQTTMessage = SensorDataMessage | EventTriggerMessage | EventType;

export type MessageRecord = MQTTMessage & {
	id: string;
	receivedAt: Date;
	topic: string;
};