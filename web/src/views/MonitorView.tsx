import { useMemo, useState } from "react";
import type { MessageRecord, StatusMessage } from "../models/mqtt_msg";
import type { LightSensorData, SensorDataMessage, UltrasonicSensorData } from "../models/sensor_data";
import type { MsgType } from "../models/incoming_msg";

import type { EventTriggerMessage, EventType } from "../models/events";
import type { MQTTServerConnectionStatus } from "../models/mqtt_status";
import { useMessageContext } from "../hooks/MessageContext";

interface MonitorViewProps {
	connectionStatus: MQTTServerConnectionStatus;
}

export default function MonitorView({ connectionStatus }: MonitorViewProps) {
  const { messages, clearMessages } = useMessageContext();
  const [msgTypeFilter, setMsgTypeFilter] = useState<MsgType | "all">("all");

  const filteredMessages = useMemo(
    () =>
      msgTypeFilter === "all"
        ? messages
        : messages.filter((message) => message.msg_type === msgTypeFilter),
    [messages, msgTypeFilter]
  );

  const formatUnixOrMillis = (value: number) => {
    const date = value > 1e12 ? new Date(value) : new Date(value * 1000);
    return date.toLocaleString("es-ES");
  };

  const formatReceivedAt = (date: Date) => {
    return date.toLocaleTimeString();
  };

  const renderSensorData = (message: MessageRecord) => {
    if (message.msg_type !== "sensor_data") return null;
    
    const sensorMsg = message as SensorDataMessage;
    
    if (sensorMsg.sensor_type === "light") {
      const data = sensorMsg.data as LightSensorData;
      return (
        <div className="text-xs">
          <div>Lux: {data.lux.toFixed(2)}</div>
          <div>ALS: {data.als}</div>
          <div>Estado: {data.estado}</div>
        </div>
      );
    } else if (sensorMsg.sensor_type === "ultrasonic") {
      const data = sensorMsg.data as UltrasonicSensorData;
      return (
        <div className="text-xs">
          <div>Distance: {data.distance_cm.toFixed(2)} cm</div>
          <div>Estado: {data.estado}</div>
        </div>
      );
    }
  };

  const renderEventData = (message: MessageRecord) => {
    if (message.msg_type !== "event") return null;

    const eventMsg = message as EventType;
    return (
      <div className="text-xs">
        <div>Event ID: {eventMsg.event_id}</div>
        <div>Sensor: {eventMsg.sensor_type}</div>
        <div>Threshold: {eventMsg.trigger_threshold}</div>
        <div>Type: {eventMsg.trigger_type}</div>
        <div>Active: {eventMsg.is_active ? "Yes" : "No"}</div>
        <div>Message: {eventMsg.alert_message}</div>
				<div>Target Device: {eventMsg.target_device}</div>
				<div>Target Device Value: {eventMsg.target_device_value}</div>
      </div>
    );
  };

  const renderStatusData = (message: MessageRecord) => {
    if (message.msg_type !== "status") return null;

    const statusMsg = message as StatusMessage;
    return (
      <div className="text-xs">
        <div>Last Update: {formatUnixOrMillis(statusMsg.last_update)}</div>
      </div>
    );
  };

	const renderEventTriggerData = (message: MessageRecord) => {
		if (message.msg_type !== "alert") return null;
		const alertMsg = message as EventTriggerMessage;
		return (
			<div className="text-xs">
				<div>Event ID: {alertMsg.event.event_id}</div>
				<div>Event Sensor: {alertMsg.event.sensor_type}</div>
				<div>Trigger Threshold: {alertMsg.event.trigger_threshold}</div>
				<div>Trigger Type: {alertMsg.event.trigger_type}</div>
				<div>Is Active: {alertMsg.event.is_active ? "Yes" : "No"}</div>
				<div>Message: "{alertMsg.event.alert_message}"</div>
				<div>Target Device: {alertMsg.event.target_device}</div>
				<div>Target Device Value: {alertMsg.event.target_device_value}</div>
				<div>Timestamp: {formatUnixOrMillis(alertMsg.timestamp)}</div>
			</div>
		);
	}

  const getMessageTypeColor = (msgType: MsgType) => {
    switch (msgType) {
      case "sensor_data": return "bg-blue-600";
      case "alert": return "bg-red-600";
      case "event": return "bg-yellow-600";
      case "status": return "bg-purple-600";
      default: return "bg-gray-600";
    }
  };

  return (
      <div className="flex-1 flex flex-col gap-4 mx-auto w-[95%]">
        <div className="flex flex-col gap-4 max-w-2xl mx-auto w-[65%]">
					<div className="flex items-center gap-2">
          <div className={`w-3 h-3 rounded-full ${
            connectionStatus === "connected" ? "bg-green-500" : 
            connectionStatus === "connecting" ? "bg-yellow-500 animate-pulse" : 
            "bg-red-500"
          }`}></div>
          <span className="text-sm font-medium capitalize">{connectionStatus}</span>
        </div>
        
        <button
          onClick={clearMessages}
          className="px-4 py-2 bg-red-600 hover:bg-red-700 rounded text-sm font-medium transition-colors"
        >
          Clear Messages
        </button>
        
        <div className="flex items-center gap-4 text-sm text-gray-200">
          <label className="text-gray-400" htmlFor="msg-type-filter">Filter by type</label>
          <select
            id="msg-type-filter"
            value={msgTypeFilter}
            onChange={(e) => setMsgTypeFilter(e.target.value as MsgType | "all")}
            className="bg-[#2a2a2a] border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-blue-600"
          >
            <option value="all">All</option>
            <option value="sensor_data">sensor_data</option>
            <option value="alert">alert</option>
            <option value="event">event</option>
            <option value="status">status</option>
          </select>
          <span className="text-gray-400">Showing {filteredMessages.length} of {messages.length}</span>
        </div>
				</div>

				<div className="overflow-x-auto flex-1">
          <table className="w-full text-sm bg-[#1a1a1a] rounded-lg shadow-xl overflow-hidden border border-gray-800">
            <thead className="bg-[#2a2a2a] border-b border-gray-700 sticky top-0">
              <tr>
                <th className="px-6 py-4 text-left font-semibold">Received</th>
                <th className="px-6 py-4 text-left font-semibold">Topic</th>
                <th className="px-6 py-4 text-left font-semibold">Node ID</th>
                <th className="px-6 py-4 text-left font-semibold">Type</th>
                <th className="px-6 py-4 text-left font-semibold">Data</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-gray-800">
              {filteredMessages.length === 0 ? (
                <tr>
                  <td colSpan={7} className="px-6 py-12 text-center text-gray-500">
                    {messages.length === 0
                      ? connectionStatus === "connected"
                        ? "No messages received yet. Waiting for data..."
                        : "Connecting to MQTT broker..."
                      : "No messages match the selected type."}
                  </td>
                </tr>
              ) : (
                filteredMessages.map((msg) => (
                  <tr key={msg.id} className="hover:bg-[#2a2a2a] transition-colors">
                    <td className="px-4 py-3 whitespace-nowrap text-gray-400">
                      {formatReceivedAt(msg.receivedAt)}
                    </td>
                    <td className="px-4 py-3 font-mono text-xs text-blue-400">
                      {msg.topic}
                    </td>
                    <td className="px-4 py-3 font-mono">
                      {"node_id" in msg ? msg.node_id : "-"}
                    </td>
                    <td className="px-4 py-3">
                      <span className={`px-2 py-1 rounded text-xs font-medium ${getMessageTypeColor(msg.msg_type)}`}>
                        {msg.msg_type}
                      </span>
                    </td>
                    <td className="px-4 py-3">
                      {msg.msg_type === "sensor_data" && renderSensorData(msg)}
                      {msg.msg_type === "event" && renderEventData(msg)}
                      {msg.msg_type === "status" && renderStatusData(msg)}
											{msg.msg_type === "alert" && renderEventTriggerData(msg)}
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