/***************************************************************************

    28-series Parallel EEPROM sich as Xicor X28, Atmel AT28, etc.
		Caters for different speeds such as X28C256, X28HC256, etc.
		Caters for different storage sizes such as X28C64, X28C256, etc.

***************************************************************************/

#ifndef EEPROM28_IPP
#define EEPROM28_IPP

#include "eeprom28.hpp"

#pragma once

template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead
>
inline void eeprom28<AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead>::change_to_state(int ns) {
  // printf("Changing state to %d\r\n", ns);
  m_state = ns;
}

template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead
>
void eeprom28<AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead>::state_machine_error() {
  change_to_state(m_write_protected ? STATE_IDLE : STATE_BUFFERING);
}

template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead
>
void eeprom28<AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead>::write(uint32_t offset, uint8_t data) {
  // Note whether we are starting from the idle state.
  bool was_idle = m_state == STATE_IDLE;

  // printf("write(%04x, %02x) in state %d\n", offset, data, m_state);
  if (offset >= TOTAL_SIZE_BYTES) {
    // Attempting to write outside the range of this device does nothing.
    return;
  }

  if (m_state == STATE_PROGRAMMING) {
    // An attempt to write during a programming cycle does nothing.
    printf("IN PROGRAMMING CYCLE: writing %02x @ %04x\n", data, offset);
    return;
  }

  if (TBLCUsec > 0) {
    // Adjust the time remaining for more writes to the same page.
    m_start_programming_timer->adjust(attotime::from_usec(TBLCUsec));
  }

  if (m_state == STATE_IDLE || m_state == STATE_PROTECTED_WRITE) {
    // This is the first write that we will buffer.
    // At this point, the beginning of buffering data, we expect to program the data
    // we are about to buffer into EEPROM storage - unless of course we're write protected,
    // in which case we already know _not_ to program the buffer into the EEPROM.
    // If later on we detect a protection command sequence we will set this to 'false' 
    // so that the command sequence (which will end up in the buffer)
    // does not get written to storage.
    m_program_buffer_to_eeprom = (!m_write_protected) || (m_state == STATE_PROTECTED_WRITE);
    // printf("m_program_buffer_to_eeprom -> %d\r\n", m_program_buffer_to_eeprom);

    // We start to buffer a set of writes.
    // We do this even if we're write protected - m_program_buffer_to_eeprom protects us.
    change_to_state(STATE_BUFFERING);

    // We note which page we're starting to buffer, copy its current contents into the buffer
    // so the buffer can be written into on a byte by byte basis, before being written back
    // to storage during the programming cycle.
    m_buffering_page = offset & PAGE_MASK;
    const uint8_t *p = &(m_storage[m_buffering_page]);
    std::copy(p, p + PageSizeBytes, std::begin(m_page_buffer));
  }

  if (m_state == STATE_BUFFERING || m_state == STATE_PROTECTION_1) {
    // The datasheet for the X28C256 says that 
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
    if ((offset & PAGE_MASK) == m_buffering_page) {
      m_page_buffer[offset & PAGE_OFFSET_MASK] = data;
    }

    // Note where the last write occurred.
    m_last_written_offset = offset;
  }

  // Detect if this is the initiation of a protection {en|dis}able sequence
  if ((was_idle) && (offset == (0x5555 & ADDRESS_MASK)) && (data == 0xaa)){ 
    return change_to_state(STATE_PROTECTION_1);
  }

  // Detect if this is the second write in a protection {en|dis}able sequence
  if (m_state == STATE_PROTECTION_1) {
    if ((offset == (0x2aaa & ADDRESS_MASK)) && (data == 0x55)) {
      // We're firmly within a protection command sequence. 
      // Inhibit the actual writing of data during the subsequence programming cycle.
      m_program_buffer_to_eeprom = false;
      // printf("m_program_buffer_to_eeprom -> %d\r\n", m_program_buffer_to_eeprom);
      return change_to_state(STATE_PROTECTION_2);
    } else {
      return state_machine_error();
    }
  }

  if (m_state == STATE_PROTECTION_2) {
    if ((offset == (0x5555 & ADDRESS_MASK)) && (data == 0xa0)) {
      m_write_protected = true;
      return change_to_state(STATE_PROTECTED_WRITE);
    } else if ((offset == (0x5555 & ADDRESS_MASK)) && (data == 0x80) && (m_write_protected)) {
      return change_to_state(STATE_PROTECTION_DISABLE_3);
    } else  {
      return state_machine_error();
    }
  }

  if (m_state == STATE_PROTECTION_DISABLE_3) {
    if ((offset == (0x5555 & ADDRESS_MASK)) && (data == 0xaa)) {
      return change_to_state(STATE_PROTECTION_DISABLE_4);
    } else {
      return state_machine_error();
    }
  }

  if ((m_state == STATE_PROTECTION_DISABLE_4) && (offset == (0x2aaa & ADDRESS_MASK)) && (data == 0x55)) {
    return change_to_state(STATE_PROTECTION_DISABLE_5);
  }

  if ((m_state == STATE_PROTECTION_DISABLE_5) && (offset == (0x5555 & ADDRESS_MASK)) && (data == 0x20)) {
    // disable write protection.
    m_write_protected = false;
    // Write protection was disabled, and the preceding writes were just part of that command sequence.
    m_program_buffer_to_eeprom = false;
    // printf("m_program_buffer_to_eeprom -> %d\r\n", m_program_buffer_to_eeprom);

    if (TBLCUsec > 0) {
      // Explicitly start the programming cycle: disable the timer
      m_start_programming_timer->enable(false);
    }
    
    // and start the programming cycle
    return start_programming_cycle();
  } 

  // if (m_write_protected) {
  //   printf("X28C: write %02x to %x while write protected\n", data, offset);
  // }
}

template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead
>
uint8_t eeprom28<AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead>::read(uint32_t offset) {
  uint8_t data = m_storage[offset];

  if (m_state == STATE_PROTECTION_1) {
    // Per the X28C256 datasheet regarding the protection {en|dis}able sequence,
    //   "Note: Once initiated, the sequence of write operations
    //   should not be interrupted."
    // A read operation would seem to interrupt that sequence, and thus return us to the normal
    // 'buffering' state.
    change_to_state(STATE_BUFFERING);
  }

  if (ProgramOnRead) {
    if (m_state == STATE_BUFFERING) {
      // We have some buffered data, immediately write it to storage;
      // First, cancel any existing programming cycle timer
      if (TBLCUsec > 0) {
        m_start_programming_timer->enable(false);
      }
      // Then, start the programming cycle. If T_WC is 0, this will in turn also change the
      // state to STATE_IDLE.
      start_programming_cycle();
    }
  }

  if (m_program_buffer_to_eeprom || (m_state == STATE_PROGRAMMING)) {
    // "/DATA Polling"
    // While programming or preparing to progam, if the client reads back the 
    // last written byte, the returned bit 7 is the inverse of what was written.
    // But according to at least one datasheet:
    //   "If the X28C64 is in the protected state and an illegal write
    //    operation is attempted /DATA Polling will not operate."
    // so we only perform this if we're actually going to be writing to storage.
    if (m_program_buffer_to_eeprom && (offset == m_last_written_offset)) {
      data = data ^ INVERSE_DATA_BIT;
    }

    // While programming or preparing to progam, bit 6 of what is returned
    // alternates between 0 and 1.
    data = (data & ~TOGGLE_BIT) | m_toggle_bit;
    m_toggle_bit ^= TOGGLE_BIT;
  }

  return data;
}

template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead
>
void eeprom28<AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead>::start_programming_cycle() {
  change_to_state(STATE_PROGRAMMING);

  if (TWCUsec > 0) {
    m_programming_completed_timer->adjust(attotime::from_usec(TWCUsec));
  }
  
  if (m_program_buffer_to_eeprom) {
    std::copy(std::begin(m_page_buffer), std::end(m_page_buffer), &(m_storage[m_buffering_page]));
  }

  if (TWCUsec == 0) {
    programming_cycle_complete();
  }
}

template<
	int AddressBits, 
	uint32_t PageSizeBytes, 
	uint32_t TBLCUsec, 
	uint32_t TWCUsec,
	bool ProgramOnRead
>
void eeprom28<AddressBits, PageSizeBytes, TBLCUsec, TWCUsec, ProgramOnRead>::programming_cycle_complete() {
  change_to_state(STATE_IDLE);
  m_program_buffer_to_eeprom = false;
  // printf("m_program_buffer_to_eeprom -> %d\r\n", m_program_buffer_to_eeprom);
}


#endif // EEPROM28_IPP
