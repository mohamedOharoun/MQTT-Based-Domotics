export type SensorType = "light" | "ultrasonic";

export interface LightSensorData {
	lux: number;
	als: number;
	estado: "Brillante" | "Oscuro" | "Medio";
}

export interface UltrasonicSensorData {
	distance_cm: number;
	estado: "Cerca" | "Lejos" | "Medio";
}

export interface SensorDataMessage {
	node_id: string;
	msg_type: "sensor_data";
	msg_id: number;
	sensor_type: SensorType;
	data: LightSensorData | UltrasonicSensorData;
}