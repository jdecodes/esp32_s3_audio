"""WavReader: reads a WAV file as mono 16-bit PCM in packet-sized chunks."""
import struct
import wave

from configs import BYTES_PER_SAMPLE, FRAMES_PER_PACKET, SAMPLE_RATE_HZ

MONO = 1
STEREO = 2
CONVERT_HINT = "convert it with:  ffmpeg -i input.wav -ar 16000 -ac 1 output.wav"


class WavFormatError(ValueError):
    """The WAV file is not in a format the ESP32 speaker path accepts."""


class WavReader:
    """Reads a WAV file in chunks of up to FRAMES_PER_PACKET mono samples.

    Accepted files: 16-bit PCM, mono or stereo, at SAMPLE_RATE_HZ. Stereo is mixed down to mono.
    Chunks are little-endian int16 bytes, ready to go into a packet.
    """

    def __init__(self, path):
        self.path = path
        self._wav = None
        self._channels = 0

    def open(self):
        try:
            wav = wave.open(str(self.path), "rb")
        except wave.Error as exc:
            raise WavFormatError(f"{self.path}: not a PCM WAV file ({exc}); {CONVERT_HINT}") from exc

        try:
            self._check_format(wav)
        except WavFormatError:
            wav.close()
            raise
        self._wav = wav
        self._channels = wav.getnchannels()

    def _check_format(self, wav):
        if wav.getsampwidth() != BYTES_PER_SAMPLE:
            raise WavFormatError(
                f"{self.path}: {wav.getsampwidth() * 8}-bit samples, need 16-bit; {CONVERT_HINT}")
        if wav.getnchannels() not in (MONO, STEREO):
            raise WavFormatError(
                f"{self.path}: {wav.getnchannels()} channels, need mono or stereo; {CONVERT_HINT}")
        if wav.getframerate() != SAMPLE_RATE_HZ:
            raise WavFormatError(
                f"{self.path}: {wav.getframerate()} Hz, need {SAMPLE_RATE_HZ} Hz; {CONVERT_HINT}")

    @property
    def duration_s(self):
        return self._wav.getnframes() / SAMPLE_RATE_HZ

    def read_chunk(self, frames=FRAMES_PER_PACKET):
        """Returns up to `frames` mono samples as little-endian int16 bytes. Empty bytes at the end of the file."""
        raw = self._wav.readframes(frames)
        if not raw or self._channels == MONO:
            return raw
        return self._mix_down_to_mono(raw)

    @staticmethod
    def _mix_down_to_mono(raw):
        # stereo frame = [left int16][right int16]
        sample_count = len(raw) // BYTES_PER_SAMPLE
        samples = struct.unpack(f"<{sample_count}h", raw)
        left = samples[0::STEREO]
        right = samples[1::STEREO]
        mono = [(l + r) // 2 for l, r in zip(left, right)]
        return struct.pack(f"<{len(mono)}h", *mono)

    def rewind(self):
        self._wav.rewind()

    def close(self):
        if self._wav is not None:
            self._wav.close()
            self._wav = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *exc_info):
        self.close()