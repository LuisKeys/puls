#pragma once
#include <cstdint>

namespace puls {

struct Step {
    bool    active      = false;
    uint8_t velocity    = 100; // 1..127
    uint8_t probability = 100; // 0..100 %
    uint8_t ratchets    = 1;   // 1..8
    int8_t  microtiming = 0;   // -50..+50 % of the step
    uint8_t gate        = 50;  // 1..100 % of the (ratchet) step
};

} // namespace puls
