import argparse

from configs import UDP_PORT
from mic_controller import MicController
from wifi_manager import ensure_wifi

def parse_args():
    parser = argparse.ArgumentParser(
        description="Control the ESP32 Wi-Fi microphone from the terminal."
    )
    parser.add_argument(
        "--host",
        default="192.168.4.1",
        help="ESP32 SoftAP IP address (default: 192.168.4.1)",
    )
    parser.add_argument(
        "--udp-port",
        type=int,
        default=UDP_PORT,
        help=f"Local UDP listening port (default: {UDP_PORT})",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    #ensure_wifi()

    controller = MicController(
        host=args.host,
        udp_port=args.udp_port,
    )

    print()
    print("ESP32 Wi-Fi Microphone")
    print("----------------------")
    print("Commands: start_rec, stop_rec, stats, quit")
    print()

    try:
        controller.connect()

        while True:
            try:
                command = input("mic> ").strip().lower()
            except EOFError:
                break

            if command in ("start", "s"):
                try:
                    controller.start_recording()
                except Exception as exc:
                    print(f"Could not start recording: {exc}")

            elif command in ("stop", "x"):
                try:
                    controller.stop_recording()
                except Exception as exc:
                    print(f"Stop warning: {exc}")

            elif command in ("stats", "status"):
                controller.print_stats()

            elif command.startswith("tone"):
                parts = command.split()

                if len(parts) != 2 or parts[1] not in ("on", "off"):
                    print("Usage: tone on | tone off")
                    continue

                controller.set_test_tone(parts[1] == "on")

            elif command in ("quit", "q", "exit"):
                break

            elif command:
                print("Commands: start, stop, tone on, tone off, stats, quit")

    except KeyboardInterrupt:
        print("\nInterrupted.")

    except Exception as exc:
        print(f"\nCould not connect to ESP32: {exc}")

    finally:
        controller.close()


if __name__ == "__main__":
    main()
