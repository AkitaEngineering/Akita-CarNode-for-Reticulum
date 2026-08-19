#!/usr/bin/env python3

import json
import unittest
from unittest.mock import patch

from akita_reticulum_bridge import BRIDGE_PROTOCOL, AkitaReticulumBridge


class FakePacket:
    def __init__(self, destination, payload):
        self.destination = destination
        self.payload = payload

    def send(self):
        return object()


class FakeDestination:
    IN = "in"
    OUT = "out"
    PLAIN = "plain"
    SINGLE = "single"

    def __init__(self, identity, direction, kind, app_name, *aspects):
        self.identity = identity
        self.direction = direction
        self.kind = kind
        self.app_name = app_name
        self.aspects = aspects
        self.hash = b"\x11" * 16
        self.hexhash = "11" * 16


class FakeRNS:
    LOG_INFO = 3
    LOG_ERROR = 1

    class Reticulum:
        TRUNCATED_HASHLENGTH = 128

        def __init__(self, config_path):
            self.config_path = config_path

    class Destination:
        IN = "in"
        OUT = "out"
        PLAIN = "plain"
        SINGLE = "single"

        def __init__(self, identity, direction, kind, app_name, *aspects):
            self.identity = identity
            self.direction = direction
            self.kind = kind
            self.app_name = app_name
            self.aspects = aspects
            self.hash = b"\x22" * 16
            self.hexhash = "22" * 16

    class Transport:
        paths = set()

        @classmethod
        def has_path(cls, destination_hash):
            return destination_hash in cls.paths

        @classmethod
        def request_path(cls, destination_hash):
            cls.paths.add(destination_hash)

    class Identity:
        @staticmethod
        def recall(destination_hash):
            return object()

    Packet = FakePacket

    @staticmethod
    def log(message, level=None):
        return None

    @staticmethod
    def prettyhexrep(value):
        return value.hex()


def make_bridge(**overrides):
    FakeRNS.Transport.paths = set()
    kwargs = dict(
        rns=FakeRNS,
        config_path=None,
        app_name="akita_car_node",
        aspects=["vehicle_data", "stream"],
        default_destination="",
        path_timeout=0.0,
        delivery_attempts=2,
        delivery_backoff_seconds=0.0,
        delivery_backoff_factor=2.0,
        delivery_backoff_max=1.0,
        delivery_deadline_seconds=1.0,
    )
    kwargs.update(overrides)
    return AkitaReticulumBridge(**kwargs)


class BridgeTests(unittest.TestCase):
    def test_ping_returns_ok(self):
        bridge = make_bridge()
        response = bridge.handle_envelope({"bridge": BRIDGE_PROTOCOL, "kind": "ping", "sequence": 3})
        self.assertEqual(response["status"], "ok")
        self.assertEqual(response["request"], "ping")
        self.assertEqual(response["sequence"], 3)
        self.assertEqual(response["mode"], "bridge_ready")

    def test_legacy_protocol_is_accepted(self):
        bridge = make_bridge()
        response = bridge.handle_envelope({"bridge": "akita-rns-udp-v1", "kind": "ping"})
        self.assertEqual(response["status"], "ok")

    def test_unknown_protocol_is_rejected(self):
        bridge = make_bridge()
        with self.assertRaises(ValueError):
            bridge.handle_envelope({"bridge": "nope", "kind": "ping"})

    def test_broadcast_telemetry(self):
        bridge = make_bridge()
        response = bridge.handle_envelope(
            {
                "bridge": BRIDGE_PROTOCOL,
                "kind": "telemetry",
                "sequence": 9,
                "destination": "",
                "payload": {"node_id": "AkitaCarNode", "rpm": 900},
            }
        )
        self.assertEqual(response["status"], "ok")
        self.assertEqual(response["mode"], "broadcast")
        self.assertEqual(response["bytes"], len(json.dumps({"node_id": "AkitaCarNode", "rpm": 900}, separators=(",", ":"))))

    def test_invalid_destination_is_rejected(self):
        bridge = make_bridge()
        with self.assertRaises(ValueError):
            bridge.handle_envelope(
                {
                    "bridge": BRIDGE_PROTOCOL,
                    "kind": "telemetry",
                    "destination": "zzzz",
                    "payload": {"ok": True},
                }
            )

    def test_directed_telemetry_uses_known_path(self):
        destination = "ab" * 16
        FakeRNS.Transport.paths.add(bytes.fromhex(destination))
        bridge = make_bridge()
        with patch.object(FakeRNS, "Destination", FakeDestination):
            response = bridge.handle_envelope(
                {
                    "bridge": BRIDGE_PROTOCOL,
                    "kind": "telemetry",
                    "destination": destination,
                    "payload": {"ok": True},
                }
            )
        self.assertEqual(response["status"], "ok")
        self.assertEqual(response["mode"], "directed")
        self.assertEqual(response["attempts"], 1)

    def test_unsupported_request_type(self):
        bridge = make_bridge()
        with self.assertRaises(ValueError):
            bridge.handle_envelope({"bridge": BRIDGE_PROTOCOL, "kind": "shutdown"})


if __name__ == "__main__":
    raise SystemExit(unittest.main())
