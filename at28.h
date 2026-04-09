#ifndef __AT28_HPP__
#define __AT28_HPP__

#pragma once

#include "eeprom28.h"

template <typename B>
class at28_device_pins {
public:
	void set_a9_12v(int state) {
		B::set_access_id_page(state);
	}

	void set_oe_12v(int state) {
		B::set_chip_erase(state);
	}
};


#undef AT28_SUPER_PARAMS
#undef AT28_PARAMS
#define AT28_SUPER_PARAMS AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead, true, true, true, TCEUsec
#define AT28_PARAMS AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead, TCEUsec

template<
	int AddressBits,
	uint32_t PageSizeBytes,
	uint32_t TBLCUsec,
	uint32_t TWCUsec,
	bool ProgramOnRead = false,
	uint32_t TCEUsec = 20'000   // 20 ms chip erase time by default
>
class at28_device
: public eeprom28_device<AT28_SUPER_PARAMS>
, public at28_device_pins<at28_device<AT28_PARAMS>>
{
	using super = eeprom28_device<AT28_SUPER_PARAMS>;
	using self = at28_device<AT28_PARAMS>;

public:
	at28_device(const std::string_view &part, const std::string_view &tag) : super(part, tag) {}
};

#undef AT28_SUPER_PARAMS
#undef AT28_PARAMS


#define EEPROM28_DEFINE_AT28_DEVICE(n, ab, pb, tb, tw)                   \
class n##_device : public at28_device<ab, pb, tb, tw>                    \
{                                                                        \
	using super = at28_device<ab, pb, tb, tw>;                             \
public:                                                                  \
  n##_device(const std::string_view &tag) : super(#n, tag) {}            \
};

EEPROM28_DEFINE_AT28_DEVICE(at28c64b, 13, 64, 150, 10000)
EEPROM28_DEFINE_AT28_DEVICE(at28hc64bf, 13, 64, 150, 2000)
EEPROM28_DEFINE_AT28_DEVICE(at28c256, 15, 64, 150, 10000)
EEPROM28_DEFINE_AT28_DEVICE(at28c256f, 15, 64, 150, 3000)

#undef EEPROM28_DEFINE_AT28_DEVICE

template<
	int AddressBits,
	uint32_t PageSizeBytes,
	uint32_t TBLCUsec,
	uint32_t TWCUsec,
	bool ProgramOnRead = false,
	uint32_t TCEUsec = 20'000   // 20 ms chip erase time by default
>
class at28_nvram_device
: public eeprom28_nvram_device<
		AddressBits,
		PageSizeBytes,
		TBLCUsec,
		TWCUsec,
		ProgramOnRead,
		true, // HasIdPage
		true, // HasHardwareChipErase
		true, // HasSoftwareChipErase
		TCEUsec
	>
, public at28_device_pins<
		at28_nvram_device<
			AddressBits,
			PageSizeBytes,
			TBLCUsec,
			TWCUsec,
			ProgramOnRead,
			TCEUsec
		>
	>
{
	using super = eeprom28_nvram_device<
		AddressBits,
		PageSizeBytes,
		TBLCUsec,
		TWCUsec,
		ProgramOnRead,
		true, // HasIdPage
		true, // HasHardwareChipErase
		true, // HasSoftwareChipErase
		TCEUsec
	>;

public:
	at28_nvram_device(const std::string_view &part, const std::string_view &tag) : super(part, tag) {}
};

#define DEFINE_AT28_NVRAM_DEVICE(n, ab, pb, tb, tw)                  \
class n##_device : public at28_nvram_device<ab, pb, tb, tw>          \
{                                                                    \
	using super = at28_nvram_device<ab, pb, tb, tw>;                   \
public:                                                              \
  n##_device(const std::string_view &tag) : super(#n, tag) {}        \
};

DEFINE_AT28_NVRAM_DEVICE(at28c64b_nvram, 13, 64, 150, 10000)

#endif // __AT28_HPP__
