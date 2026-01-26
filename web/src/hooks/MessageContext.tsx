import { createContext, useContext, useEffect, useMemo, useState } from "react";
import type { ReactNode } from "react";
import type { MqttClient } from "mqtt";
import type { MessageRecord, MQTTMessage, StatusMessage } from "../models/mqtt_msg";
import type { LightSensorData, UltrasonicSensorData } from "../models/sensor_data";

type MessageContextValue = {
	messages: MessageRecord[];
	clearMessages: () => void;
};

const MessageContext = createContext<MessageContextValue | undefined>(undefined);

type ProviderProps = {
	children: ReactNode;
	mqttClient: MqttClient | null;
};

const createSensorMessage = (
	nodeId: string,
	sensorType: "light" | "ultrasonic",
	data: LightSensorData | UltrasonicSensorData,
	topic: string
): MessageRecord => ({
	node_id: nodeId,
	msg_type: "sensor_data",
	msg_id: Math.floor(Math.random() * 1_000_000),
	sensor_type: sensorType,
	data,
	id: `${Date.now()}-${Math.random()}`,
	receivedAt: new Date(),
	topic,
});

export function MessageProvider({ children, mqttClient }: ProviderProps) {
	const [messages, setMessages] = useState<MessageRecord[]>([]);
	const [partialLightData, setPartialLightData] = useState<Map<string, Partial<LightSensorData>>>(new Map());
	const [partialUltrasonicData, setPartialUltrasonicData] = useState<Map<string, Partial<UltrasonicSensorData>>>(new Map());

	useEffect(() => {
		if (!mqttClient) return;

		const handleMessage = (topic: string, payload: Buffer) => {
			try {
				if (topic.includes("params/event/alert/")) {
					const event_trigger = JSON.parse(payload.toString()) as MQTTMessage;
					const messageRecord: MessageRecord = {
						...event_trigger,
						id: `${Date.now()}-${Math.random()}`,
						receivedAt: new Date(),
						topic,
					};
					setMessages((prev) => [messageRecord, ...prev].slice(0, 100));
				} else if (topic.includes("params/event/")) {
					if (payload.length === 0) return;
					const message = JSON.parse(payload.toString()) as MQTTMessage;
					const messageRecord: MessageRecord = {
						...message,
						id: `${Date.now()}-${Math.random()}`,
						receivedAt: new Date(),
						topic,
					};
					setMessages((prev) => [messageRecord, ...prev].slice(0, 100));
				} else if (topic.startsWith("sensors/brightness/")) {
					const parts = topic.split("/");
					const nodeId = parts[2] || "unknown";
					const field = parts[3];
					const value = payload.toString();

					setPartialLightData((prev) => {
						const updated = new Map(prev);
						const current = updated.get(nodeId) || {};

						if (field === "light") {
							current.lux = parseFloat(value);
						} else if (field === "als") {
							current.als = parseInt(value);
						} else if (field === "estado") {
							current.estado = value as "Brillante" | "Oscuro" | "Medio";
						}

						updated.set(nodeId, current);

						if (current.lux !== undefined && current.als !== undefined && current.estado !== undefined) {
							const completeData: LightSensorData = {
								lux: current.lux,
								als: current.als,
								estado: current.estado,
							};

							const message = createSensorMessage(nodeId, "light", completeData, `sensors/brightness/${nodeId}`);
							setMessages((prevMessages) => [message, ...prevMessages].slice(0, 100));
							updated.delete(nodeId);
						}

						return updated;
					});
				} else if (topic.startsWith("sensors/distance/")) {
					const parts = topic.split("/");
					const nodeId = parts[2] || "unknown";
					const field = parts[3];
					const value = payload.toString();

					setPartialUltrasonicData((prev) => {
						const updated = new Map(prev);
						const current = updated.get(nodeId) || {};

						if (field === "distance_cm") {
							current.distance_cm = parseFloat(value);
						} else if (field === "estado") {
							current.estado = value as "Cerca" | "Lejos" | "Medio";
						}

						updated.set(nodeId, current);

						if (current.distance_cm !== undefined && current.estado !== undefined) {
							const completeData: UltrasonicSensorData = {
								distance_cm: current.distance_cm,
								estado: current.estado,
							};

							const message = createSensorMessage(nodeId, "ultrasonic", completeData, `sensors/distance/${nodeId}`);
							setMessages((prevMessages) => [message, ...prevMessages].slice(0, 100));
							updated.delete(nodeId);
						}

						return updated;
					});
				} else if (topic.startsWith("status/")) {
					const parts = topic.split("/");
					const nodeId = parts[1] || "unknown";
					const field = parts[2];

					if (field === "last_update") {
						const timestamp = Number(payload.toString());
						if (!Number.isNaN(timestamp)) {
							const statusMsg: StatusMessage = {
								node_id: nodeId,
								msg_type: "status",
								last_update: timestamp,
							};

							const messageRecord: MessageRecord = {
								...statusMsg,
								id: `${Date.now()}-${Math.random()}`,
								receivedAt: new Date(),
								topic,
							};

							setMessages((prevMessages) => [messageRecord, ...prevMessages].slice(0, 100));
						}
					}
				}
			} catch (err) {
				console.error("Error processing message:", err);
			}
		};

		mqttClient.on("message", handleMessage);

		return () => {
			if (typeof mqttClient.off === "function") {
				mqttClient.off("message", handleMessage);
			} else {
				mqttClient.removeListener("message", handleMessage);
			}
		};
	}, [mqttClient]);

	const clearMessages = () => setMessages([]);

	const value = useMemo(
		() => ({
			messages,
			clearMessages,
		}),
		[messages]
	);

	return <MessageContext.Provider value={value}>{children}</MessageContext.Provider>;
}

export function useMessageContext() {
	const ctx = useContext(MessageContext);
	if (!ctx) throw new Error("useMessageContext must be used within a MessageProvider");
	return ctx;
}
