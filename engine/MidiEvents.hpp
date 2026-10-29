#pragma once
#include <cstdint>

namespace puls {

// A fully formed 3-byte channel message at a sample offset inside the current block.
struct MidiMsg {
    uint32_t frame;
    uint8_t  data[3];
    bool isNoteOn()  const noexcept { return (data[0] & 0xF0) == 0x90; }
    bool isNoteOff() const noexcept { return (data[0] & 0xF0) == 0x80; }
};

} // namespace puls
