"""Reproducible, file-free unit tests for the experimental modem."""

import binascii
import unittest

import numpy as np

from gibber import COMMAND_FRIES, COMMANDS, decode, encode, packet_for_command


class GibberCodecTests(unittest.TestCase):
    def test_packet_is_stable(self):
        packet = packet_for_command(COMMAND_FRIES)
        self.assertEqual(packet.hex(), "47012153d3")
        self.assertEqual(int.from_bytes(packet[3:], "big"), binascii.crc_hqx(packet[:3], 0xFFFF))

    def test_clean_roundtrip(self):
        command, packet, offset = decode(encode())
        self.assertEqual(command, COMMAND_FRIES)
        self.assertEqual(COMMANDS[command], "would you like fries with that?")
        self.assertEqual(packet, packet_for_command(COMMAND_FRIES))
        self.assertEqual(offset, 384)

    def test_waveform_duration(self):
        self.assertEqual(len(encode()), 7424)

    def test_additive_noise_15db(self):
        signal = encode()
        rng = np.random.default_rng(12)
        rms = np.sqrt(np.mean(signal ** 2))
        noise = rng.standard_normal(len(signal)) * (rms / (10 ** (15 / 20)))
        command, _, _ = decode(signal + noise)
        self.assertEqual(command, COMMAND_FRIES)


if __name__ == "__main__":
    unittest.main()
