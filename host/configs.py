CHANNEL_COUNT = 1          # mono
SAMPLE_WIDTH_BYTES = 2     # int16

SAMPLE_RATE_HZ = 16000      # must match SAMPLE_RATE in the firmware
UDP_PORT = 5000             # must match HOST_PORT in the firmware
STATS_INTERVAL_SEC = 1.0

RECORDING_DIR = r"E:\mcu_recordings"


"""Constants for the playback path (computer -> ESP32 speaker).

If your configs.py already defines the audio format, import it from there instead,
so there is one source of truth.
"""
import struct

# ---- ESP32 ----
ESP_HOST = "192.168.4.1"      # the ESP32 SoftAP address

# hello always needs a udp_port: it is where the ESP32 sends microphone audio.
# Playback does not use it, but the firmware requires a valid number.
HELLO_UDP_PORT = 5000

# ---- audio format (must match audio_config.h in the firmware) ----
SAMPLE_RATE_HZ = 16000
CHANNEL_COUNT = 1                 # mono
BYTES_PER_SAMPLE = 2              # int16, little-endian
FRAMES_PER_PACKET = 512           # audio samples per UDP packet
PACKET_PERIOD_S = FRAMES_PER_PACKET / SAMPLE_RATE_HZ   # 0.032 s of audio per packet

# ---- packet layout (little-endian) ----
# packet = header + data
#   header: seq      uint32                           (4 bytes)
#   data:   samples  int16 x up to FRAMES_PER_PACKET  (mono)
HEADER_FORMAT = "<I"
HEADER_SIZE_BYTES = struct.calcsize(HEADER_FORMAT)
MAX_DATA_BYTES = FRAMES_PER_PACKET * BYTES_PER_SAMPLE
SEQ_MASK = 0xFFFFFFFF             # seq wraps at 32 bits

# ---- pacing ----
# UDP has no flow control and the ESP32 buffers only about 90 ms, so the sender must pace itself.
SPIN_MARGIN_S = 0.002             # sleep until this long before the deadline, then spin (Windows sleep is coarse)
RESYNC_THRESHOLD_S = 0.1          # if we fall this far behind, restart the clock instead of sending a burst

# ---- session timing ----
START_SETTLE_S = 0.3              # the firmware sends no acknowledgement for start_playback yet
STOP_SETTLE_S = 0.3               # lets the ESP32 play out what it has buffered
REJECT_CHECK_S = 0.3              # how long to wait for an "error" reply after start_playback
WAIT_POLL_S = 0.2                 # Ctrl-C needs a timeout on Windows

# ---- files ----
DEFAULT_WAV_PATH = "test_tone.wav"