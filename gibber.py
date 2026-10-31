#!/usr/bin/env python3
"""Gibber on My Link v0.1: semantic commands over an audible OFDM burst.

This is an experimental, independently verifiable acoustic protocol, NOT a
GGWave/GibberLink compatible recording. Requires numpy and scipy.

Usage:
  python gibber.py encode output.wav
  python gibber.py decode output.wav
  python gibber.py test
"""

import argparse
import binascii
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import chirp, correlate, correlation_lags, windows

SAMPLE_RATE = 48_000
NFFT = 1024
CP = 256
CARRIERS = np.arange(66, 194, 2)  # 64 frequencies: 3.09–9.00 kHz
SYMBOL_SAMPLES = NFFT + CP
LEAD_SAMPLES = 384               # 8 ms quiet before transmission
CHIRP_SAMPLES = 864              # 18 ms sync chirp
GAP_SAMPLES = 336                # 7 ms silence between sync and OFDM
TAIL_SAMPLES = 720               # 15 ms tail
NUM_DATA_SYMBOLS = 3
MAGIC = 0x47
VERSION = 0x01
COMMAND_FRIES = 0x21
COMMANDS = {COMMAND_FRIES: "would you like fries with that?"}
SEED = 20261030


def sync_chirp():
    t = np.arange(CHIRP_SAMPLES) / SAMPLE_RATE
    waveform = chirp(t, f0=2_000, f1=11_000,
                     t1=CHIRP_SAMPLES / SAMPLE_RATE, method="linear")
    return waveform * windows.tukey(CHIRP_SAMPLES, alpha=0.35)


def qpsk_map(bits):
    pairs = bits.reshape(-1, 2).astype(np.int16)
    return ((1 - 2 * pairs[:, 0]) + 1j * (1 - 2 * pairs[:, 1])) / np.sqrt(2)


def ofdm_symbol(values):
    bins = np.zeros(NFFT // 2 + 1, dtype=complex)
    bins[CARRIERS] = values
    samples = np.fft.irfft(bins, n=NFFT)
    return np.concatenate([samples[-CP:], samples])


def preamble():
    rng = np.random.default_rng(SEED)
    bits = rng.integers(0, 2, 2 * len(CARRIERS), dtype=np.uint8)
    return qpsk_map(bits)


def packet_for_command(command_id):
    header = bytes([MAGIC, VERSION, command_id])
    crc = binascii.crc_hqx(header, 0xFFFF)
    return header + crc.to_bytes(2, "big")


def permutation(symbol_index):
    rng = np.random.default_rng(SEED + 100 + symbol_index)
    return rng.permutation(128)


def encode(command_id=COMMAND_FRIES):
    packet = packet_for_command(command_id)
    bits = np.unpackbits(np.frombuffer(packet, dtype=np.uint8))
    # Three copies within each data symbol; 8 known filler bits.
    repeated = np.concatenate([np.tile(bits, 3), np.zeros(8, dtype=np.uint8)])
    symbols = [ofdm_symbol(preamble())]
    for index in range(NUM_DATA_SYMBOLS):
        symbols.append(ofdm_symbol(qpsk_map(repeated[permutation(index)])))
    ofdm = np.concatenate(symbols)
    # Ramp only frame endpoints to avoid hard clicks.
    fade = np.ones(len(ofdm))
    n = 48
    fade[:n] = np.linspace(0, 1, n)
    fade[-n:] = np.linspace(1, 0, n)
    ofdm *= fade

    burst = np.concatenate([
        np.zeros(LEAD_SAMPLES),
        sync_chirp() * 0.02,  # normalized together below
        np.zeros(GAP_SAMPLES),
        ofdm,
        np.zeros(TAIL_SAMPLES),
    ])
    # Ensure sync tone is clearly audible even if data waveform has low amplitude.
    start = LEAD_SAMPLES
    stop = start + CHIRP_SAMPLES
    burst[start:stop] = sync_chirp() * max(np.max(np.abs(ofdm)), 0.01) * 0.7
    burst *= 0.82 / np.max(np.abs(burst))
    return burst


def locate_sync(signal):
    corr = correlate(signal, sync_chirp(), mode="valid", method="fft")
    start = int(np.argmax(np.abs(corr)))
    return start


def decode(signal):
    # Synchronize by detecting the known frequency sweep.
    chirp_start = locate_sync(signal)
    start = chirp_start + CHIRP_SAMPLES + GAP_SAMPLES
    needed = (NUM_DATA_SYMBOLS + 1) * SYMBOL_SAMPLES
    if start + needed > len(signal):
        raise ValueError("Incomplete OFDM burst")

    def fft_symbol(symbol_index):
        pos = start + symbol_index * SYMBOL_SAMPLES
        time = signal[pos + CP:pos + CP + NFFT]
        return np.fft.rfft(time)[CARRIERS]

    # Channel response is inferred from a known full-band training symbol.
    channel = fft_symbol(0) / preamble()
    if np.min(np.abs(channel)) < 1e-8:
        raise ValueError("Training signal missing")

    # Nine independent hard decisions per bit: three spectral copies x three
    # time copies. Majority vote, then a CRC-16 validity check.
    copies = []
    for symbol_index in range(NUM_DATA_SYMBOLS):
        equalized = fft_symbol(symbol_index + 1) / channel
        encoded_bits = np.empty(128, dtype=np.uint8)
        encoded_bits[0::2] = (np.real(equalized) < 0).astype(np.uint8)
        encoded_bits[1::2] = (np.imag(equalized) < 0).astype(np.uint8)
        ordered = np.empty_like(encoded_bits)
        ordered[permutation(symbol_index)] = encoded_bits
        copies.extend(ordered[:120].reshape(3, 40))
    recovered_bits = (np.sum(np.array(copies), axis=0) >= 5).astype(np.uint8)
    packet = np.packbits(recovered_bits).tobytes()
    if packet[0] != MAGIC or packet[1] != VERSION:
        raise ValueError("Unrecognized frame/version")
    crc = binascii.crc_hqx(packet[:3], 0xFFFF)
    if int.from_bytes(packet[3:5], "big") != crc:
        raise ValueError("CRC failed; damaged transmission")
    return packet[2], packet, chirp_start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["encode", "decode", "test"])
    parser.add_argument("file", nargs="?", default="samples/gibber_fries.wav")
    args = parser.parse_args()

    if args.action == "encode":
        waveform = encode()
        wavfile.write(args.file, SAMPLE_RATE, np.int16(waveform * 32767))
        print(f"Wrote {args.file}: {len(waveform)/SAMPLE_RATE*1000:.1f} ms")
        print(f"Command {COMMAND_FRIES:#04x} -> {COMMANDS[COMMAND_FRIES]!r}")
        return
    if args.action == "decode":
        rate, audio = wavfile.read(args.file)
        if rate != SAMPLE_RATE:
            raise ValueError(f"Expected {SAMPLE_RATE} Hz, got {rate}")
        data = audio.astype(float) / 32768.0
        if data.ndim != 1:
            data = data.mean(axis=1)
        code, packet, offset = decode(data)
        print(f"Synchronized at sample {offset}")
        print(f"CRC-verified packet: {packet.hex(' ')}")
        print(f"Decoded command: {code:#04x} -> {COMMANDS.get(code, 'unknown command')}")
        return
    if args.action == "test":
        reference = encode()
        for snr in [None, 35, 25, 20, 15]:
            rng = np.random.default_rng(12)
            noise = np.zeros_like(reference) if snr is None else rng.standard_normal(len(reference)) * (np.sqrt(np.mean(reference**2)) / 10**(snr/20))
            audio = reference + noise
            code, packet, sync_at = decode(audio)
            assert code == COMMAND_FRIES
            print(f"SNR={snr or 'clean':>5} dB: PASS, sync sample {sync_at}, packet {packet.hex(' ')}")


if __name__ == "__main__":
    main()