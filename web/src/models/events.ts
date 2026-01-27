import type { SensorType } from "./sensor_data";

export interface EventType {
	msg_type: "event";
	sensor_type: SensorType;
	trigger_threshold: number;
	trigger_type: "above" | "below" | "equal";
	is_active: boolean;
	alert_message: string;
}

export interface EventTriggerMessage {
	node_id: string;
	msg_type: "alert";
	event: EventType;
	timestamp: number;
}
