#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gibber {

inline constexpr int kSampleRate = 48000;
inline constexpr std::size_t kFrameSamples = 7424;
inline constexpr std::uint8_t kCommandFries = 0x21;

struct DecodedMessage {
    std::uint8_t command{};
    std::array<std::uint8_t, 5> packet{};
    std::size_t syncSample{};
};

std::array<std::uint8_t, 5> makePacket(std::uint8_t command);
std::vector<double> encode(std::uint8_t command = kCommandFries);
DecodedMessage decode(const std::vector<double>& audio);
void writeWav(const std::string& filename, const std::vector<double>& audio);
std::vector<double> readWav(const std::string& filename);
std::string commandName(std::uint8_t command);

}
