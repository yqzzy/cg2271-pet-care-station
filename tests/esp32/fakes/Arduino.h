#ifndef PET_TEST_ARDUINO_H
#define PET_TEST_ARDUINO_H

#include "fake_runtime.h"
#include <string>

static const int SERIAL_8N1 = 0;

inline uint32_t millis() { return fake::now; }

class HardwareSerial {
public:
    std::string input;
    std::string output;
    void setRxBufferSize(size_t) {}
    void begin(unsigned, int, int, int) {}
    int available() const { return static_cast<int>(input.size()); }
    int read() {
        if (input.empty()) return -1;
        int value = static_cast<unsigned char>(input[0]);
        input.erase(0, 1);
        return value;
    }
    size_t write(const uint8_t *data, size_t size) {
        if (fake::mutexDepth) ++fake::ioWhileLocked;
        output.append(reinterpret_cast<const char *>(data), size);
        return size;
    }
};

class DebugSerial {
public:
    void printf(const char *, ...) { if (fake::mutexDepth) ++fake::ioWhileLocked; }
    void println(const char *) { if (fake::mutexDepth) ++fake::ioWhileLocked; }
};
extern DebugSerial Serial;
extern HardwareSerial Serial1;

#endif
