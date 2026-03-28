/***************************************************************************

    28-series Parallel EEPROM sich as Xicor X28, Atmel AT28, etc.
		Caters for different speeds such as X28C256, X28HC256, etc.
		Caters for different storage sizes such as X28C64, X28C256, etc.

***************************************************************************/

#ifndef EEPROM28_HPP
#define EEPROM28_HPP

#pragma once

#include "env.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// eeprom28: 28-series EEPROM with pages write and software write protection

/**
 * Template parameters
 * 
 * AddressBits:
 *   The number of bits in the address bus. 
 *   _28_64 EEPROMs store 64 kbits = 8 kbytes = 13 address bits,
 *   _28_256 ones store 256 kbits = 32 kbytes = 15 address bits,
 *   _28_512 EEPROMs store 512 kbits = 64 kbytes = 16 address bits,
 *   _28_010 ones store 1024 kbits = 128 kbytes = 17 address bits.
 * 
 * PageSizeBytes:
 *   These EEPROMs support writing an entire page at a time. These page sizes vary
 *   by EEPROM size and by manufacturer. 64 and 128 byte page sizes are both common.
 * 
 * TBLCUsec:
 *   The EEPROM's T_BLC, the "Byte Load Cycle Time", in microseconds.
 *   After a byte write, if another byte on the same page is written within T_BLC, 
 *   those writes will be combined in a single programming cycle. Or in other words,
 *   the programming cycle starts T_BLC after the most recent write.
 *   Xicor X28C256 has T_BLC = 100 mucroseconds; AT28C256 has T_BLC = 150 microseconds.
 * 
 * TWCUsec:
 *   The EEPROM's T_WC, the "Write Cycle Time", in microseconds. This is the amount
 *   of time it takes for the EEPROM to complete is programming cycle.
 *   Xicor X28C256 has T_WC typ = 5ms = 5000 microseconds, max = 10ms = 10000 microseconds.
 *   Xicor X28HC256 has T_WC typ = 3ms = 3000 microseconds, max = 5ms = 5000 microseconds.
 * 
 * ProgramOnRead:
 *   Instead of waiting for the TBLCUsec interval to elapse, whenever a Read hapens,
 *   immediately start any pending programming cycle. 
 *   This is not present on real devices but here it allows us to create a 'fast' eeprom.
 *   In particular, if we know that the client always follows writes with reads to verify
 *   that the programming cycle completes, then we can set TBLCUsec to 0 and 
 *   ProgramOnRead to true. We now have a device that obeys the EEPROM write
 *   protection commands but does not have to wait for T_BLC to expire before writing the
 *   buffered data to storage.
 *   If we then also set TWCUsec to 0, we have a device that also immediately writes the
 *   buffered data to storage, giving us a very fast EEPROM device. 
 *   This can be used for example for external EEPROM cartridges, where emulating the precise
 *   timing of a particular chip is unlikely to be very important, and instead, allowing
 *   that cartridge to be as quick as possible may give a more pleasant user experience.
 */
template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead = false
>
class eeprom28 {
public:
	// construction/destruction
	eeprom28() { }

	void write(uint32_t offset, uint8_t data);
	uint8_t read(uint32_t offset);

// protected:
	static constexpr uint32_t ADDRESS_BITS = AddressBits;
	static constexpr uint32_t TOTAL_SIZE_BYTES = 1 << AddressBits;
	static constexpr uint32_t ADDRESS_MASK = TOTAL_SIZE_BYTES - 1;
	static constexpr uint32_t PAGE_SIZE_BYTES = PageSizeBytes;
	static constexpr uint32_t T_BLC_USEC = TBLCUsec;
	static constexpr uint32_t T_WC_USEC = TWCUsec;

	static constexpr uint32_t PAGE_OFFSET_MASK = PageSizeBytes - 1;
	static constexpr uint32_t PAGE_MASK = ~(PAGE_OFFSET_MASK);

	static constexpr uint8_t INVERSE_DATA_BIT = 1 << 7;
	static constexpr uint8_t TOGGLE_BIT = 1 << 6;

	// device-level overrides
	virtual void start() {
		if (TBLCUsec > 0) {
			m_start_programming_timer = timer_alloc(std::bind(&eeprom28::start_programming_cycle, this));
		}

		if (TWCUsec > 0) {
			m_programming_completed_timer = timer_alloc(std::bind(&eeprom28::programming_cycle_complete, this));
		}
	}

// private:

	// Change State to a new internal state
	void change_to_state(int ns);

	// Error in the internal state machine, return to the correct idle internal state
	void state_machine_error();

	// internal state
	enum {
		// idle state: reads work as normal, writes will succeed or fail depending on
		// m_write_protected - except for those writes that are part of one of the protection
		// enable or disable sequences.
		STATE_IDLE,  

		// after detecting the first write that initiates a protection en-/disable sequence,
		// writing AA to address 5555 (1555 on X28C64)
		STATE_PROTECTION_1,
		// after detecting the second write  that initiates a protection en-/disable sequence,
		// writing 55 to address 2AAA (0AAA on X28C64)
		STATE_PROTECTION_2,


		// after detecting the third write that initiates a protection disable sequence,
		// writing 80 to address 5555 (1555 on X28C64)
		STATE_PROTECTION_DISABLE_3,
		// after detecting the fourth write that initiates a protection disable sequence,
		// writing AA to address 5555 (1555 on X28C64)
		STATE_PROTECTION_DISABLE_4,
		// after detecting the fifth write that initiates a protection disable sequence.
		// writing 55 to address 2AAA (0AAA on X28C64)
		STATE_PROTECTION_DISABLE_5,
		// after detecting the sixth write in the sequence, 
		// writing 20 to address 5555 (1555 on X28C64),
		// the device will return to STATE_IDLE with m_write_protected = false.

	
		// After detecting the third write that initiates a protection enable sequence,
		// writing A0 to address 5555 (1555 on X28C64).
		// At this point the device will accept one page write, before then going into
		// or remaining in write-protected mode.
		STATE_PROTECTED_WRITE,

		// When a write operation starts, this selects a 64-byte page that is being written,
		// and writes the byte to this buffer.
		// As long as the next write is to the same page (A6-A14 remain the same) and the next
		// write is initiated within 100 microseconds, more bytes can be written, and those
		// too will be buffered. 
		// If no more writes happen within 100 microseconds, thw programming cycle starts,
		// during which time the buffer will be saved to the corresponding page in the EEPROM.
		STATE_BUFFERING,

		// Buffered data is being programmed into the EEPROM.
		STATE_PROGRAMMING,
	};

  std::array<uint8_t, TOTAL_SIZE_BYTES> m_storage;
  bool m_program_buffer_to_eeprom;
	Timer *m_start_programming_timer;
	Timer *m_programming_completed_timer;
	uint32_t m_last_written_offset;
	uint8_t m_toggle_bit = 0;
	int m_state = STATE_IDLE;
	bool m_write_protected = false;
	int m_buffering_page = 0;
	std::array<uint8_t, PageSizeBytes> m_page_buffer;

	// Timer callbacks
	void start_programming_cycle();
	void programming_cycle_complete();
};

class x28c64 : public eeprom28<13, 64, 100, 5000> {};
class x28c256 : public eeprom28<15, 64, 100, 5000> {};
class x28hc256 : public eeprom28<15, 64, 100, 3000> {};
class x28c512 : public eeprom28<16, 128, 100, 5000> {};
class x28c010 : public eeprom28<17, 128, 100, 5000> {};

// WIP:
// a 256 kbit == 32 kbyte "fast" that uses no timers, relying on the cliend to
// read() after performing a sequence of writes, at which point the pending writes 
// are immediately committed and ready.
class f28c256 : public eeprom28<15, 64, 0, 0, true> {};

#include "eeprom28.ipp"

#endif // EEPROM28_HPP
