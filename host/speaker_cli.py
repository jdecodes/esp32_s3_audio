"""Plays a WAV file on the ESP32 speaker.

    python speaker_cli.py                       plays DEFAULT_WAV_PATH
    python speaker_cli.py path/to/file.wav      plays that file
    python speaker_cli.py file.wav --loop       repeats until Ctrl-C

Join the Jd-Mic Wi-Fi first. The WAV must be 16-bit PCM at 16 kHz, mono or stereo.
"""
import argparse
import sys

from configs import DEFAULT_WAV_PATH, ESP_HOST
from playback_controller import PlaybackController
from wav_reader import WavFormatError


def parse_args():
    parser = argparse.ArgumentParser(description="Play a WAV file on the ESP32 speaker")
    parser.add_argument("wav", nargs="?", default=DEFAULT_WAV_PATH, help="WAV file to play")
    parser.add_argument("--host", default=ESP_HOST, help="ESP32 address")
    parser.add_argument("--loop", action="store_true", help="repeat the file until Ctrl-C")
    return parser.parse_args()


def main():
    args = parse_args()
    controller = PlaybackController(host=args.host)
    try:
        controller.start(args.wav, loop=args.loop)
        print(f"playing {args.wav} on {args.host} (Ctrl-C to stop)")
        controller.wait()
        print(f"done, {controller.streamer.packets_sent} packets sent"
              if controller.streamer else "done")
    except KeyboardInterrupt:
        print("stopping")
    except (WavFormatError, FileNotFoundError, RuntimeError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        controller.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())