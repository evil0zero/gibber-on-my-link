#include "gibber.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>

using namespace std;

int main() {
    try {
        const auto packet = gibber::makePacket(gibber::kCommandFries);
        const array<uint8_t, 5> expected = {0x47, 0x01, 0x21, 0x53, 0xD3};
        if (packet != expected) {
            throw runtime_error("Packet disagrees with Python fixture");
        }
        const auto waveform = gibber::encode();
        if (waveform.size() != gibber::kFrameSamples) {
            throw runtime_error("Incorrect frame length");
        }
        const auto result = gibber::decode(waveform);
        if (result.command != gibber::kCommandFries || result.packet != expected || result.syncSample != 384) {
            throw runtime_error("C++ clean roundtrip failed");
        }
        cout << "C++ roundtrip: PASS\n";
        return 0;
    } catch (const exception& error) {
        cerr << error.what() << '\n';
        return 1;
    }
}
