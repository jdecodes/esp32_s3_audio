import json

import websocket


class ControlClient:
    """Small WebSocket client for the ESP32 control endpoint."""

    def __init__(self, host="192.168.4.1", path="/ws", timeout_sec=3.0):
        self.url = f"ws://{host}{path}"
        self.timeout_sec = timeout_sec
        self._ws = None

    @property
    def connected(self):
        return self._ws is not None and self._ws.connected

    def connect(self):
        if self.connected:
            return

        self._ws = websocket.create_connection(
            self.url,
            timeout=self.timeout_sec,
            enable_multithread=True,
        )

    def _send(self, message):
        if not self.connected:
            raise ConnectionError("Not connected to the ESP32 control WebSocket")

        self._ws.send(json.dumps(message))

    def hello(self, udp_port):
        """Register this client's UDP destination with the ESP32."""
        self._send({
            "type": "hello_rec",
            "udp_port": int(udp_port),
        })

        response_text = self._ws.recv()
        try:
            response = json.loads(response_text)
        except (TypeError, json.JSONDecodeError) as exc:
            raise RuntimeError(
                f"Invalid HELLO response from ESP32: {response_text!r}"
            ) from exc

        if response.get("type") == "error":
            raise RuntimeError(
                f"ESP32 rejected HELLO: {response.get('message', response)}"
            )

        if response.get("type") != "hello_rec" or response.get("ok") is not True:
            raise RuntimeError(f"Unexpected HELLO response: {response}")

        return response

    def hello_playback(self, udp_port):
        """Register this client's UDP destination with the ESP32."""
        self._send({
            "type": "hello_play",
            "udp_port": int(udp_port),
        })

        response_text = self._ws.recv()
        try:
            response = json.loads(response_text)
        except (TypeError, json.JSONDecodeError) as exc:
            raise RuntimeError(
                f"Invalid HELLO response from ESP32: {response_text!r}"
            ) from exc

        if response.get("type") == "error":
            raise RuntimeError(
                f"ESP32 rejected HELLO: {response.get('message', response)}"
            )

        if response.get("type") != "hello_play" or response.get("ok") is not True:
            raise RuntimeError(f"Unexpected HELLO response: {response}")

        return response
    
    def start_recording(self):
        # The current firmware queues START but does not send a START ACK.
        self._send({"type": "start_rec"})

    def stop_recording(self):
        # The current firmware queues STOP but does not send a STOP ACK.
        self._send({"type": "stop_rec"})

    def start_playback(self):
        # Like start_recording: the firmware queues the command and sends no acknowledgement.
        self._send({"type": "start_play"})

    def stop_playback(self):
        self._send({"type": "stop_play"})

    def poll_message(self, wait_sec=0.2):
        """Returns a pending message from the ESP32 as a dict, or None if nothing arrives within wait_sec."""
        if not self.connected:
            raise ConnectionError("Not connected to the ESP32 control WebSocket")

        self._ws.settimeout(wait_sec)
        try:
            text = self._ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        finally:
            self._ws.settimeout(self.timeout_sec)

        try:
            return json.loads(text)
        except (TypeError, json.JSONDecodeError):
            return None
        
    def set_test_tone(self, enabled: bool):
        self._send({
            "type": "enable_test_tone",
            "enabled": bool(enabled),
        })

    def close(self):
        if self._ws is not None:
            try:
                self._ws.close()
            finally:
                self._ws = None
