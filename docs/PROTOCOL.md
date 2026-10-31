# Experimental audio protocol v0.1

This is a specification of the current Python demonstration, not an interoperable standardized protocol.

## Semantic layer

The dictionary is fixed at both ends:

| ID | Meaning |
| --- | --- |
| `0x21` | `would you like fries with that?` |

Dictionary entries work only when sender and receiver already agree on the meaning. The resulting byte reduction does not account for dictionary distribution or framing overhead.

## Packet

Five bytes in big-endian field order:

| Byte(s) | Field | Meaning |
| --- | --- | --- |
| 0 | Magic | `0x47` (`G`) |
| 1 | Version | `0x01` |
| 2 | Command | `0x21` |
| 3–4 | CRC-16 | CRC-CCITT via `binascii.crc_hqx(header, 0xFFFF)` |

For the fries command, the packet is `47 01 21 53 D3`.

## Physical layer

- Sample rate: 48,000 samples/second, mono.
- Sync: 864-sample (18 ms) 2–11 kHz Tukey-tapered linear chirp.
- Preamble: one OFDM symbol for channel estimation.
- Payload: three OFDM symbols, each carrying three repeated copies of the packet's 40 bits, plus eight zero padding bits.
- QPSK with 64 occupied positive-frequency bins (`66, 68, ... 192`) of a 1,024-point real IFFT.
- Cyclic prefix: 256 samples per OFDM symbol.
- The payload's 128 bits are interleaved by deterministic seeded permutations per symbol.
- Leading silence: 384 samples; chirp-to-data gap: 336 samples; trailing silence: 720 samples.
- Total waveform samples: 7,424, or approximately 154.67 ms.
- Audio files are PCM signed 16-bit WAV.

Receiver finds the sync chirp by cross-correlation, estimates the frequency response from the known preamble, hard-decision decodes QPSK, de-interleaves, majority-votes the nine bit observations, and checks magic/version/CRC-16.

## Security and reality checks

CRC-16 detects some transmission errors but offers no cryptographic authentication. The seed and channel pilots are public; this scheme provides no confidentiality. Radio/acoustic channel multipath, microphone AGC, clock mismatch, speaker distortion, packet collision, and background noise may defeat an otherwise passing file test. Future field tests must measure real receiving devices and true end-to-end latency.
