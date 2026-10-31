"""PlaybackController: one playback session = control connection + UDP sender + paced WAV streaming."""
import time

from control_client import ControlClient
from configs import (
    ESP_HOST, HELLO_UDP_PORT, REJECT_CHECK_S, START_SETTLE_S, STOP_SETTLE_S, WAIT_POLL_S,
)
from udp_sender import UdpSender
from wav_reader import WavReader
from wav_streamer import WavStreamer


class PlaybackController:
    """Orchestrates playback to the ESP32 speaker, the way MicController orchestrates recording.

    Order of events:
      connect -> hello (learn the playback port) -> start_playback -> stream paced UDP packets
      -> stop_playback -> close

    The firmware accepts ONE control session; a newer one replaces the older. To run the microphone
    and the speaker together, create one ControlClient and pass it to both controllers.
    """

    def __init__(self, host=ESP_HOST, control_client=None, hello_udp_port=HELLO_UDP_PORT):
        self._host = host
        self._owns_control = control_client is None
        self._control = control_client if control_client is not None else ControlClient(host=host)
        # With a shared session, pass the microphone's UDP port here so this hello does not change it.
        self._hello_udp_port = hello_udp_port

        self._reader = None
        self._sender = UdpSender()
        self._streamer = None
        self._playback_started = False

    @property
    def streamer(self):
        return self._streamer

    def start(self, wav_path, loop=False, on_finished=None):
        """Opens the file, starts playback on the ESP32 and begins streaming. Raises on any failure."""
        self._reader = WavReader(wav_path)
        self._reader.open()
        try:
            self._control.connect()
            hello = self._control.hello_playback(self._hello_udp_port)
            
            print(f"{hello}")
            playback_port = 5000 
            """playback_port = hello.get("playback_port")"""
            if playback_port is None:
                raise RuntimeError(
                    "this firmware has no playback support (the hello reply has no playback_port)")

            self._sender.open()
            self._sender.set_destination(self._host, playback_port)
            self._sender.reset_sequence()

            self._control.start_playback()
            self._playback_started = True
            self._raise_if_rejected()
            time.sleep(START_SETTLE_S)

            self._streamer = WavStreamer(self._reader, self._sender, loop=loop, on_finished=on_finished)
            self._streamer.start()
        except Exception:
            self.stop()
            raise

    def _raise_if_rejected(self):
        # The firmware does not acknowledge start_playback; it only answers with an "error" message.
        message = self._control.poll_message(REJECT_CHECK_S)
        if message is not None and message.get("type") == "error":
            raise RuntimeError(f"ESP32 rejected start_playback: {message.get('message', message)}")

    def wait(self):
        """Blocks until the file has been sent (never, with loop=True, until stop() or Ctrl-C)."""
        while not self._streamer.wait(WAIT_POLL_S):
            pass
        if self._streamer.error is not None:
            raise self._streamer.error

    def stop(self):
        """Stops streaming and playback and releases everything. Safe to call more than once."""
        if self._streamer is not None:
            self._streamer.stop()
            self._streamer = None

        if self._playback_started:
            try:
                self._control.stop_playback()
                time.sleep(STOP_SETTLE_S)          # the ESP32 plays out what it has buffered
            except Exception:
                pass                               # the connection may already be gone
            self._playback_started = False

        self._sender.close()
        if self._reader is not None:
            self._reader.close()
            self._reader = None
        if self._owns_control:
            self._control.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.stop()
        
