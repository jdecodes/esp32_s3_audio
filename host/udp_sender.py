"""UdpSender: owns the UDP socket, the destination and the sequence counter. Knows nothing about WAV files."""
import socket
import struct

from configs import HEADER_FORMAT, MAX_DATA_BYTES, SEQ_MASK


class UdpSender:
    """Sends audio packets to the ESP32.

    packet = header + data
      header: seq      uint32 little-endian
      data:   samples  int16 little-endian, mono, up to FRAMES_PER_PACKET
    """

    def __init__(self):
        self._socket = None
        self._destination = None
        self._next_seq = 0

    def open(self):
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def set_destination(self, host, port):
        self._destination = (host, int(port))

    def reset_sequence(self):
        self._next_seq = 0

    def send(self, samples):
        """Sends one packet. `samples` is little-endian int16 mono bytes."""
        if self._socket is None or self._destination is None:
            raise RuntimeError("UdpSender is not open or has no destination")
        if not samples or len(samples) > MAX_DATA_BYTES:
            raise ValueError(f"a packet carries 1..{MAX_DATA_BYTES} data bytes, got {len(samples)}")

        packet = struct.pack(HEADER_FORMAT, self._next_seq) + samples
        self._socket.sendto(packet, self._destination)
        self._next_seq = (self._next_seq + 1) & SEQ_MASK

    def close(self):
        if self._socket is not None:
            self._socket.close()
            self._socket = None