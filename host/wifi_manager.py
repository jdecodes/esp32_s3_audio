
import subprocess
import tempfile
import time
import os
from xml.sax.saxutils import escape

SSID = "Jd-Mic"
PASSWORD = "jdecodes@1234"

CONNECT_TIMEOUT_SEC = 20

def run_netsh(*args):
    return subprocess.run(
        ["netsh", "wlan", *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def get_current_ssid():
    result = run_netsh("show", "interfaces")

    for line in result.stdout.splitlines():
        line = line.strip()

        if line.startswith("SSID") and not line.startswith("BSSID"):
            return line.split(":", 1)[1].strip()

    return None


def create_wifi_profile(ssid, password):
    xml = f"""<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
    <name>{escape(ssid)}</name>
    <SSIDConfig>
        <SSID>
            <name>{escape(ssid)}</name>
        </SSID>
    </SSIDConfig>
    <connectionType>ESS</connectionType>
    <connectionMode>manual</connectionMode>
    <MSM>
        <security>
            <authEncryption>
                <authentication>WPA2PSK</authentication>
                <encryption>AES</encryption>
                <useOneX>false</useOneX>
            </authEncryption>
            <sharedKey>
                <keyType>passPhrase</keyType>
                <protected>false</protected>
                <keyMaterial>{escape(password)}</keyMaterial>
            </sharedKey>
        </security>
    </MSM>
</WLANProfile>
"""

    path = None

    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            suffix=".xml",
            encoding="utf-8",
            delete=False,
        ) as file:
            file.write(xml)
            path = file.name

        # Replace the saved profile for this SSID.
        run_netsh("wlan", "delete", "profile", f"name={ssid}")

        result = run_netsh(
            "add",
            "profile",
            f"filename={path}",
            "user=current",
        )

        if result.returncode != 0:
            if not "already exists" in result.stdout:
                print(f"error code {result.returncode}")
                print(f" stderr {result.stderr}")
                print(f" stdout {result.stdout}")
                raise RuntimeError(result.stdout + result.stderr)

    finally:
        if path and os.path.exists(path):
            os.remove(path)


def connect_wifi(ssid):
    print(f"Connecting to {ssid}...")

    result = run_netsh(
        "connect",
        f"name={ssid}",
        f"ssid={ssid}",
    )

    if result.returncode != 0:
        raise RuntimeError(result.stdout + result.stderr)

    for _ in range(CONNECT_TIMEOUT_SEC):
        time.sleep(1)

        if get_current_ssid() == ssid:
            print(f"Connected to {ssid}")
            return

    raise TimeoutError(f"Could not connect to {ssid}")


def set_network_private(ssid):
    print("Setting Windows network profile to Private...")

    # Escape single quotes for PowerShell.
    safe_ssid = ssid.replace("'", "''")

    command = (
        f"$p = Get-NetConnectionProfile | "
        f"Where-Object {{ $_.Name -eq '{safe_ssid}' }}; "
        f"if ($null -eq $p) {{ throw 'Network profile not found' }}; "
        f"$p | Set-NetConnectionProfile -NetworkCategory Private"
    )

    result = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            command,
        ],
        capture_output=True,
        text=True,
    )

    if result.returncode != 0:
        raise RuntimeError(
            "Connected to Wi-Fi, but could not set Private profile. "
            "Try running the terminal as Administrator.\n"
            + result.stderr
        )

    print("Windows network profile is Private.")


def ensure_wifi():
    if get_current_ssid() == SSID:
        print(f"Already connected to {SSID}")
    else:
        print(f"Creating Wi-Fi profile for {SSID}...")
        create_wifi_profile(SSID, PASSWORD)
        connect_wifi(SSID)

    set_network_private(SSID)

    print("Wi-Fi setup complete.")
