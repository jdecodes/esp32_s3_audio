import os
import queue
import time

from control_client import ControlClient
from streaming_player import StreamingPlayer
from udp_receiver import END_OF_STREAM, UdpReceiver
from wav_writer import WavWriter

from configs import (
    CHANNEL_COUNT,
    RECORDING_DIR,
    SAMPLE_RATE_HZ,
    SAMPLE_WIDTH_BYTES,
    UDP_PORT,
)


# The current ESP32 firmware does not send a STOP acknowledgement.
# Give its stream task time to consume the queued STOP command.
STOP_DRAIN_SEC = 0.20
THREAD_JOIN_TIMEOUT_SEC = 5.0


class MicController:
    """Coordinates the ESP32 control connection and one local audio session.

    Existing UdpReceiver, StreamingPlayer and WavWriter classes are reused
    unchanged. A fresh set of these objects is created for every recording.
    """

    def __init__(
        self,
        host="192.168.4.1",
        udp_port=UDP_PORT,
        recording_dir=RECORDING_DIR,
    ):
        self.host = host
        self.udp_port = int(udp_port)
        self.recording_dir = recording_dir

        self.control = ControlClient(host=host)

        self._recording = False
        self._player = None
        self._receiver = None
        self._wav_writer = None
        self._player_queue = None
        self._wav_queue = None
        self._session_started_at = None
        self._last_file_name = None
        self._last_stats = None

    @property
    def recording(self):
        return self._recording

    def connect(self):
        """Connect once; the WebSocket can be reused across recording sessions."""
        self.control.connect()
        print(f"Connected to ESP32 control endpoint: {self.control.url}")

    def set_test_tone(self, enabled: bool):
        if not self._recording:
            print("Start recording before enabling test tone.")
            return

        self.control.set_test_tone(enabled)

        state = "enabled" if enabled else "disabled"
        print(f"Test tone {state}.")
    
    def start_recording(self):
        if self._recording:
            print("Already recording.")
            return

        if not self.control.connected:
            self.connect()

        os.makedirs(self.recording_dir, exist_ok=True)

        # Each consumer gets its own queue, as in the original main.py.
        self._player_queue = queue.Queue()
        self._wav_queue = queue.Queue()

        self._player = StreamingPlayer(self._player_queue, SAMPLE_RATE_HZ)
        self._wav_writer = WavWriter(self._wav_queue, SAMPLE_RATE_HZ)
        self._receiver = UdpReceiver(
            self.udp_port,
            [self._player_queue, self._wav_queue],
        )

        try:
            # Start local consumers and bind the UDP receiver BEFORE asking
            # the ESP32 to transmit.
            self._player.start()
            self._wav_writer.start()
            self._receiver.start()

            # The existing UdpReceiver does not expose a "socket bound" event.
            # Give its thread a moment to enter run() before the HELLO round trip.
            time.sleep(0.05)
            if not self._receiver.is_alive():
                raise RuntimeError(
                    "UDP receiver exited during startup. Check whether "
                    f"UDP port {self.udp_port} is already in use."
                )

            # HELLO is acknowledged by the ESP32. This round trip also gives
            # the receiver thread time to bind its UDP socket.
            self.control.hello(self.udp_port)

            # START is queued by the ESP32. Current firmware sends no ACK.
            self.control.start_recording()

            self._recording = True
            self._session_started_at = time.monotonic()
            print("Recording started.")
            print(f"Receiving UDP audio on port {self.udp_port}")

        except Exception:
            # If HELLO succeeded but START failed partway through, make a
            # best-effort STOP before tearing down the local receiver.
            if self.control.connected:
                try:
                    self.control.stop_recording()
                except Exception:
                    pass
            self._shutdown_local_pipeline()
            raise

    def stop_recording(self):
        if not self._recording:
            print("Not recording.")
            return

        control_error = None
        try:
            self.control.stop_recording()
            time.sleep(STOP_DRAIN_SEC)
        except Exception as exc:
            control_error = exc

        stats = self.get_stats()

        # Capture the path before _shutdown_local_pipeline clears the writer.
        if self._wav_writer is not None:
            self._last_file_name = self._wav_writer.get_file_name()

        self._shutdown_local_pipeline()

        self._recording = False
        self._last_stats = stats
        file_name = self._last_file_name

        print("Recording stopped.")
        if file_name:
            print(f"Saved: {file_name}")

        if control_error is not None:
            raise RuntimeError(
                f"Local recording stopped, but sending STOP to ESP32 failed: "
                f"{control_error}"
            ) from control_error

    def get_stats(self):
        """Return a snapshot of the current session counters."""
        if self._receiver is None or self._wav_writer is None:
            return self._last_stats or {
                "packets_received": 0,
                "packets_lost": 0,
                "underruns": 0,
                "buffered_ms": 0.0,
                "wav_bytes": 0,
                "audio_seconds": 0.0,
                "wall_seconds": 0.0,
            }

        bytes_per_second = (
            SAMPLE_RATE_HZ * SAMPLE_WIDTH_BYTES * CHANNEL_COUNT
        )
        wav_bytes = self._wav_writer.bytes_written
        wall_seconds = (
            time.monotonic() - self._session_started_at
            if self._session_started_at is not None
            else 0.0
        )

        return {
            "packets_received": self._receiver.packets_received,
            "packets_lost": self._receiver.packets_lost,
            "underruns": self._player.underrun_count,
            "buffered_ms": self._player.buffered_ms,
            "wav_bytes": wav_bytes,
            "audio_seconds": wav_bytes / bytes_per_second,
            "wall_seconds": wall_seconds,
        }

    def print_stats(self):
        stats = self.get_stats()
        if not self._recording and self._last_stats is None:
            print("No recording session yet.")
            return

        print(
            f"received={stats['packets_received']} "
            f"lost={stats['packets_lost']} "
            f"underruns={stats['underruns']} "
            f"buffered={stats['buffered_ms']:.0f}ms "
            f"wav_bytes={stats['wav_bytes']} "
            f"audio={stats['audio_seconds']:.1f}s "
            f"wall={stats['wall_seconds']:.1f}s"
        )

    def _shutdown_local_pipeline(self):
        """Stop local threads and ensure the WAV writer receives EOS."""
        receiver = self._receiver
        player = self._player
        writer = self._wav_writer

        if receiver is not None:
            try:
                receiver.stop()
                receiver.join(timeout=THREAD_JOIN_TIMEOUT_SEC)
            except RuntimeError:
                # Thread may not have been started if an earlier startup
                # operation failed.
                pass

        # UdpReceiver normally pushes END_OF_STREAM itself on exit. Push it
        # here as a fallback for a receiver that failed before reaching its
        # normal cleanup path. Duplicate sentinels are harmless.
        if self._player_queue is not None:
            self._player_queue.put(END_OF_STREAM)
        if self._wav_queue is not None:
            self._wav_queue.put(END_OF_STREAM)

        if writer is not None and writer.ident is not None:
            writer.join(timeout=THREAD_JOIN_TIMEOUT_SEC)
            if writer.is_alive():
                print("Warning: WAV writer did not finish before timeout.")

        if player is not None:
            try:
                player.stop()
            except Exception as exc:
                print(f"Warning: could not stop audio playback cleanly: {exc}")

        self._receiver = None
        self._player = None
        self._wav_writer = None
        self._player_queue = None
        self._wav_queue = None
        self._session_started_at = None

    def close(self):
        try:
            if self._recording:
                self.stop_recording()
        finally:
            self.control.close()
            print("Control connection closed.")
