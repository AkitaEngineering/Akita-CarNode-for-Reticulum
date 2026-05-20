#!/usr/bin/env python3

import argparse
import json
import socket
import string
import sys
import time
from pathlib import Path


BRIDGE_PROTOCOL = "akita-rns-udp-v2"
SUPPORTED_BRIDGE_PROTOCOLS = {"akita-rns-udp-v1", BRIDGE_PROTOCOL}


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
        delivery_attempts: int,
        delivery_backoff_seconds: float,
        delivery_backoff_factor: float,
        delivery_backoff_max: float,
    ):
        self.rns = rns
        self.reticulum = rns.Reticulum(config_path)
        self.app_name = app_name
        self.aspects = aspects
        self.default_destination = default_destination.strip().lower()
        self.path_timeout = max(0.0, path_timeout)
        self.delivery_attempts = max(1, delivery_attempts)
        self.delivery_backoff_seconds = max(0.0, delivery_backoff_seconds)
        self.delivery_backoff_factor = max(1.0, delivery_backoff_factor)
        self.delivery_backoff_max = max(self.delivery_backoff_seconds, delivery_backoff_max)
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

    def deliver_directed(self, destination_hash: str, payload: bytes):
        backoff = self.delivery_backoff_seconds
        last_error = None

        for attempt in range(1, self.delivery_attempts + 1):
            try:
                destination = self.resolve_outbound_destination(destination_hash)
                receipt = self.rns.Packet(destination, payload).send()
                if receipt is None:
                    raise RuntimeError("Packet send did not return a receipt")
                return destination, attempt
            except Exception as exc:
                last_error = exc
                if attempt >= self.delivery_attempts:
                    break

                self.log(
                    f"Directed delivery attempt {attempt}/{self.delivery_attempts} failed for {destination_hash}: {exc}. Retrying in {backoff:.2f} seconds",
                    self.rns.LOG_INFO,
                )
                self.rns.Transport.request_path(bytes.fromhex(destination_hash))
                if backoff > 0:
                    time.sleep(backoff)
                backoff = min(self.delivery_backoff_max, backoff * self.delivery_backoff_factor)

        raise RuntimeError(
            f"Directed delivery failed after {self.delivery_attempts} attempts: {last_error}"
        )

    def bridge_response(self, status: str, request: str, sequence=None, **fields) -> dict:
        response = {
            "bridge": BRIDGE_PROTOCOL,
            "status": status,
            "request": request,
        }
        if sequence is not None:
            response["sequence"] = sequence
        response.update(fields)
        return response

    def handle_envelope(self, envelope: dict) -> dict:
        bridge_protocol = str(envelope.get("bridge", "") or "")
        request = str(envelope.get("kind", "telemetry") or "telemetry")
        sequence = envelope.get("sequence")

        if bridge_protocol not in SUPPORTED_BRIDGE_PROTOCOLS:
            raise ValueError("Unsupported bridge envelope")

        if request == "ping":
            return self.bridge_response(
                "ok",
                request,
                sequence=sequence,
                mode="bridge_ready",
                app_name=self.app_name,
                aspects=self.aspects,
            )

        if request != "telemetry":
            raise ValueError(f"Unsupported bridge request type: {request}")

        destination_hash = self.validate_destination(
            str(envelope.get("destination", self.default_destination) or self.default_destination)
        )
        payload = self.payload_bytes(envelope.get("payload"))

        if destination_hash:
            destination, attempts = self.deliver_directed(destination_hash, payload)
            self.log(
                f"Forwarded {len(payload)} bytes to {self.rns.prettyhexrep(destination.hash)}",
                self.rns.LOG_INFO,
            )
            return self.bridge_response(
                "ok",
                request,
                sequence=sequence,
                mode="directed",
                destination=destination.hexhash,
                bytes=len(payload),
                attempts=attempts,
            )

        self.rns.Packet(self.broadcast_destination, payload).send()
        self.log(
            f"Broadcast {len(payload)} bytes on {self.rns.prettyhexrep(self.broadcast_destination.hash)}",
            self.rns.LOG_INFO,
        )
        return self.bridge_response(
            "ok",
            request,
            sequence=sequence,
            mode="broadcast",
            destination=self.broadcast_destination.hexhash,
            bytes=len(payload),
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
    parser.add_argument(
        "--delivery-attempts",
        type=int,
        default=3,
        help="Total directed delivery attempts before returning an error",
    )
    parser.add_argument(
        "--delivery-backoff-seconds",
        type=float,
        default=0.5,
        help="Initial backoff in seconds between directed delivery retries",
    )
    parser.add_argument(
        "--delivery-backoff-factor",
        type=float,
        default=2.0,
        help="Multiplier applied to the directed delivery retry backoff",
    )
    parser.add_argument(
        "--delivery-backoff-max",
        type=float,
        default=4.0,
        help="Maximum backoff in seconds between directed delivery retries",
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
        delivery_attempts=args.delivery_attempts,
        delivery_backoff_seconds=args.delivery_backoff_seconds,
        delivery_backoff_factor=args.delivery_backoff_factor,
        delivery_backoff_max=args.delivery_backoff_max,
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
        address = None
        envelope = None
        try:
            data, address = sock.recvfrom(4096)
            envelope = json.loads(data.decode("utf-8"))
            response = bridge.handle_envelope(envelope)
            sock.sendto(json.dumps(response, separators=(",", ":")).encode("utf-8"), address)
        except KeyboardInterrupt:
            print("")
            return 0
        except Exception as exc:
            source = "unknown peer"
            if address is not None:
                source = f"{address[0]}:{address[1]}"

            if address is not None:
                request = "unknown"
                sequence = None
                if isinstance(envelope, dict):
                    request = str(envelope.get("kind", request) or request)
                    sequence = envelope.get("sequence")

                error_response = bridge.bridge_response(
                    "error",
                    request,
                    sequence=sequence,
                    message=str(exc),
                )
                sock.sendto(json.dumps(error_response, separators=(",", ":")).encode("utf-8"), address)

            bridge.log(
                f"Bridge receive from {source} failed: {exc}",
                bridge.rns.LOG_ERROR,
            )


if __name__ == "__main__":
    raise SystemExit(main())
