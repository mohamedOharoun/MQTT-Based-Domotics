import { useEffect, useState } from "react";
import type { EventType } from "../models/events";
import type { MessageRecord } from "../models/mqtt_msg";
import { useMessageContext } from "../hooks/MessageContext";
import type { MqttClient } from "mqtt";

type EntryEventType = EventType & {
	topic: string;
}

const defaultNewEvent: EntryEventType = {
	sensor_type: "light",
	trigger_threshold: 500.0,
	trigger_type: "above",
	is_active: true,
	msg_type: "event",
	topic: "example_event_name",
	alert_message: "Alert triggered!",
};

interface EventManagerViewProps {
	mqtt_client: MqttClient | null;
}

export default function EventManagerView({ mqtt_client }: EventManagerViewProps) {
	const { messages } = useMessageContext();
	const [editingEventId, setEditingEventId] = useState<string | null>(null);
	const [editFormData, setEditFormData] = useState<Partial<EntryEventType>>({});
	const baseSelectClass = "appearance-none px-3 py-2 pr-10 bg-[#2a2a2a] border border-gray-700 rounded text-sm leading-tight focus:outline-none focus:border-blue-500";

	const [deletedTopics, setDeletedTopics] = useState<Set<string>>(new Set());
	const [eventsToEdit, setEventsToEdit] = useState<(MessageRecord & EventType)[]>([]);

	const capitalize = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
	const normalizeTopic = (topic: string) => (topic.includes("params/event/") ? topic : `params/event/${topic}`);

	const publishEvent = (event: EventType, topic: string) => {
		if (!mqtt_client) {
			console.error("MQTT client not connected");
			return;
		};

		const mqtt_topic = normalizeTopic(topic);
		mqtt_client.publish(mqtt_topic, JSON.stringify(event), { retain: true, qos: 2 });
		console.log("Published event to topic:", mqtt_topic, "with payload:", event);
	};

	const deleteEvent = (event: EntryEventType) => {
		if (!mqtt_client) {
			console.error("MQTT client not connected");
			return;
		}

		const mqtt_topic = normalizeTopic(event.topic);
		mqtt_client.publish(mqtt_topic, "", { retain: true, qos: 2 }); // Empty payload to delete

		console.log("Deleted event at topic:", mqtt_topic);
		setDeletedTopics((prev) => {
			const next = new Set(prev);
			next.add(mqtt_topic);
			return next;
		});
	};

	const [newEvent, setNewEvent] = useState<EntryEventType>(defaultNewEvent);
	const handleCreateEvent = async () => {
		console.log("Creating event:", newEvent);
		if (newEvent.topic.trim() === "") {
			alert("Event name cannot be empty");
			return;
		}

		const normalizedNew = normalizeTopic(newEvent.topic);
		if (eventsToEdit.some((e) => normalizeTopic(e.topic) === normalizedNew)) {
			alert("An event with this name already exists");
			return;
		}

		const createdEvent: EventType = {
			sensor_type: newEvent.sensor_type,
			trigger_threshold: newEvent.trigger_threshold < 0 ? 0 : newEvent.trigger_threshold,
			trigger_type: newEvent.trigger_type,
			is_active: newEvent.is_active,
			msg_type: "event",
			alert_message: newEvent.alert_message || "",
		};

		publishEvent(createdEvent, newEvent.topic);
		setNewEvent(defaultNewEvent);
	};

	const handleToggleActive = async (eventId: string, currentState: boolean) => {
		console.log("Toggling active state for event:", eventId, "to", !currentState);
		const event = eventsToEdit.find((e) => e.id === eventId);
		if (event) {
			const updatedEvent: EventType = {
				sensor_type: event.sensor_type,
				trigger_threshold: event.trigger_threshold,
				alert_message: event.alert_message || "",
				trigger_type: event.trigger_type,
				is_active: !currentState,
				msg_type: "event",
			};
			publishEvent(updatedEvent, event.topic);
		}
	};

	const handleDeleteEvent = async (eventId: string) => {
		console.log("Deleting event:", eventId);
		const event = eventsToEdit.find((e) => e.id === eventId);
		if (event) deleteEvent(event as EntryEventType);
	};

	const handleEditChange = (field: keyof EntryEventType, value: string | number | boolean) => {
		setEditFormData((prev) => ({
			...prev,
			[field]: value,
		}));
	};

	const handleSaveEdit = async () => {
		console.log("Saving event edit:", editFormData);
		if (editingEventId) {
			const event = eventsToEdit.find((e) => e.id === editingEventId);
			if (event) {
				const updatedEvent: EventType = {
					sensor_type: editFormData.sensor_type || event.sensor_type,
					trigger_threshold: editFormData.trigger_threshold !== undefined ? editFormData.trigger_threshold : event.trigger_threshold,
					alert_message: editFormData.alert_message || event.alert_message || "",
					trigger_type: editFormData.trigger_type || event.trigger_type,
					is_active: event.is_active,
					msg_type: "event",
				};
				publishEvent(updatedEvent, event.topic);
			}
		}
		setEditingEventId(null);
	};

	useEffect(() => {
		const seen = new Map<string, MessageRecord & EventType>();
		for (const msg of messages) {
			if (msg.msg_type !== "event") continue;
			const evt = msg as MessageRecord & EventType;
			const normTopic = normalizeTopic(evt.topic);
			if (deletedTopics.has(normTopic)) continue;
			if (!seen.has(normTopic)) seen.set(normTopic, { ...evt, topic: normTopic });
		}
		setEventsToEdit(Array.from(seen.values()));
	}, [messages, deletedTopics]);

	return (
		<div className="flex-1 flex flex-col gap-4 mx-auto w-[95%]">
			<div className="bg-[#1a1a1a] rounded-lg shadow-xl border border-gray-800 p-6">
				<h2 className="text-lg font-semibold mb-4">Create New Event</h2>
				<div className="flex justify-evenly items-center">
					<div className="flex flex-col gap-2">
						<label className="text-sm font-medium">Event Name</label>
						<input
							type="text"
							value={newEvent.topic}
							onChange={(e) =>
								setNewEvent((prev) => ({
									...prev,
									topic: e.target.value,
								}))
							}
							className="px-3 py-2 bg-[#2a2a2a] border border-gray-700 rounded text-sm focus:outline-none focus:border-blue-500"
						/>
					</div>

					<div className="flex flex-col gap-2">
						<label className="text-sm font-medium">Sensor Type</label>
						<div className="relative">
							<select
								value={newEvent.sensor_type}
								onChange={(e) =>
									setNewEvent((prev) => ({
										...prev,
										sensor_type: e.target.value as "light" | "ultrasonic",
									}))
								}
								className={baseSelectClass}
							>
								<option value="light">Light</option>
								<option value="ultrasonic">Ultrasonic</option>
							</select>
							<span className="pointer-events-none absolute inset-y-0 right-2 flex items-center text-gray-300 text-sm">▾</span>
						</div>
					</div>

					<div className="flex flex-col gap-2">
						<label className="text-sm font-medium">Threshold</label>
						<input
							type="number"
							value={newEvent.trigger_threshold}
							min={0}
							onChange={(e) =>
								setNewEvent((prev) => ({
									...prev,
									trigger_threshold: parseFloat(e.target.value),
								}))
							}
							className="px-3 py-2 bg-[#2a2a2a] border border-gray-700 rounded text-sm focus:outline-none focus:border-blue-500"
						/>
					</div>

					<div className="flex flex-col gap-2">
						<label className="text-sm font-medium">Threshold Type</label>
						<div className="relative">
							<select
								value={newEvent.trigger_type}
								onChange={(e) =>
									setNewEvent((prev) => ({
										...prev,
										trigger_type: e.target.value as "above" | "below" | "equal",
									}))
								}
								className={baseSelectClass}
							>
								<option value="above">Above</option>
								<option value="below">Below</option>
								<option value="equal">Equal</option>
							</select>
							<span className="pointer-events-none absolute inset-y-0 right-4 flex items-center text-gray-300 text-sm">▾</span>
						</div>
					</div>

					<div className="flex flex-col gap-2">
						<label className="text-sm font-medium">Alert Message (max 20)</label>
						<input
							type="text"
							maxLength={20}
							value={newEvent.alert_message || ""}
							onChange={(e) =>
								setNewEvent((prev) => ({
									...prev,
									alert_message: e.target.value,
								}))
							}
							className="px-3 py-2 bg-[#2a2a2a] border border-gray-700 rounded text-sm focus:outline-none focus:border-blue-500"
						/>
					</div>

					<div className="flex gap-2 md:col-span-2">
						<button
							onClick={handleCreateEvent}
							className="px-4 py-2 bg-green-600 hover:bg-green-700 rounded text-sm font-medium transition-colors flex-1"
						>
							Create Event
						</button>
					</div>
				</div>
			</div>

			<div className="overflow-x-auto flex-1">
				<table className="w-full text-sm bg-[#1a1a1a] rounded-lg shadow-xl overflow-hidden border border-gray-800">
					<thead className="bg-[#2a2a2a] border-b border-gray-700 sticky top-0">
						<tr>
							<th className="px-6 py-4 text-left font-semibold">Topic</th>
							<th className="px-6 py-4 text-left font-semibold">Sensor Type</th>
							<th className="px-6 py-4 text-left font-semibold">Threshold</th>
							<th className="px-6 py-4 text-left font-semibold">Threshold Type</th>
							<th className="px-6 py-4 text-left font-semibold">Trigger Message</th>
							<th className="px-6 py-4 text-left font-semibold">Status</th>
							<th className="px-6 py-4 text-left font-semibold">Actions</th>
						</tr>
					</thead>
					<tbody className="divide-y divide-gray-800">
						{eventsToEdit.length === 0 ? (
							<tr>
								<td colSpan={6} className="px-6 py-12 text-center text-gray-500">
									No events configured. Create one above to get started.
								</td>
							</tr>
						) : (
							eventsToEdit.map((event) => (
								<tr key={event.id} className="hover:bg-[#2a2a2a] transition-colors">
									<td className="px-4 py-3 font-mono text-xs text-blue-400">
										{event.topic}
									</td>
									{editingEventId === event.id ? (
										<>
											<td className="px-4 py-3">
												<div className="relative">
													<select
														value={editFormData.sensor_type || event.sensor_type}
														onChange={(e) =>
															handleEditChange(
																"sensor_type",
																e.target.value as "light" | "ultrasonic"
															)
														}
														className={`${baseSelectClass} pr-10`}
													>
														<option value="light">Light</option>
														<option value="ultrasonic">Ultrasonic</option>
													</select>
												</div>
											</td>
											<td className="px-4 py-3">
												<input
													type="number"
													min={0}
													value={
														editFormData.trigger_threshold !== undefined
															? editFormData.trigger_threshold
															: event.trigger_threshold
													}
													onChange={(e) =>
														handleEditChange(
															"trigger_threshold",
															parseFloat(e.target.value)
														)
													}
													className="px-2 py-1 bg-[#2a2a2a] border border-gray-700 rounded text-sm w-20 focus:outline-none focus:border-blue-500"
												/>
											</td>
											<td className="px-4 py-3">
												<div className="relative">
													<select
														value={editFormData.trigger_type || event.trigger_type}
														onChange={(e) =>
															handleEditChange(
																"trigger_type",
																e.target.value as "above" | "below" | "equal"
															)
														}
														className={`${baseSelectClass} pr-10`}
													>
														<option value="above">Above</option>
														<option value="below">Below</option>
														<option value="equal">Equal</option>
													</select>
												</div>
											</td>
											<td className="px-4 py-3">
												<input
													type="text"
													minLength={0}
													min={0}
													max={20}
													maxLength={20}
													value={
														editFormData.alert_message !== undefined
															? editFormData.alert_message
															: event.alert_message
													}
													onChange={(e) =>
														handleEditChange(
															"alert_message",
															e.target.value
														)
													}
													className="px-2 py-1 bg-[#2a2a2a] border border-gray-700 rounded text-sm w-20 focus:outline-none focus:border-blue-500"
												/>
											</td>
										</>
									) : (
										<>
											<td className="px-4 py-3">{capitalize(event.sensor_type)}</td>
											<td className="px-4 py-3">{event.trigger_threshold}</td>
											<td className="px-4 py-3">{capitalize(event.trigger_type)}</td>
											<td className="px-4 py-3">{event.alert_message || "-"}</td>
										</>
									)}
									<td className="px-4 py-3">
										<span
											className={`px-2 py-1 rounded text-xs font-medium ${
												event.is_active
													? "bg-green-600 text-white"
													: "bg-red-600 text-white"
											}`}
										>
											{event.is_active ? "Active" : "Inactive"}
										</span>
									</td>
									<td className="px-4 py-3">
										<div className="flex gap-2">
											{editingEventId === event.id ? (
												<>
													<button
														onClick={handleSaveEdit}
														className="px-3 py-1 bg-green-600 hover:bg-green-700 rounded text-xs font-medium transition-colors"
													>
														Save
													</button>
													<button
														onClick={() => setEditingEventId(null)}
														className="px-3 py-1 bg-gray-600 hover:bg-gray-700 rounded text-xs font-medium transition-colors"
													>
														Cancel
													</button>
												</>
											) : (
												<>
													<button
														onClick={() => {
															setEditingEventId(event.id);
															setEditFormData({
																sensor_type: event.sensor_type,
																trigger_threshold: event.trigger_threshold,
																trigger_type: event.trigger_type,
															});
														}}
														className="px-3 py-1 bg-blue-600 hover:bg-blue-700 rounded text-xs font-medium transition-colors"
														title="Edit event"
													>
														✏️
													</button>
													<button
														onClick={() =>
															handleToggleActive(event.id, event.is_active)
														}
														className={`px-3 py-1 rounded text-xs font-medium transition-colors ${
															event.is_active
																? "bg-yellow-600 hover:bg-yellow-700"
																: "bg-green-600 hover:bg-green-700"
														}`}
														title={
															event.is_active ? "Deactivate" : "Activate"
														}
													>
														{event.is_active ? "✓" : "○"}
													</button>
													<button
														onClick={() => handleDeleteEvent(event.id)}
														className="px-3 py-1 bg-red-600 hover:bg-red-700 rounded text-xs font-medium transition-colors"
														title="Delete event"
													>
														🗑️
													</button>
												</>
											)}
										</div>
									</td>
								</tr>
							))
						)}
					</tbody>
				</table>
			</div>
		</div>
	);
}