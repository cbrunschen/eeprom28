#ifndef __AT28_HPP__
#define __AT28_HPP__

#pragma once

#include "eeprom28.h"

#define AT28_PARAMS_WITH_DEFAULTS \
	int AddressBits,                \
	uint32_t PageSizeBytes,         \
	uint32_t TBLCUsec,              \
	uint32_t                        \
	TWCUsec,                        \
	bool ProgramOnRead = false,     \
	uint32_t TCEUsec = 20'000

#define AT28_ARGS \
	AddressBits,    \
	PageSizeBytes,  \
	TBLCUsec,       \
	TWCUsec,        \
	ProgramOnRead,  \
	TCEUsec

#define AT28_SUPER_ARGS \
	AddressBits,          \
	PageSizeBytes,        \
	TBLCUsec,             \
	TWCUsec,              \
	ProgramOnRead,        \
	true,                 \
	true,                 \
	true,                 \
	TCEUsec

#define AT28_PINS                      \
	void set_a9_12v(int state) {         \
		this->set_access_id_page(state);   \
	}                                    \
	void set_oe_12v(int state) {         \
		this->set_chip_erase(state);       \
	}

template<AT28_PARAMS_WITH_DEFAULTS>
class at28_device
: public eeprom28_device<AT28_SUPER_ARGS>
{
	using super = eeprom28_device<AT28_SUPER_ARGS>;
	using self = at28_device<AT28_ARGS>;
public:
	at28_device(const std::string_view &part, const std::string_view &tag) : super(part, tag) {}
	AT28_PINS
};

template<AT28_PARAMS_WITH_DEFAULTS>
class at28_nvram_device
: public eeprom28_nvram_device<AT28_SUPER_ARGS>
{
	using self = at28_nvram_device<AT28_ARGS>;
	using super = eeprom28_nvram_device<AT28_SUPER_ARGS>;
public:
	at28_nvram_device(const std::string_view &part, const std::string_view &tag) : super(part, tag) {}
	AT28_PINS
};

#define DEFINE_AT28_DEVICES(n, ab, pb, tb, tw, ...)                           \
class n##_device                                                              \
: public at28_device<ab, pb, tb, tw __VA_OPT__(,) __VA_ARGS__>                \
{                                                                             \
	using super = at28_device<ab, pb, tb, tw __VA_OPT__(,) __VA_ARGS__>;        \
public:                                                                       \
  n##_device(const std::string_view &tag) : super(#n, tag) {}                 \
};                                                                            \
class n##_nvram_device                                                        \
: public at28_nvram_device<ab, pb, tb, tw __VA_OPT__(,) __VA_ARGS__>          \
{                                                                             \
	using super = at28_nvram_device<ab, pb, tb, tw __VA_OPT__(,) __VA_ARGS__>;  \
public:                                                                       \
  n##_nvram_device(const std::string_view &tag) : super(#n "_nvram", tag) {}  \
};

DEFINE_AT28_DEVICES(at28mini, 4, 4, 150, 10000)
DEFINE_AT28_DEVICES(at28c64b, 13, 64, 150, 10000)
DEFINE_AT28_DEVICES(at28hc64bf, 13, 64, 150, 2000)
DEFINE_AT28_DEVICES(at28c256, 15, 64, 150, 10000)
DEFINE_AT28_DEVICES(at28c256f, 15, 64, 150, 3000)

#undef DEFINE_AT28_DEVICES

#define AT28_AKA(n, en)                      \
using n##_device = en##_device;              \
using n##_nvram_device = en##_nvram_device;

AT28_AKA(at28hc256, at28c256)
AT28_AKA(at28hc256f, at28c256f)

#undef AT28_AKA

#endif // __AT28_HPP__
