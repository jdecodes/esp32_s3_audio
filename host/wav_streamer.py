"""WavStreamer: reads a WAV file and sends it in real time, one packet every PACKET_PERIOD_S."""
import threading
import time

from configs import PACKET_PERIOD_S, RESYNC_THRESHOLD_S, SPIN_MARGIN_S


class WavStreamer:
    """Runs a thread that moves audio from a WavReader to a UdpSender at real-time speed.

    UDP has no flow control and the ESP32 buffers only about 90 ms, so sending the file as fast as
    possible would overflow it. The clock is monotonic and advances by one packet period per packet.
    """

    def __init__(self, reader, sender, loop=False, on_finished=None):
        self._reader = reader
        self._sender = sender
        self._loop = loop
        self._on_finished = on_finished      # called with the error (or None) when the thread ends
        self._stop_event = threading.Event()
        self._finished_event = threading.Event()
        self._thread = None

        self.packets_sent = 0
        self.resyncs = 0                     # times we fell behind and restarted the clock
        self.error = None

    def start(self):
        if self._thread is not None:
            raise RuntimeError("WavStreamer was already started")
        self._thread = threading.Thread(target=self._run, name="WavStreamer", daemon=True)
        self._thread.start()

    def stop(self):
        self._stop_event.set()
        if self._thread is not None and self._thread is not threading.current_thread():
            self._thread.join()

    def wait(self, timeout=None):
        """Waits for the stream to end. Returns True if it ended, False on timeout."""
        return self._finished_event.wait(timeout)

    def _run(self):
        next_send = time.monotonic()          # the first packet goes out immediately
        try:
            while not self._stop_event.is_set():
                packets_this_pass = 0
                while not self._stop_event.is_set():
                    chunk = self._reader.read_chunk()
                    if not chunk:
                        break
                    self._sleep_until(next_send)
                    self._sender.send(chunk)
                    self.packets_sent += 1
                    packets_this_pass += 1

                    next_send += PACKET_PERIOD_S
                    if time.monotonic() - next_send > RESYNC_THRESHOLD_S:
                        next_send = time.monotonic()   # fell behind (OS stall): no burst of catch-up packets
                        self.resyncs += 1

                if not self._loop or packets_this_pass == 0:
                    break
                self._reader.rewind()
        except Exception as exc:              # keep the thread's error for the caller
            self.error = exc
        finally:
            self._finished_event.set()
            if self._on_finished is not None:
                self._on_finished(self.error)

    @staticmethod
    def _sleep_until(deadline):
        remaining = deadline - time.monotonic()
        if remaining > SPIN_MARGIN_S:
            time.sleep(remaining - SPIN_MARGIN_S)     # coarse sleep first
        while time.monotonic() < deadline:            # then spin the last couple of milliseconds
            pass