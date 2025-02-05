#pragma once
#include <cstdint>

namespace CRC {
    class CRC_32 {
    public:
        static uint32_t Table[256];
        
        static void InitializeTable() {
            static bool initialized = false;
            if (!initialized) {
                for (uint32_t i = 0; i < 256; i++) {
                    uint32_t c = i;
                    for (int j = 0; j < 8; j++) {
                        c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
                    }
                    Table[i] = c;
                }
                initialized = true;
            }
        }
        
        static uint32_t Calculate(const void* data, size_t length) {
            InitializeTable();
            uint32_t crc = 0xFFFFFFFF;
            const unsigned char* buffer = static_cast<const unsigned char*>(data);
            
            // Process one byte at a time
            for (size_t i = 0; i < length; i++) {
                crc = (crc >> 8) ^ Table[(crc & 0xFF) ^ buffer[i]];
            }
            
            return ~crc;
        }
    };

    inline uint32_t Calculate(const void* data, size_t length, CRC_32) {
        return CRC_32::Calculate(data, length);
    }
    
    uint32_t CRC_32::Table[256];
} 