#pragma once

#include <cstdint>

namespace psxreborn {

// Runtime-only load watch used to reverse-engineer guest data flow
// (PSXREBORN_WATCH=<base hex>:<len hex>), see docs PSXReborn debug notes.
bool memoryWatchEnabled();
void memoryWatchLoad(uint32_t address, const uint32_t* value, uint32_t pc, uint32_t ra);
void memoryWatchStore(uint32_t address, uint32_t value, uint32_t pc);

}  // namespace psxreborn
