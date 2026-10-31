#include "gibber.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        cerr << "Usage: gibber_cpp encode|decode|test [file.wav]\n";
        return 2;
    }
    const string operation = argv[1];
    const string filename = argc == 3 ? argv[2] : "samples/gibber_fries.wav";
    try {
        if (operation == "encode") {
            const auto signal = gibber::encode();
            gibber::writeWav(filename, signal);
            cout << "Encoded " << gibber::commandName(gibber::kCommandFries)
                 << " as " << signal.size() << " samples ("
                 << double(signal.size()) * 1000.0 / gibber::kSampleRate << " ms)\n";
        } else if (operation == "decode" || operation == "test") {
            const auto signal = operation == "test" ? gibber::encode() : gibber::readWav(filename);
            const auto result = gibber::decode(signal);
            cout << "Sync sample: " << result.syncSample << "\nPacket: ";
            for (auto byte : result.packet) {
                cout << hex << setw(2) << setfill('0') << unsigned(byte) << ' ';
            }
            cout << dec << "\nCommand: " << gibber::commandName(result.command) << '\n';
            if (result.command != gibber::kCommandFries) {
                throw runtime_error("Unexpected command");
            }
        } else {
            cerr << "Unknown operation\n";
            return 2;
        }
        return 0;
    } catch (const exception& error) {
        cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
