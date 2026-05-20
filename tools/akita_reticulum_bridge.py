#!/usr/bin/env python3

import argparse
import json
import socket
import string
import sys
import time
from pathlib import Path


def load_rns(reticulum_path: str | None):
    if reticulum_path:
        sys.path.insert(0, reticulum_path)

    try:
        import RNS  # type: ignore
        return RNS
    except ImportError:
        script_dir = Path(__file__).resolve().parent
        fallback = script_dir.parents[1] / "Reticulum"
        if fallback.exists():
            sys.path.insert(0, str(fallback))
            import RNS  # type: ignore
            return RNS
        raise


class AkitaReticulumBridge:
    def __init__(
        self,
        rns,
        config_path: str | None,
        app_name: str,
        aspects: list[str],
        default_destination: str,
        path_timeout: float,
    ):
        self.rns = rns
        self.reticulum = rns.Reticulum(config_path)
        self.app_name = app_name
        self.aspects = aspects
        self.default_destination = default_destination.strip().lower()
        self.path_timeout = max(0.0, path_timeout)
        self.destination_hex_length = (rns.Reticulum.TRUNCATED_HASHLENGTH // 8) * 2
        self.broadcast_destination = rns.Destination(
            None,
            rns.Destination.IN,
            rns.Destination.PLAIN,
            self.app_name,
            *self.aspects,
        )

    def log(self, message: str, level=None):
        if level is None:
            self.rns.log(message)
        else:
            self.rns.log(message, level)

    def validate_destination(self, destination_hash: str) -> str:
        candidate = destination_hash.strip().lower()
        if not candidate:
            return ""

        if len(candidate) != self.destination_hex_length:
            raise ValueError(
                f"Reticulum destination must be {self.destination_hex_length} hex characters"
            )

        if any(char not in string.hexdigits for char in candidate):
            raise ValueError("Reticulum destination must contain only hexadecimal characters")

        return candidate

    def resolve_outbound_destination(self, destination_hash: str):
        if not self.rns.Transport.has_path(bytes.fromhex(destination_hash)):
            self.log(
                f"No known Reticulum path for {destination_hash}; requesting path",
                self.rns.LOG_INFO,
            )
            self.rns.Transport.request_path(bytes.fromhex(destination_hash))
            deadline = time.time() + self.path_timeout
            while self.path_timeout > 0 and time.time() < deadline:
                if self.rns.Transport.has_path(bytes.fromhex(destination_hash)):
                    break
                time.sleep(0.25)

        if not self.rns.Transport.has_path(bytes.fromhex(destination_hash)):
            raise RuntimeError(
                f"No Reticulum path for {destination_hash}. Wait for an announce or configure a reachable destination."
            )

        identity = self.rns.Identity.recall(bytes.fromhex(destination_hash))
        if identity is None:
            raise RuntimeError(
                f"Reticulum path for {destination_hash} exists, but the destination identity is not recalled yet"
            )

        return self.rns.Destination(
            identity,
            self.rns.Destination.OUT,
            self.rns.Destination.SINGLE,
            self.app_name,
            *self.aspects,
        )

    def payload_bytes(self, payload_value) -> bytes:
        return json.dumps(payload_value, separators=(",", ":")).encode("utf-8")

    def publish_envelope(self, envelope: dict):
        if envelope.get("bridge") != "akita-rns-udp-v1":
            raise ValueError("Unsupported bridge envelope")

        destination_hash = self.validate_destination(
            str(envelope.get("destination", self.default_destination) or self.default_destination)
        )
        payload = self.payload_bytes(envelope.get("payload"))

        if destination_hash:
            destination = self.resolve_outbound_destination(destination_hash)
            self.rns.Packet(destination, payload).send()
            self.log(
                f"Forwarded {len(payload)} bytes to {self.rns.prettyhexrep(destination.hash)}",
                self.rns.LOG_INFO,
            )
            return

        self.rns.Packet(self.broadcast_destination, payload).send()
        self.log(
            f"Broadcast {len(payload)} bytes on {self.rns.prettyhexrep(self.broadcast_destination.hash)}",
            self.rns.LOG_INFO,
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Bridge Akita firmware telemetry from rns+udp:// into Reticulum"
    )
    parser.add_argument("--listen-host", default="0.0.0.0", help="UDP host to bind")
    parser.add_argument("--listen-port", type=int, default=4242, help="UDP port to bind")
    parser.add_argument("--config", default=None, help="Optional Reticulum config directory")
    parser.add_argument(
        "--reticulum-path",
        default=None,
        help="Path to a local Reticulum checkout if RNS is not installed",
    )
    parser.add_argument(
        "--app-name",
        default="akita_car_node",
        help="Reticulum app name used for directed packets and broadcasts",
    )
    parser.add_argument(
        "--aspects",
        nargs="+",
        default=["vehicle_data", "stream"],
        help="Reticulum destination aspects for directed packets and broadcasts",
    )
    parser.add_argument(
        "--default-destination",
        default="",
        help="Fallback Reticulum destination hash when the firmware leaves destination empty",
    )
    parser.add_argument(
        "--path-timeout",
        type=float,
        default=5.0,
        help="Seconds to wait after requesting a missing Reticulum path",
    )
    args = parser.parse_args()

    try:
        rns = load_rns(args.reticulum_path)
    except ImportError as exc:
        print(f"Unable to import RNS: {exc}", file=sys.stderr)
        return 1

    bridge = AkitaReticulumBridge(
        rns=rns,
        config_path=args.config,
        app_name=args.app_name,
        aspects=args.aspects,
        default_destination=args.default_destination,
        path_timeout=args.path_timeout,
    )

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.listen_host, args.listen_port))

    bridge.log(
        f"Akita Reticulum bridge listening on udp://{args.listen_host}:{args.listen_port}",
        bridge.rns.LOG_INFO,
    )
    bridge.log(
        "Using Reticulum broadcast destination "
        + bridge.rns.prettyhexrep(bridge.broadcast_destination.hash)
        + f" for {args.app_name}.{'.'.join(args.aspects)} when no directed destination is supplied",
        bridge.rns.LOG_INFO,
    )

    while True:
        try:
            data, address = sock.recvfrom(4096)
            envelope = json.loads(data.decode("utf-8"))
            bridge.publish_envelope(envelope)
        except KeyboardInterrupt:
            print("")
            return 0
        except Exception as exc:
            bridge.log(
                f"Bridge receive from {address[0]}:{address[1]} failed: {exc}",
                bridge.rns.LOG_ERROR,
            )


if __name__ == "__main__":
    raise SystemExit(main())
