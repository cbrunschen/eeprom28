/***************************************************************************

	28-series Parallel EEPROM read and write logic, including write protection
	command sequences, such as Xicor X28, Atmel AT28, etc.
	Caters for different speeds such as X28C256, X28HC256, etc.
	Caters for different storage sizes such as X28C64, X28C256, X28C010,
	XM28C020, etc.

***************************************************************************/

#ifndef EEPROM28_HPP
#define EEPROM28_HPP

#pragma once

#include "env.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// eeprom28_device: 28-series EEPROM with paged write and software write protection

#define EEPROM28_PARAMS int AddressBits, uint32_t PageSizeBytes, uint32_t TBLCUsec, uint32_t TWCUsec, bool ProgramOnRead, bool HasIdPage, bool HasHardwareChipErase, bool HasSoftwareChipErase, uint32_t TCEUsec
#define EEPROM28_ARGS AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead, HasIdPage, HasHardwareChipErase, HasSoftwareChipErase, TCEUsec

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
 *   *Must* be a power of two.
 * 
 * TBLCUsec:
 *   The EEPROM's T_BLC, the "Byte Load Cycle Time", in microseconds.
 *   After a byte write, if another byte on the same page is written within T_BLC, 
 *   those writes will be combined in a single programming cycle. Or in other words,
 *   the programming cycle starts T_BLC after the most recent write.
 *   Xicor X28C256 has T_BLC = 100 microseconds; AT28C256 has T_BLC = 150 microseconds.
 *   If TBLCUsec == 0, then there is no timed end to the Byte Load Cycle, so we
 *   _must_ trigger programming in some other way. The only way to do that is to triggger
 *   programming on read(), so that is what we do. See also ProgramOnRead below, for
 *   explicitly enabling this behaviour.
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
 *   that the programming cycle completes, then we can set TBLCUsec to 0 and thus implicitly
 *   ProgramOnRead to true. We now have a device that obeys the EEPROM write
 *   protection commands but does not have to wait for T_BLC to expire before writing the
 *   buffered data to storage.
 *   If we then also set TWCUsec to 0, we have a device that also immediately writes the
 *   buffered data to storage, giving us a very fast EEPROM device. 
 *   This can be used for example for external EEPROM cartridges, where emulating the precise
 *   timing of a particular chip is unlikely to be very important, and instead, allowing
 *   that cartridge to be as quick as possible may give a more pleasant user experience.
 * 
 * HasIdPage:
 *   In addition to the data, has an extra page if "identification" data, which is accessed by
 *   calling `set_access_id_page(1)`, which allows the ID page to be accessed at the top of
 *   the chip's address range.
 *   On Atmel AT28 EEPROMs, this is achieved by driving pin A9 to +12V.
 * 
 * HasHardwareChipErase:
 *   While `chip_erase` is asserted (by `set_chip_erase(1)`), a write to any address will trigger
 *   erasing all data on the chip. This will set all data, including any ID data, to 0xff.
 *   This will take TCEUsec microseconds (20ms == 20000us by default).
 *   On Atmel AT28 series chipe, this is achieved by driving pin /OE (Output enable)
 *   to +12V.
 * 
 * HasSoftwareChipErase:
 *   In addition to the usual software write protection commands, adds a further command.
 *   If, at the end of the "protection disable" command sequence, instead of writing a final
 *   0x20 (to disable write protection) the host writes 0x10, then a software chip erase
 *   is initiated. This will set all data (including any ID data) to 0xff. This will take
 *   TCEUsec microseconds (20ms == 20000us by default).
 * 
 * TCEUsec:
 *   The time it takes for a Chip Erase, either hardware or software, to complete.
 *   20ms == 20000us by default. If set to 0, chip erase will take effect immediately.
 */
template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead = false,
	bool HasIdPage = false,
	bool HasHardwareChipErase = false,
	bool HasSoftwareChipErase = false,
	uint32_t TCEUsec = 20'000   // 20 ms chip erase time by default
>
class eeprom28_device 
: public device_t 
{
	using self = eeprom28_device<EEPROM28_ARGS>;

public:
	// construction/destruction
	eeprom28_device(const std::string_view &part, const std::string_view &tag) : device_t(tag), m_part(part) { }
	virtual ~eeprom28_device() { }

	const std::string &part() { return m_part; }

	void write(uint32_t offset, uint8_t data);
	uint8_t read(uint32_t offset);

	// Allow users to override device timing.
	void override_t_blc_usec(uint32_t t_blc_usec) {
		m_t_blc_usec = t_blc_usec;
	}

	void override_t_wc_usec(uint32_t t_wc_usec) {
		m_t_wc_usec = t_wc_usec;
	}

	void override_program_on_read(bool program_on_read) {
		m_program_on_read = program_on_read;
	}

	void override_t_ce_usec(uint32_t t_ce_usec) requires (HasHardwareChipErase || HasSoftwareChipErase)
	{
		m_t_ce_usec = t_ce_usec;
	}

	void set_chip_erase(int state) requires HasHardwareChipErase
	{
		m_chip_erase = (state != 0);
	}

	void set_access_id_page(int state) requires HasIdPage
	{
		m_access_id_page = (state != 0);
	}

#ifndef EEPROM28_VISIBLE_FOR_TESTING
protected:
#endif // EEPROM28_VISIBLE_FOR_TESTING

	static constexpr bool HAS_ID_PAGE = HasIdPage;
	static constexpr bool HAS_HARDWARE_CHIP_ERASE = HasHardwareChipErase;
	static constexpr bool HAS_SOFTWARE_CHIP_ERASE = HasSoftwareChipErase;
	static constexpr bool HAS_CHIP_ERASE = HAS_HARDWARE_CHIP_ERASE || HAS_SOFTWARE_CHIP_ERASE;

	static constexpr uint32_t ADDRESS_BITS = AddressBits;
	static constexpr uint32_t DATA_SIZE_BYTES = 1 << AddressBits;
	static constexpr uint32_t ADDRESS_MASK = DATA_SIZE_BYTES - 1;
	static constexpr uint32_t PAGE_SIZE_BYTES = PageSizeBytes;

	static constexpr uint32_t PAGE_OFFSET_MASK = PageSizeBytes - 1;
	static constexpr uint32_t PAGE_MASK = ~(PAGE_OFFSET_MASK);

	static constexpr uint32_t ID_PAGE_SIZE_BYTES = HasIdPage ? PageSizeBytes : 0;
	static constexpr uint32_t ID_PAGE_OFFSET = DATA_SIZE_BYTES - ID_PAGE_SIZE_BYTES;
	static constexpr uint32_t ID_PAGE = PAGE_MASK + 1;

	static constexpr uint32_t TOTAL_SIZE_BYTES = DATA_SIZE_BYTES + ID_PAGE_SIZE_BYTES;

	static constexpr uint32_t T_BLC_USEC = TBLCUsec;
	static constexpr uint32_t T_WC_USEC = TWCUsec;
	static constexpr bool PROGRAM_ON_READ = (TBLCUsec == 0) || ProgramOnRead;
	static constexpr uint32_t T_CE_USEC = TCEUsec;

	static constexpr uint8_t INVERSE_DATA_BIT = 1 << 7;
	static constexpr uint8_t TOGGLE_BIT = 1 << 6;

	// device-level overrides
	virtual void device_start() {
		if (m_t_blc_usec > 0 && m_start_programming_timer == nullptr) {
			m_start_programming_timer = timer_alloc(std::bind(&self::start_programming_cycle, this));
		}

		if ((m_t_wc_usec > 0  || (HAS_CHIP_ERASE && (m_t_ce_usec > 0))) && m_programming_completed_timer == nullptr) {
			m_programming_completed_timer = timer_alloc(std::bind(&self::programming_cycle_complete, this));
		}
	}

	virtual void device_reset() {
		if (m_start_programming_timer != nullptr)
			m_start_programming_timer->enable(false);

		if (m_programming_completed_timer != nullptr)
			m_programming_completed_timer->enable(false);

		change_to_state(COMMAND_STATE_NONE);
		change_to_state(STATE_IDLE);

		m_last_written_offset = -1;
		m_software_data_protection_enabled = false;
		m_buffering_page = 0;
	}


#ifndef EEPROM28_VISIBLE_FOR_TESTING
protected:
#endif // EEPROM28_VISIBLE_FOR_TESTING

	// Change State to a new internal state
	void change_to_state(int ns) {
			m_state = ns;
	}

	// Error in the internal state machine, return to the correct idle internal state
	void state_machine_error() {
		fprintf(stdout, "%s: state machine error: changing from state %d", m_part.c_str(), m_state);
		change_to_state(m_software_data_protection_enabled ? STATE_IDLE : STATE_BUFFERING);
		fprintf(stdout, "% to state %d", m_state);
	}

	// Change State to a new command processing state
	void change_to_command_state(int ns) {
		m_command_state = ns;
	}

	// Error in the command state machine, return to the correct idle internal state
	void command_state_machine_error() {
		change_to_command_state(COMMAND_STATE_NONE);
	}

	// internal state
	enum {
		// idle state: reads work as normal, writes will succeed or fail depending on
		// m_software_data_protection_enabled - except for those writes that are part of one of the protection
		// enable or disable sequences.
		STATE_IDLE,  
	
		// After detecting the third write that initiates a protection enable sequence,
		// writing A0 to address 5555 (1555 on X28C64).
		// At this point the device will accept one page write, before then going into
		// or remaining in write-protected mode.
		STATE_PROTECTED_WRITE,

		// When a write operation starts, this selects a page that is being written,
		// and writes the byte to an internal buffer.
		// As long as the next write is to the same page (higher address bits remain
		// the same) and the next write is initiated within T_BLC, more bytes can be
		// written, and those too will be written to the internal buffer. 
		// If no more writes happen within T_BLC, the programming cycle starts,
		// during which time the buffer will be saved to the corresponding page in
		// the persistent EEPROM storage.
		STATE_BUFFERING,

		// Buffered data is being programmed into the EEPROM.
		STATE_PROGRAMMING,
	};

	// Command processing state
	enum {
		// Not in any command sequence.
		COMMAND_STATE_NONE = 0,

		// after detecting the first write that initiates a command sequence,
		// writing AA to address 5555 (1555 on X28C64)
		COMMAND_STATE_1,

		// after detecting the second write  that initiates a command sequence,
		// writing 55 to address 2AAA (0AAA on X28C64)
		COMMAND_STATE_2,
		// If in this state the client writes A0 to 5555 (1555 on X28C64),
		// that concludes the Protection Enable command sequence and enters
		// the Protected Write state: allowing writes to one page, at the end of which
		// the device will enter the Write Protected state, setting
		// m_write_protecion_enabled = true.
		// If instead the client writes  80 to 5555 (1555 on X28C64), this continues
		// the Protection Disable command sequence.


		// after detecting the third write that initiates a protection disable command sequence,
		// writing 80 to address 5555 (1555 on X28C64)
		COMMAND_STATE_PROTECION_DISABLE_3,

		// after detecting the fourth write that initiates a protection disable command sequence,
		// writing AA to address 5555 (1555 on X28C64)
		COMMAND_STATE_PROTECION_DISABLE_4,

		// after detecting the fifth write that initiates a protection disable command sequence.
		// writing 55 to address 2AAA (0AAA on X28C64)
		COMMAND_STATE_PROTECION_DISABLE_5,

		// after detecting the sixth write in the protection disable command sequence:
		// - writing 20 to address 5555 (1555 on X28C64),
		//   the device will return to COMMAND_STATE_NONE and STATE_IDLE with
		//   m_software_data_protection_enabled = false.
		// If HasSoftwareChipErase == true, then:
		// - writing 10 to address 5555 (1555 on AT28C64x),
		//   the device will perform a chip erase, filling its storage with FF. This will take TCEUsec.
		//   While this is ongoing, Toggle Bit Polling will work, but as there was no specific most
		//   recent address writte, /DATA Polling will not.
	};

	std::string m_part;
	std::array<uint8_t, TOTAL_SIZE_BYTES> m_storage;
	bool m_program_buffer_to_eeprom = false;
	emu_timer *m_start_programming_timer = nullptr;
	emu_timer *m_programming_completed_timer = nullptr;
	int32_t m_last_written_offset = -1;
	uint8_t m_toggle_bit = 0;
	int m_state = STATE_IDLE;
	int m_command_state = COMMAND_STATE_NONE;
	bool m_software_data_protection_enabled = false;
	int m_buffering_page = 0;
	std::array<uint8_t, PageSizeBytes> m_page_buffer;

		// Configurable overrides: initialize per the device's definition.
	uint32_t m_t_blc_usec = T_BLC_USEC;
	uint32_t m_t_wc_usec = T_WC_USEC;
	bool m_program_on_read = PROGRAM_ON_READ;
	uint32_t m_t_ce_usec = T_CE_USEC;

	bool m_chip_erase = false;
	bool m_access_id_page = false;

	// Timer callbacks
	void start_programming_cycle() {
		change_to_state(STATE_PROGRAMMING);
		
		if (m_program_buffer_to_eeprom) {
			std::copy(std::begin(m_page_buffer), std::end(m_page_buffer), &(m_storage[storage_page(m_buffering_page)]));
		}

		if (m_t_wc_usec > 0) {
			m_programming_completed_timer->adjust(attotime::from_usec(m_t_wc_usec));
		} else {
			programming_cycle_complete();
		}
	}

	void programming_cycle_complete() {
		change_to_state(STATE_IDLE);

		m_program_buffer_to_eeprom = false;
		m_last_written_offset = -1;
	}

	// Helper
	void disable_software_data_protection() {
		// We have now received a complete "disable write protection" command. So we:
		// - Disable write protection, i.e., enable writes.
		m_software_data_protection_enabled = false;
		// - Note that we're no longer in a command sequence.
		change_to_command_state(COMMAND_STATE_NONE);
		// - Write protection was disabled, and the preceding writes were just part of that command sequence.
		m_program_buffer_to_eeprom = false;
		// printf("m_program_buffer_to_eeprom -> %d\r\n", m_program_buffer_to_eeprom);

		if (m_t_blc_usec > 0) {
			// Since we're explicitly starting the programming cycle, disable the timer
			m_start_programming_timer->enable(false);
		}
		
		// - Start the programming cycle
		start_programming_cycle();
	}

	// Chip Erase
	void start_erase_cycle() requires (HasHardwareChipErase || HasSoftwareChipErase) {
		if (HAS_CHIP_ERASE) {
			change_to_state(STATE_PROGRAMMING);
			
			// Erase all the data by setting it to 0xff
			std::fill_n(&m_storage[0], TOTAL_SIZE_BYTES, 0xff);

			if (m_t_ce_usec > 0) {
				m_programming_completed_timer->adjust(attotime::from_usec(TCEUsec));
			} else {
				programming_cycle_complete();
			}
		}
	}

	// are we accessing the ID page?
	bool is_accessing_id_page(uint32_t offset) requires HasIdPage {
		if (HAS_ID_PAGE) {
			return m_access_id_page && (offset & PAGE_MASK) == ID_PAGE_OFFSET;
		} else {
			return false;
		}
	}

	uint32_t storage_offset(uint32_t offset) requires HasIdPage {
		return is_accessing_id_page(offset) ? (offset - ID_PAGE_OFFSET + DATA_SIZE_BYTES) : offset;
	}

	uint32_t storage_offset(uint32_t offset) requires (!HasIdPage) {
		return offset;
	}

	uint32_t storage_page(uint32_t offset) {
		return storage_offset(offset) & PAGE_MASK;
	}
};

template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead = false,
	bool HasIdPage = false,
	bool HasHardwareChipErase = false,
	bool HasSoftwareChipErase = false,
	uint32_t TCEUsec = 20'000   // 20 ms chip erase time by default
>
class eeprom28_nvram_device
: public eeprom28_device<EEPROM28_ARGS>
, public device_nvram_interface
{
	using self = eeprom28_nvram_device<EEPROM28_ARGS>;
	using super = eeprom28_device<EEPROM28_ARGS>;

public:
	eeprom28_nvram_device(const std::string_view &part, const std::string_view &tag) : super(part, tag) {}

	// derived class overrides
	void nvram_default() override
	{
		std::memset(&self::m_storage[0], 0xff, self::TOTAL_SIZE_BYTES);
	}

	bool nvram_read(std::basic_istream<uint8_t> &file) override
	{
		auto constexpr N = self::TOTAL_SIZE_BYTES + 1;
		std::array<uint8_t, N> buffer;
		size_t offset = 0;
		size_t nread = 0;

		assert(!file.badbit);

		while (offset < N && (nread = file.readsome(&buffer[offset], N - offset)) > 0) {
			offset += nread;
		}

		if (offset < N) {
			// std::cout << this->part() << " read only " << offset << " out of " << N << " expected bytes" << std::endl;
			return false;
		} else if (file.peek() != std::char_traits<uint8_t>::eof()) {
			// std::cout << this->part() << " read all " << offset << " expected bytes, but not at end of input" << std::endl;
			return false;
		}

		std::memcpy(&self::m_storage[0], &buffer[0], self::TOTAL_SIZE_BYTES);
		self::m_software_data_protection_enabled = buffer[self::TOTAL_SIZE_BYTES] != 0;
		return true;
	}

	bool nvram_write(std::basic_ostream<uint8_t> &file) override
	{
		std::array<uint8_t, self::TOTAL_SIZE_BYTES + 1> buffer;
		std::memcpy(&buffer[0], &self::m_storage[0], self::TOTAL_SIZE_BYTES);
		buffer[self::TOTAL_SIZE_BYTES] = self::m_software_data_protection_enabled ? 1 : 0;
		file.write(&buffer[0], self::TOTAL_SIZE_BYTES + 1);
		return true;
	}
};

#include "eeprom28.ipp"

#undef EEPROM28_PARAMS
#undef EEPROM28_ARGS

#endif // EEPROM28_HPP
