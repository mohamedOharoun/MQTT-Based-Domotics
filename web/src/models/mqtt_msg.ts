import type { EventTriggerMessage, EventType } from "./events";
import type { SensorDataMessage } from "./sensor_data";

export interface StatusMessage {
	node_id: string;
	msg_type: "status";
	last_update: number;
}

export type MQTTMessage = SensorDataMessage | EventTriggerMessage | EventType | StatusMessage;

export type MessageRecord = MQTTMessage & {
	id: string;
	receivedAt: Date;
	topic: string;
};