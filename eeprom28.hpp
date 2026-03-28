/***************************************************************************

    28-series Parallel EEPROM sich as Xicor X28, Atmel AT28, etc.
		Caters for different speeds such as X28C256, X28HC256, etc.
		Caters for different storage sizes such as X28C64, X28C256, etc.

***************************************************************************/

#ifndef EEPROM28_HPP
#define EEPROM28_HPP

#pragma once

#include "env.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> eeprom28 EEPROM

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
 */
template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec
>
class eeprom28
{
public:
	// construction/destruction
	eeprom28()
  {
	}

	void write(uint32_t offset, uint8_t data)
	{
		// printf("write(%04x, %02x) state %d\n", offset, data, m_state);
    if (offset >= TOTAL_BYTES)
    {
      return;
    }

		if (m_state == STATE_PROGRAMMING)
		{
			printf("IN PROGRAMMING CYCLE: writing %02x @ %04x\n", data, offset);
			return;
		}

		m_start_programming_timer->adjust(attotime::from_usec(TBLCUsec));

		if (m_state == STATE_PROTECTED_WRITE || m_state == STATE_IDLE)
		{
			// We can start to buffer a set of writes.
			// We do this even if we're write protected.
			m_state = STATE_BUFFERING;

			// At this point we expect to program the buffered data into EEPROM storage -
			// unless of course we're write protected, in which case we already know not to 
			// program the buffer into the EEPROM.
			// If later on we detect a protection command sequence we will set this to 'false' 
			// so that the command sequence (which will end up written to the buffer)
      // does not get written to storage.
			m_program_buffer_to_eeprom = (!m_write_protected) || (m_state == STATE_PROTECTED_WRITE);

			// Note which page we're starting to buffer, copy its current contents into the buffer
			// so the buffer can be written into on a byte by byte basis, before being written back
			// to storage during the programming cycle.
			m_buffering_page = offset & X28C_PAGE_MASK;
			for (uint32_t i; i < PageSizeBytes; i++)
			{
				m_page_buffer[i] = this->m_storage[m_buffering_page + i];
			}
		}

		if (m_state == STATE_BUFFERING || m_state == STATE_PROTECTION_1)
		{
			// The datasheet says that 
			//   "the page address (A6 through A14) for each subsequent
			//   valid write cycle to the part during this operation must be
			//   the same as the initial page address."
			// but does not say anything about the chip verifying this, or the consequences
			// if this is no the case.
			// A valid interpretation is that the chip simply accepts the write into the buffer anyway,
			// at the appropriate offset within the page; 
			// Another valid interpretation is to reject the write.
			// Here, I choose the latter: any writes to an address within a different page
			// are ignored, only those within the same page are accepted.
			if ((offset & X28C_PAGE_MASK) == m_buffering_page)
			{
				m_page_buffer[offset & X28C_OFFSET_MASK] = data;
			}
			// Note where the last write occurred.
			m_last_written_offset = offset;
		}

		// Also detect whether this is the initiation of a protection en-/disable sequence
		if ((m_state == STATE_BUFFERING) && (offset == (0x5555 & ADDRESS_MASK)) && (data == 0xaa))
		{
			m_state = STATE_PROTECTION_1;
			return;
		}

		if (m_state == STATE_PROTECTION_1)
		{
			if ((offset == (0x2aaa & ADDRESS_MASK)) && (data == 0x55))
			{
				// We're firmly within a protection command sequence. 
				// Inhibit the actual writing of data during the subsequence programming cycle.
				m_program_buffer_to_eeprom = false;
				m_state = STATE_PROTECTION_2;
			}
			else
			{
				m_state = m_write_protected ? STATE_IDLE : STATE_BUFFERING;
				return;
			}
		}

		if ((m_state == STATE_PROTECTION_2))
		{
			if ((offset == (0x5555 & ADDRESS_MASK)) && (data == 0xa0))
			{
				m_state = STATE_PROTECTED_WRITE;
				return;
			}
			else if ((offset == (0x5555 & ADDRESS_MASK)) && (data == 0x80) && (m_write_protected))
			{
				m_state = STATE_PROTECTION_DISABLE_3;
				return;
			}
			else
			{
				m_state = m_write_protected ? STATE_IDLE : STATE_BUFFERING;
				return;
			}
		}

		if ((m_state == STATE_PROTECTION_DISABLE_3)) {
			if ((offset == (0x5555 & ADDRESS_MASK)) && (data == 0xaa))
			{
				m_state = STATE_PROTECTION_DISABLE_4;
				return;
			}
			else
			{
				m_state = m_write_protected ? STATE_IDLE : STATE_BUFFERING;
				return;
			}
		}

		if ((m_state == STATE_PROTECTION_DISABLE_4) && (offset == (0x2aaa & ADDRESS_MASK)) && (data == 0x55))
		{
			m_state = STATE_PROTECTION_DISABLE_5;
			return;
		}

		if ((m_state == STATE_PROTECTION_DISABLE_5) && (offset == (0x5555 & ADDRESS_MASK)) && (data == 0x20))
		{
			m_write_protected = false;
			m_state = STATE_IDLE;
			// Write protection was disabled, and the preceding writes were just part of that command sequence.
			// So ensure that none of the writes were actually buffered, by re-reading the page so that when
			// it is re-written, there are no changes.
			for (uint32_t i; i < PageSizeBytes; i++)
			{
				m_page_buffer[i] = m_storage[m_buffering_page + i];
			}
			return;
		} 

		if (m_write_protected)
		{
			// printf("X28C: write %02x to %x while write protected\n", data, offset);
			return;
		}

	}

	uint8_t read(uint32_t offset)
	{
		uint8_t data = m_storage[offset];

		if (m_program_buffer_to_eeprom || (m_state == STATE_PROGRAMMING))
		{
			// "/DATA Polling"
			// While programming or preparing to progam, if the client reads back the 
			// last written byte, the returned bit 7 is the inverse of what was written.
			// But according to at least one datasheet:
			//   "If the X28C64 is in the protected state and an illegal write
			//    operation is attempted /DATA Polling will not operate."
			// so we only perform this if we're actually going to be writing to storage.
			if (m_program_buffer_to_eeprom && (offset == m_last_written_offset))
			{
				data = data ^ INVERSE_DATA_BIT;
			}

			// While programming or preparing to progam, bit 6 of what is returned
			// alternates between 0 and 1.
			data = (data & ~TOGGLE_BIT) | m_toggle_bit;
			m_toggle_bit ^= TOGGLE_BIT;
		}

		return data;
	}

// protected:
	static constexpr int ADDRESS_BITS = AddressBits;
	static constexpr int T_BLC_USEC = TBLCUsec;
	static constexpr int T_WC_USEC = TWCUsec;
	static constexpr int ADDRESS_MASK = (1 << AddressBits) - 1;
	static constexpr int TOTAL_BYTES = 1 << AddressBits;
	static constexpr int NVRAM_BYTES = TOTAL_BYTES + 1; // 1 extra to store write protection status
	static constexpr int INVERSE_DATA_BIT = 1 << 7;
	static constexpr int TOGGLE_BIT = 1 << 6;

	// device-level overrides
	virtual void start()
	{
		m_start_programming_timer = timer_alloc(std::bind(&eeprom28::start_programming_cycle, this));
		m_programming_completed_timer = timer_alloc(std::bind(&eeprom28::programming_cycle_complete, this));
	}

// private:

	static constexpr uint32_t X28C_OFFSET_MASK = PageSizeBytes - 1;
	static constexpr uint32_t X28C_PAGE_MASK = ~(X28C_OFFSET_MASK);

	// internal state
	enum
	{
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

  std::array<uint8_t, TOTAL_BYTES> m_storage;
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
	void start_programming_cycle()
	{
		m_programming_completed_timer->adjust(attotime::from_usec(TWCUsec));
		m_state = STATE_PROGRAMMING;
		
		if (m_program_buffer_to_eeprom)
		{ 
			for (uint32_t i = 0; i < PageSizeBytes; i++)
			{
				m_storage[m_buffering_page + i] = m_page_buffer[i];
			}
		}
	}

	void programming_cycle_complete()
	{
		m_state = STATE_IDLE;
		m_program_buffer_to_eeprom = false;
	}
};

class x28c64 : public eeprom28<13, 64, 100, 5000> {};
class x28c256 : public eeprom28<15, 64, 100, 5000> {};
class x28hc256 : public eeprom28<15, 64, 100, 3000> {};
class x28c512 : public eeprom28<16, 128, 100, 5000> {};
class x28c010 : public eeprom28<17, 128, 100, 5000> {};

#endif // EEPROM28_HPP
