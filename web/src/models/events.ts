import type { MsgType } from "./incoming_msg";
import type { SensorType } from "./sensor_data";

export interface EventType {
	msg_type: MsgType;
	sensor_type: SensorType;
	trigger_threshold: number;
	trigger_type: "above" | "below" | "equal";
	is_active: boolean;
}

export interface EventTriggerMessage {
	node_id: string;
	msg_type: MsgType;
	event: EventType;
}
