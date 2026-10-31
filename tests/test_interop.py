"""End-to-end C++/Python WAV cross-decoding and bit-for-bit PCM compatibility."""

import argparse
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from gibber import COMMAND_FRIES, SAMPLE_RATE, decode, encode


class CrossLanguageTests(unittest.TestCase):
    binary = None

    def test_cross_language_wav(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            python_wav = folder / "python.wav"
            cpp_wav = folder / "cpp.wav"
            expected = np.int16(encode() * 32767)
            wavfile.write(python_wav, SAMPLE_RATE, expected)

            output = subprocess.run(
                [str(self.binary), "decode", str(python_wav)],
                check=True, capture_output=True, text=True,
            )
            self.assertIn("47 01 21 53 d3", output.stdout)
            self.assertIn("would you like fries with that?", output.stdout)

            subprocess.run([str(self.binary), "encode", str(cpp_wav)], check=True, capture_output=True)
            rate, actual = wavfile.read(cpp_wav)
            self.assertEqual(rate, SAMPLE_RATE)
            np.testing.assert_array_equal(actual, expected)
            command, packet, offset = decode(actual.astype(float) / 32768.0)
            self.assertEqual(command, COMMAND_FRIES)
            self.assertEqual(packet.hex(), "47012153d3")
            self.assertEqual(offset, 384)


if __name__ == "__main__":
    arguments = argparse.ArgumentParser()
    arguments.add_argument("--binary", type=Path, required=True)
    options = arguments.parse_args()
    CrossLanguageTests.binary = options.binary.resolve()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
