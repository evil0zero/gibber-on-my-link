#include "gibber.hpp"
#include "gibber_tables.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace std;

namespace gibber {
namespace {

constexpr size_t kFft = 1024;
constexpr size_t kPrefix = 256;
constexpr size_t kCarriers = 64;
constexpr size_t kSymbol = kFft + kPrefix;
constexpr size_t kLead = 384;
constexpr size_t kSync = 864;
constexpr size_t kGap = 336;
constexpr size_t kTail = 720;
constexpr size_t kDataSymbols = 3;
constexpr double kPi = 3.141592653589793238462643383279502884;
using Complex = complex<double>;

void fft(vector<Complex>& values, bool inverse) {
    const size_t size = values.size();
    if (size == 0 || (size & (size - 1)) != 0) {
        throw invalid_argument("FFT length must be a power of two");
    }
    for (size_t index = 1, swapped = 0; index < size; ++index) {
        size_t bit = size >> 1;
        for (; swapped & bit; bit >>= 1) {
            swapped ^= bit;
        }
        swapped ^= bit;
        if (index < swapped) {
            swap(values[index], values[swapped]);
        }
    }
    for (size_t length = 2; length <= size; length <<= 1) {
        const double angle = (inverse ? 2.0 : -2.0) * kPi / double(length);
        const Complex rotation(cos(angle), sin(angle));
        for (size_t start = 0; start < size; start += length) {
            Complex factor(1.0, 0.0);
            for (size_t offset = 0; offset < length / 2; ++offset) {
                const Complex upper = values[start + offset];
                const Complex lower = values[start + offset + length / 2] * factor;
                values[start + offset] = upper + lower;
                values[start + offset + length / 2] = upper - lower;
                factor *= rotation;
            }
        }
    }
    if (inverse) {
        for (auto& value : values) {
            value /= double(size);
        }
    }
}

vector<double> makeChirp() {
    vector<double> waveform(kSync);
    for (size_t index = 0; index < kSync; ++index) {
        const double t = double(index) / kSampleRate;
        const double span = double(kSync) / kSampleRate;
        const double phase = 2.0 * kPi * (2000.0 * t + 9000.0 * t * t / (2.0 * span));
        const double fraction = double(index) / double(kSync - 1);
        constexpr double alpha = 0.35;
        double window = 1.0;
        if (fraction < alpha / 2.0) {
            window = 0.5 * (1.0 + cos(kPi * (2.0 * fraction / alpha - 1.0)));
        } else if (fraction >= 1.0 - alpha / 2.0) {
            window = 0.5 * (1.0 + cos(kPi * (2.0 * fraction / alpha - 2.0 / alpha + 1.0)));
        }
        waveform[index] = cos(phase) * window;
    }
    return waveform;
}

Complex qpsk(uint8_t pair) {
    constexpr double scale = 0.7071067811865475244;
    const double realPart = (pair & 2) ? -scale : scale;
    const double imaginaryPart = (pair & 1) ? -scale : scale;
    return {realPart, imaginaryPart};
}

vector<double> makeSymbol(const array<Complex, kCarriers>& values) {
    vector<Complex> spectrum(kFft);
    for (size_t index = 0; index < kCarriers; ++index) {
        const size_t bin = 66 + index * 2;
        spectrum[bin] = values[index];
        spectrum[kFft - bin] = conj(values[index]);
    }
    fft(spectrum, true);
    vector<double> result(kSymbol);
    for (size_t index = 0; index < kPrefix; ++index) {
        result[index] = spectrum[kFft - kPrefix + index].real();
    }
    for (size_t index = 0; index < kFft; ++index) {
        result[kPrefix + index] = spectrum[index].real();
    }
    return result;
}

array<Complex, kCarriers> preamble() {
    array<Complex, kCarriers> pilots{};
    for (size_t index = 0; index < kCarriers; ++index) {
        pilots[index] = qpsk(kPilotSymbols[index]);
    }
    return pilots;
}

uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t index = 0; index < length; ++index) {
        crc ^= uint16_t(data[index]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
        }
    }
    return crc;
}

array<uint8_t, 40> packetBits(const array<uint8_t, 5>& packet) {
    array<uint8_t, 40> bits{};
    for (size_t index = 0; index < packet.size(); ++index) {
        for (size_t bit = 0; bit < 8; ++bit) {
            bits[index * 8 + bit] = (packet[index] >> (7 - bit)) & 1;
        }
    }
    return bits;
}

size_t detectSync(const vector<double>& input) {
    const vector<double> chirp = makeChirp();
    if (input.size() < chirp.size()) {
        throw runtime_error("Audio is too short for sync chirp");
    }
    size_t fftLength = 1;
    while (fftLength < input.size() + chirp.size() - 1) {
        fftLength <<= 1;
    }
    vector<Complex> signal(fftLength), kernel(fftLength);
    for (size_t index = 0; index < input.size(); ++index) {
        signal[index] = input[index];
    }
    for (size_t index = 0; index < chirp.size(); ++index) {
        kernel[index] = chirp[chirp.size() - 1 - index];
    }
    fft(signal, false);
    fft(kernel, false);
    for (size_t index = 0; index < fftLength; ++index) {
        signal[index] *= kernel[index];
    }
    fft(signal, true);
    size_t best = 0;
    double score = -1.0;
    for (size_t offset = 0; offset + kSync <= input.size(); ++offset) {
        const double correlation = abs(signal[offset + kSync - 1].real());
        if (correlation > score) {
            score = correlation;
            best = offset;
        }
    }
    return best;
}

array<Complex, kCarriers> readSymbol(const vector<double>& signal, size_t start) {
    vector<Complex> samples(kFft);
    for (size_t index = 0; index < kFft; ++index) {
        samples[index] = signal[start + kPrefix + index];
    }
    fft(samples, false);
    array<Complex, kCarriers> carriers{};
    for (size_t index = 0; index < kCarriers; ++index) {
        carriers[index] = samples[66 + index * 2];
    }
    return carriers;
}

void put16(ostream& stream, uint16_t value) {
    stream.put(char(value & 0xFF));
    stream.put(char((value >> 8) & 0xFF));
}

void put32(ostream& stream, uint32_t value) {
    put16(stream, uint16_t(value));
    put16(stream, uint16_t(value >> 16));
}

uint16_t get16(istream& stream) {
    const int lo = stream.get();
    const int hi = stream.get();
    if (lo == EOF || hi == EOF) {
        throw runtime_error("Truncated WAV file");
    }
    return uint16_t(lo | (hi << 8));
}

uint32_t get32(istream& stream) {
    const uint32_t lo = get16(stream);
    const uint32_t hi = get16(stream);
    return lo | (hi << 16);
}

} // namespace

array<uint8_t, 5> makePacket(uint8_t command) {
    array<uint8_t, 5> packet = {0x47, 0x01, command, 0, 0};
    const uint16_t checksum = crc16(packet.data(), 3);
    packet[3] = uint8_t(checksum >> 8);
    packet[4] = uint8_t(checksum);
    return packet;
}

vector<double> encode(uint8_t command) {
    const auto bits = packetBits(makePacket(command));
    array<uint8_t, 128> repeated{};
    for (size_t index = 0; index < 120; ++index) {
        repeated[index] = bits[index % bits.size()];
    }
    vector<double> ofdm;
    ofdm.reserve((kDataSymbols + 1) * kSymbol);
    auto appendSymbol = [&](const array<Complex, kCarriers>& carriers) {
        const auto symbol = makeSymbol(carriers);
        ofdm.insert(ofdm.end(), symbol.begin(), symbol.end());
    };
    appendSymbol(preamble());
    for (size_t symbolIndex = 0; symbolIndex < kDataSymbols; ++symbolIndex) {
        array<Complex, kCarriers> carriers{};
        for (size_t index = 0; index < kCarriers; ++index) {
            const auto& order = kPermutations[symbolIndex];
            const uint8_t firstBit = repeated[order[index * 2]];
            const uint8_t secondBit = repeated[order[index * 2 + 1]];
            carriers[index] = qpsk(uint8_t((firstBit << 1) | secondBit));
        }
        appendSymbol(carriers);
    }
    constexpr size_t fadeSamples = 48;
    for (size_t index = 0; index < fadeSamples; ++index) {
        ofdm[index] *= double(index) / double(fadeSamples - 1);
        ofdm[ofdm.size() - fadeSamples + index] *= 1.0 - double(index) / double(fadeSamples - 1);
    }
    double ofdmPeak = 0;
    for (double sample : ofdm) {
        ofdmPeak = max(ofdmPeak, abs(sample));
    }
    vector<double> result(kFrameSamples, 0.0);
    const auto chirp = makeChirp();
    for (size_t index = 0; index < kSync; ++index) {
        result[kLead + index] = chirp[index] * max(ofdmPeak, 0.01) * 0.7;
    }
    const size_t ofdmStart = kLead + kSync + kGap;
    copy(ofdm.begin(), ofdm.end(), result.begin() + ofdmStart);
    double maximum = 0;
    for (double sample : result) {
        maximum = max(maximum, abs(sample));
    }
    for (double& sample : result) {
        sample *= 0.82 / maximum;
    }
    return result;
}

DecodedMessage decode(const vector<double>& audio) {
    const size_t chirpStart = detectSync(audio);
    const size_t start = chirpStart + kSync + kGap;
    if (start > audio.size() || (kDataSymbols + 1) * kSymbol > audio.size() - start) {
        throw runtime_error("Incomplete OFDM burst");
    }
    const auto transmittedPilot = preamble();
    const auto receivedPilot = readSymbol(audio, start);
    array<Complex, kCarriers> channel{};
    for (size_t index = 0; index < kCarriers; ++index) {
        channel[index] = receivedPilot[index] / transmittedPilot[index];
        if (abs(channel[index]) < 1e-8) {
            throw runtime_error("Training signal missing");
        }
    }
    array<unsigned, 40> votes{};
    for (size_t symbolIndex = 0; symbolIndex < kDataSymbols; ++symbolIndex) {
        const auto received = readSymbol(audio, start + (symbolIndex + 1) * kSymbol);
        array<uint8_t, 128> orderBits{};
        for (size_t index = 0; index < kCarriers; ++index) {
            const Complex equalized = received[index] / channel[index];
            orderBits[kPermutations[symbolIndex][index * 2]] = uint8_t(equalized.real() < 0.0);
            orderBits[kPermutations[symbolIndex][index * 2 + 1]] = uint8_t(equalized.imag() < 0.0);
        }
        for (size_t index = 0; index < 120; ++index) {
            votes[index % 40] += orderBits[index];
        }
    }
    DecodedMessage result;
    result.syncSample = chirpStart;
    for (size_t byte = 0; byte < 5; ++byte) {
        for (size_t bit = 0; bit < 8; ++bit) {
            result.packet[byte] = uint8_t((result.packet[byte] << 1) | (votes[byte * 8 + bit] >= 5));
        }
    }
    if (result.packet[0] != 0x47 || result.packet[1] != 0x01) {
        throw runtime_error("Unrecognized frame/version");
    }
    if ((uint16_t(result.packet[3]) << 8 | result.packet[4]) != crc16(result.packet.data(), 3)) {
        throw runtime_error("CRC failed: corrupted message");
    }
    result.command = result.packet[2];
    return result;
}

void writeWav(const string& filename, const vector<double>& audio) {
    if (audio.size() > numeric_limits<uint32_t>::max() / 2 - 36) {
        throw runtime_error("WAV too large");
    }
    ofstream output(filename, ios::binary);
    if (!output) {
        throw runtime_error("Cannot create WAV file: " + filename);
    }
    const uint32_t dataSize = uint32_t(audio.size() * 2);
    output.write("RIFF", 4);
    put32(output, 36 + dataSize);
    output.write("WAVEfmt ", 8);
    put32(output, 16);
    put16(output, 1);
    put16(output, 1);
    put32(output, kSampleRate);
    put32(output, kSampleRate * 2);
    put16(output, 2);
    put16(output, 16);
    output.write("data", 4);
    put32(output, dataSize);
    for (double sample : audio) {
        const double clamped = clamp(sample, -1.0, 1.0);
        const auto signedSample = int16_t(clamped * 32767.0);
        put16(output, uint16_t(signedSample));
    }
    if (!output) {
        throw runtime_error("Failed writing WAV file");
    }
}

vector<double> readWav(const string& filename) {
    ifstream input(filename, ios::binary);
    if (!input) {
        throw runtime_error("Cannot open WAV file: " + filename);
    }
    char identifier[4];
    input.read(identifier, 4);
    (void)get32(input);
    if (string(identifier, 4) != "RIFF") {
        throw runtime_error("Expected RIFF WAV");
    }
    input.read(identifier, 4);
    if (string(identifier, 4) != "WAVE") {
        throw runtime_error("Expected WAV data");
    }
    bool formatFound = false;
    vector<double> audio;
    while (input.read(identifier, 4)) {
        const uint32_t length = get32(input);
        const string chunk(identifier, 4);
        if (chunk == "fmt ") {
            const auto format = get16(input);
            const auto channels = get16(input);
            const auto rate = get32(input);
            (void)get32(input);
            (void)get16(input);
            const auto depth = get16(input);
            if (format != 1 || channels != 1 || rate != kSampleRate || depth != 16 || length < 16) {
                throw runtime_error("Expected 48 kHz, mono 16-bit PCM WAV");
            }
            formatFound = true;
            input.seekg(length - 16 + (length & 1), ios::cur);
        } else if (chunk == "data") {
            if (!formatFound || length % 2 != 0) {
                throw runtime_error("Invalid WAV data chunk");
            }
            audio.reserve(length / 2);
            for (uint32_t index = 0; index < length / 2; ++index) {
                audio.push_back(double(int16_t(get16(input))) / 32768.0);
            }
            break;
        } else {
            input.seekg(length + (length & 1), ios::cur);
        }
        if (!input) {
            throw runtime_error("Malformed WAV chunk");
        }
    }
    if (audio.empty()) {
        throw runtime_error("Empty WAV file");
    }
    return audio;
}

string commandName(uint8_t command) {
    if (command == kCommandFries) {
        return "would you like fries with that?";
    }
    return "unknown command";
}

} // namespace gibber
