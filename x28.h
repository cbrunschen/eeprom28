#ifndef __X28_HPP__
#define __X28_HPP__

#pragma once

#include "eeprom28.h"

#define EEPROM28_DEFINE_X28_DEVICE(n, ab, pb, tb, tw, ...)                                                \
class n##_device : public eeprom28_device<ab, pb, tb, tw __VA_OPT__(,) __VA_ARGS__> {  \
public:                                                                                \
	n##_device(const std::string_view &tag)                                              \
	: eeprom28_device<ab, pb, tb, tw __VA_OPT__(,) __VA_ARGS__>(#n, tag) {}              \
};

EEPROM28_DEFINE_X28_DEVICE(x28c64, 13, 64, 100, 5000)
EEPROM28_DEFINE_X28_DEVICE(x28c256, 15, 64, 100, 500)
EEPROM28_DEFINE_X28_DEVICE(x28hc256, 15, 128, 100, 3000)
EEPROM28_DEFINE_X28_DEVICE(x28c512, 16, 128, 100, 5000)
EEPROM28_DEFINE_X28_DEVICE(x28c010, 17, 256, 100, 5000)
EEPROM28_DEFINE_X28_DEVICE(xm28c020, 18, 128, 100, 5000) // 4 x28c512:s in a single package
EEPROM28_DEFINE_X28_DEVICE(xm28c040, 19, 256, 100, 5000) // 4 x28c010:s in a single package
// a 256 kbit == 32 kbyte "Immediate" EEPROM that uses no timers, requiring the client to
// read() after performing a sequence of writes, at which point the pending writes 
// are immediately committed and ready, and returned without Toggle Bit polling
// or /DATA polling
EEPROM28_DEFINE_X28_DEVICE(x28i256, 15, 64, 0, 0)

// a 256 kbit == 32 kbyte "Fast" EEPROM that uses only the Byte Load Cycle timer and
// also programs immediately on reading; where the Write Cycle is effectively
// infinitely quick and any pending writes are immediately committed and ready, 
// and returned without Toggle Bit polling or /DATA polling
EEPROM28_DEFINE_X28_DEVICE(x28f256, 15, 64, 100, 0, true)

#undef X28_DEF

#endif // __X28_HPP__
