#pragma once

#include <types.h>

namespace pranaOSBitreader {
    class BitReader {
    public:
        BitReader(uint8_t* data) {
            this->dataPtr = data;
            this->pos = 0;
            this->byte = 0;
            this->numBits = 0;
        }

        uint8_t ReadByte() {
            this->numBits = 0;
            uint8_t b = this->dataPtr[this->pos];
            this->pos += 1;
            return b;
        }
    };
}