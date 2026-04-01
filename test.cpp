#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

#define EEPROM28_VISIBLE_FOR_TESTING 1
#include "eeprom28.hpp"
#undef EEPROM28_VISIBLE_FOR_TESTING

#include "env.hpp"

#include <tuple>
#include <cstring>
#include <iostream>
#include <iomanip>

#define REQUIRE_MESSAGE(cond, msg) do { INFO(msg); REQUIRE(cond); } while((void)0, 0)
#define REQUIRE_BYTES(actual, expected) do {                                 \
	uint8_t aval = actual;                                                     \
	INFO("Expected " << std::hex << std::setw(2) << static_cast<int>(expected) \
    << ", have " << std::hex << std::setw(2) << static_cast<int>(aval));     \
	REQUIRE(aval == expected);                                               \
} while((void)0, 0)

using types = std::tuple<x28c64, x28c256, x28hc256, x28c512, x28c010, xm28c020, xm28c040, x28i256, x28f256, at28c256, at28c256f, at28c64b, at28hc64bf>;
using atmels = std::tuple<at28c256, at28c256f, at28c64b, at28hc64bf>;

template<typename TestType> void verify_write(TestType &dut, uint32_t i, uint8_t v) {
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

	auto t_into_blc = TestType::T_BLC_USEC / 2;
	auto t_blc_remaining = TestType::T_BLC_USEC - t_into_blc;

	global_clock.advance(t_into_blc);
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

	// Advance to the programming cycle
	if (TestType::T_BLC_USEC > 0) {
		// advance into the middle of the programming cycle.
		global_clock.advance(t_blc_remaining + TestType::T_WC_USEC / 2);
	} else {
		// when T_BLC_USEC == 0, we must trigger the programming cycle by reading
		dut.read(i);
	}

	// If we have a non-zero duration programming cycle, check that for 
	if (TestType::T_WC_USEC > 0) {
		REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);

		REQUIRE(dut.read(i) != v);
		REQUIRE(dut.read(i) != v);
		REQUIRE(dut.read(i) != v);

		// advance past the end of the programming cycle
		global_clock.advance(TestType::T_WC_USEC);
		REQUIRE(dut.m_state == TestType::STATE_IDLE);
	}
	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(dut.read(i) == v);
}

template<typename TestType> void complete_write(TestType &dut) {
	if (TestType::T_BLC_USEC > 0) {
		// allow the programming cycle to happen
		global_clock.advance(TestType::T_BLC_USEC + TestType::T_WC_USEC + 1000);
	} else {
		// trigger the programming cycle
		dut.read(0);
	}
}

TEMPLATE_LIST_TEST_CASE("Show Device Info", "", types) {
	printf("%s:\n", typeid(TestType).name());
	printf("  AddressBits = %d, DataSizeBytes = %d, PageSizeBytes = %d\n",
		TestType::ADDRESS_BITS, TestType::DATA_SIZE_BYTES, TestType::PAGE_SIZE_BYTES);
	printf("  T_BLC = %d usec, T_WC = %d usec%s\n",
		TestType::T_BLC_USEC, TestType::T_WC_USEC, TestType::PROGRAM_ON_READ ? ", Program On Read" : "");
	if (TestType::HAS_ID_PAGE) {
		printf("  Has ID Page at Offset %d, TotalSizeBytes = %d\n", TestType::ID_PAGE_OFFSET, TestType::TOTAL_SIZE_BYTES);
	}
	if (TestType::HAS_HARDWARE_CHIP_ERASE || TestType::HAS_SOFTWARE_CHIP_ERASE) {
		printf("  Has %s Chip Erase, T_CE = %d usec\n",
			TestType::HAS_HARDWARE_CHIP_ERASE 
			? (TestType::HAS_SOFTWARE_CHIP_ERASE ? "Hardware and Software" : "Hardware")
			: "Software",
			TestType::T_CE_USEC);
	}

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Write one byte without protection", "", types) {
	global_clock.reset(4711L);
	TestType dut;
	const uint8_t base = 0x3a;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.start();

	auto i = GENERATE(take(17, random((int)0, (int)TestType::DATA_SIZE_BYTES-1)));
	uint8_t v = GENERATE(take(3, random(0, 255)));

	dut.write(i, v);
	verify_write<TestType>(dut, i, v);

	// Build our expected view of what's in the EEPROM:
	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	// Filled with the base value
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);
	// ... except where we just wrote.
	expected[i] = v;

	// Check that the contents match: i.e., only the written-to address has changed.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Write protection rejects writes", "", types) {
	global_clock.reset(4711L);

	const uint8_t base = 0x5e;
	TestType dut;
	dut.m_write_enabled = false;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.start();
	dut.read(0);

	auto i = GENERATE(take(17, random((int)0, (int)TestType::DATA_SIZE_BYTES-1)));
	uint8_t v = GENERATE(take(3, random(0, 255)));

	REQUIRE(dut.read(i) == base);

	dut.write(i, v);
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

	global_clock.advance(TestType::T_BLC_USEC / 10);
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

	if (TestType::T_BLC_USEC > 0 && TestType::T_WC_USEC > 0) {
		global_clock.advance(TestType::T_BLC_USEC);
		REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);
	}

	uint8_t next = dut.read(i); 
	REQUIRE((next & 0x3f) == (base & 0x3f));

	if (TestType::T_WC_USEC > 0) {
		uint8_t last = next;
		next = dut.read(i);
		REQUIRE(next != last);
		REQUIRE((next & 0x3f) == (base & 0x3f));

		last = next;
		next = dut.read(i);
		REQUIRE(next != last);
		REQUIRE((next & 0x3f) == (base & 0x3f));

		global_clock.advance(TestType::T_WC_USEC);
	}
	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	REQUIRE(dut.read(i) == base);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Write Protection takes hold", "", types) {
	global_clock.reset(4711);
	TestType dut;
	dut.m_write_enabled = true; // start with writes enabled / no write protection.
	dut.start();

	REQUIRE(dut.m_write_enabled);

	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_1);

	global_clock.advance(TestType::T_BLC_USEC / 2);

	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_2);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xa0);

	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);
	REQUIRE(dut.m_state == TestType::STATE_PROTECTED_WRITE);

	if (TestType::T_BLC_USEC > 0) {
		global_clock.advance(TestType::T_BLC_USEC);
	} else {
		// we must trigger programming through read(), somewhere.
		dut.read(0);
	}

	if (TestType::T_WC_USEC > 0) {
		REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);
		global_clock.advance(TestType::T_WC_USEC);
	}

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(!dut.m_write_enabled);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Write Protection Sequence allows writing", "", types) {
	global_clock.reset(4711);
	TestType dut;
	dut.m_write_enabled = false;  // start with writes disabled / write protection on.
	dut.start();

	REQUIRE(!dut.m_write_enabled);

	dut.write((0x5555 & TestType::ADDRESS_MASK), 0xaa);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_1);

	global_clock.advance(TestType::T_BLC_USEC / 2);

	dut.write((0x2aaa & TestType::ADDRESS_MASK), 0x55);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_2);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write((0x5555 & TestType::ADDRESS_MASK), 0xa0);

	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);
	REQUIRE(dut.m_state == TestType::STATE_PROTECTED_WRITE);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x1231, 0x19);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_buffering_page == (0x1230 & TestType::PAGE_MASK));
	REQUIRE(dut.m_program_buffer_to_eeprom);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x1234, 0x20);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_program_buffer_to_eeprom);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x1237, 0x21);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_program_buffer_to_eeprom);

	if (TestType::T_BLC_USEC > 0) {
		global_clock.advance(TestType::T_BLC_USEC);
	} else {
		dut.read(0);
	}

	if (TestType::T_WC_USEC > 0) {
		REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);
		global_clock.advance(TestType::T_WC_USEC);
	}

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(!dut.m_write_enabled);  // we are still write protected

	// And all the writes have succeeded.
	REQUIRE(dut.m_storage[0x1231] == 0x19);
	REQUIRE(dut.m_storage[0x1234] == 0x20);
	REQUIRE(dut.m_storage[0x1237] == 0x21);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Write Un-Protection takes hold", "", types) {
	global_clock.reset(4711);
	TestType dut;
	dut.m_write_enabled = false;
	dut.start();

	REQUIRE(!dut.m_write_enabled);

	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_1);

	global_clock.advance(TestType::T_BLC_USEC / 2);

	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_2);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0x80);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_PROTECION_DISABLE_3);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_PROTECION_DISABLE_4);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_PROTECION_DISABLE_5);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0x20);

	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);

	if (TestType::T_WC_USEC > 0) {
		REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);
		global_clock.advance(TestType::T_WC_USEC);
	}

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(dut.m_write_enabled);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Multiple writes to the same page work", "", types) {
	global_clock.reset(4711);
	TestType dut;
	const uint8_t base = 0xbd;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);

	// Also keep our expected view of what's in the EEPROM:
	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	// Filled with the base value, initially.
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);

	dut.start();

	auto page = GENERATE(take(5, random((int)0, (int)(TestType::DATA_SIZE_BYTES / TestType::PAGE_SIZE_BYTES)-1)));
	auto n_bytes = 7 + (rand() % (TestType::PAGE_SIZE_BYTES - 7));
	for (int i = 0; i < n_bytes; i++) {
		auto address = page * TestType::PAGE_SIZE_BYTES + (rand() & TestType::PAGE_OFFSET_MASK);
		uint8_t v = rand() & 0xff;
		dut.write(address, v);
		// advance by less than T_BLC
		global_clock.advance(TestType::T_BLC_USEC / 2);
		// We should still be in the buffering state
		REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

		// also update our expected view of memory
		expected[address] = v;
	}

	complete_write(dut);

	// Check that the contents match: i.e., only the written-to addresses have changed - but indeed, all of them have.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Multiple writes across pages only affect the first mentioned page", "", types) {
	global_clock.reset(4711);
	TestType dut;
	const uint8_t base = 0xbd;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);

	// Also keep our expected view of what's in an ontouched page of the EEPROM:
	std::array<uint8_t, TestType::PAGE_SIZE_BYTES> expected;
	// Filled with the base value, initially.
	std::memset(&expected[0], base, TestType::PAGE_SIZE_BYTES);

	dut.start();

	constexpr auto n_pages = TestType::DATA_SIZE_BYTES / TestType::PAGE_SIZE_BYTES;
	constexpr auto last_page = n_pages - 1;

	auto page = GENERATE(take(5, random((int)0, (int)last_page)));

	auto first_address = page * TestType::PAGE_SIZE_BYTES + (rand() & TestType::PAGE_OFFSET_MASK);
	if (first_address == (0x5555 & TestType::ADDRESS_MASK)) {
		// ensure we don't look like we're trying to start a command sequence
		first_address++;
	}
	uint8_t v = rand() & 0xff;
	dut.write(first_address, v);
	// advance by less than T_BLC
	global_clock.advance(TestType::T_BLC_USEC / 2);
	// We should now be in the buffering state
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

	auto n_bytes = 7 + (rand() % (TestType::PAGE_SIZE_BYTES - 7));
	for (int i = 1; i < n_bytes; i++) {
		// Write to _any_ address - 50% of the time on the same page, 50% of the time, maybe not!
		uint32_t address = (rand() & 1) 
			? (rand() & TestType::ADDRESS_MASK) 
			: page * TestType::PAGE_SIZE_BYTES + (rand() & TestType::PAGE_OFFSET_MASK);
		uint8_t v = rand() & 0xff;
		dut.write(address, v);
		// advance by less than T_BLC
		global_clock.advance(TestType::T_BLC_USEC / 2);
		// We should still be in the buffering state
		REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	}

	complete_write(dut);

	// Check no pages other than the first page touched, have changed 
	// - and that the first-touched page _has_ changed.
	for (auto p = 0; p < n_pages; p++) {
		auto comparison_result = std::memcmp(&dut.m_storage[(p * TestType::PAGE_SIZE_BYTES)], &expected[0], TestType::PAGE_SIZE_BYTES);
		if (p == page) {
			REQUIRE(comparison_result != 0);
		} else {
			REQUIRE(comparison_result == 0);
		}
	}

	cleanup_global_timers();
}

TEST_CASE("Fast EEPROM, not write protected, write takes hold immediately upon read", "") {
	using TestType = x28f256;

	global_clock.reset(4711);
	TestType dut;
	uint8_t base = 0xd9;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.m_write_enabled = true;
	dut.start();

	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);

	auto offset = GENERATE(take(17, random((uint32_t)0, (uint32_t)TestType::DATA_SIZE_BYTES-1)));
	uint8_t value = 0x3a;

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	// write to our device under test
	dut.write(offset, value);
	// also update our expected view of memory
	expected[offset] = value;

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	uint8_t actual = dut.read(offset);

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(actual == value);

	// Check that the contents match: i.e., only the written-to addresses have changed - but indeed, all of them have.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEST_CASE("Fast EEPROM, not write protected, write takes hold after T_BLC timer expipred", "") {
	using TestType = x28f256;

	global_clock.reset(4711);
	TestType dut;
	uint8_t base = 0xd9;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.m_write_enabled = true;
	dut.start();

	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);

	auto offset = GENERATE(take(17, random((uint32_t)0, (uint32_t)TestType::DATA_SIZE_BYTES-1)));
	uint8_t value = 0x3a;

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	// write to our device under test
	dut.write(offset, value);
	// also update our expected view of memory
	expected[offset] = value;

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

	global_clock.advance(3 * TestType::T_BLC_USEC / 2);

	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	uint8_t actual = dut.m_storage[offset];
	REQUIRE(actual == value);

	// Check that the contents match: i.e., only the written-to addresses have changed - but indeed, all of them have.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEST_CASE("Fast EEPROM, write protected, protected write takes hold immediately upon read", "") {
	using TestType = x28f256;

	global_clock.reset(4711);
	TestType dut;
	uint8_t base = 0xd9;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.m_write_enabled = false;
	dut.start();

	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);

	auto offset = GENERATE(take(17, random((uint32_t)0, (uint32_t)TestType::DATA_SIZE_BYTES-1)));
	uint8_t value = 0x3a;

	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	// Perform the write protection command sequence
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_1);
	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xa0);
	REQUIRE(dut.m_state == TestType::STATE_PROTECTED_WRITE);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);

	// write some data to our device under test
	dut.write(offset, value);
	// also update our expected view of memory
	expected[offset] = value;

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	uint8_t actual = dut.read(offset);

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(actual == value);

	// Check that the contents match: i.e., only the written-to addresses have changed - but indeed, all of them have.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEST_CASE("Fast EEPROM, write protected, protected write takes hold upon T_BLC timer expiry", "") {
	using TestType = x28f256;

	global_clock.reset(4711);
	TestType dut;
	uint8_t base = 0xd9;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.m_write_enabled = false;
	dut.start();

	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);

	auto offset = GENERATE(take(17, random((uint32_t)0, (uint32_t)TestType::DATA_SIZE_BYTES-1)));
	uint8_t value = 0x3a;

	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	// Perform the write protection command sequence
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_1);
	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xa0);
	REQUIRE(dut.m_state == TestType::STATE_PROTECTED_WRITE);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);

	// write some data to our device under test
	dut.write(offset, value);
	// also update our expected view of memory
	expected[offset] = value;

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

	global_clock.advance(3 * TestType::T_BLC_USEC / 2);

	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	uint8_t actual = dut.m_storage[offset];
	REQUIRE(actual == value);

	// Check that the contents match: i.e., only the written-to addresses have changed - but indeed, all of them have.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEST_CASE("Immediate EEPROM, not write protected, write takes hold immediately upon read", "") {
	using TestType = x28i256;

	global_clock.reset(4711);
	TestType dut;
	uint8_t base = 0xd9;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.m_write_enabled = true;
	dut.start();

	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);

	auto offset = GENERATE(take(17, random((uint32_t)0, (uint32_t)TestType::DATA_SIZE_BYTES-1)));
	uint8_t value = 0x3a;

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	// write to our device under test
	dut.write(offset, value);
	// also update our expected view of memory
	expected[offset] = value;

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	uint8_t actual = dut.read(offset);

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(actual == value);

	// Check that the contents match: i.e., only the written-to addresses have changed - but indeed, all of them have.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEST_CASE("Immediate EEPROM, write protected, protected write takes hold immediately upon read", "") {
	using TestType = x28i256;

	global_clock.reset(4711);
	TestType dut;
	uint8_t base = 0xd9;
	std::memset(&dut.m_storage[0], base, TestType::DATA_SIZE_BYTES);
	dut.m_write_enabled = false;
	dut.start();

	std::array<uint8_t, TestType::DATA_SIZE_BYTES> expected;
	std::memset(&expected[0], base, TestType::DATA_SIZE_BYTES);

	auto offset = GENERATE(take(17, random((uint32_t)0, (uint32_t)TestType::DATA_SIZE_BYTES-1)));
	uint8_t value = 0x3a;

	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	// Perform the write protection command sequence
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_1);
	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xa0);
	REQUIRE(dut.m_state == TestType::STATE_PROTECTED_WRITE);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);

	// write some data to our device under test
	dut.write(offset, value);
	// also update our expected view of memory
	expected[offset] = value;

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	uint8_t actual = dut.read(offset);

	REQUIRE(dut.m_state == TestType::STATE_IDLE);
	REQUIRE(actual == value);

	// Check that the contents match: i.e., only the written-to addresses have changed - but indeed, all of them have.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::DATA_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Total Size = Data Size + (1 page for ID iff this type has an ID page)", "", types) {
	if (TestType::HAS_ID_PAGE) {
		REQUIRE(TestType::TOTAL_SIZE_BYTES == TestType::DATA_SIZE_BYTES + TestType::PAGE_SIZE_BYTES);
	} else {
		REQUIRE(TestType::TOTAL_SIZE_BYTES == TestType::DATA_SIZE_BYTES);
	}
}

TEMPLATE_LIST_TEST_CASE("Write to both data and ID page", "", atmels) {

	global_clock.reset(4711L);
	TestType dut;
	const uint8_t base = 0x3a;
	std::memset(&dut.m_storage[0], base, TestType::TOTAL_SIZE_BYTES);

	dut.start();

	auto onpage = GENERATE(take(17, random((int)0, (int)TestType::PAGE_SIZE_BYTES-1)));
	auto opposite = onpage ^ TestType::PAGE_OFFSET_MASK;
	auto i = TestType::ID_PAGE_OFFSET + onpage;
	auto mi = TestType::ID_PAGE_OFFSET + opposite;

	uint8_t v = GENERATE(take(3, random(0, 255)));
	uint8_t w = 0xff ^ v;
	uint8_t x = 0x5a ^ v;

	dut.set_access_id_page(false);
	dut.write(i, v);
	verify_write(dut, i, v);

	dut.set_access_id_page(true);
	dut.write(i, w);
	verify_write(dut, i, w);

	dut.set_access_id_page(false);
	dut.write(mi, x);
	verify_write(dut, mi, x);

	// Build our expected view of what's in the EEPROM:
	std::array<uint8_t, TestType::TOTAL_SIZE_BYTES> expected;
	// Filled with the base value
	std::memset(&expected[0], base, TestType::TOTAL_SIZE_BYTES);
	// ... except where we just wrote: 
	// The normal write should end up on its actual address;
	expected[i] = v;
	expected[mi] = x;
	// the ID page write must be in the ID page after the data,
	// so shifted up by one page size compared to its "address"
	expected[i + TestType::PAGE_SIZE_BYTES] = w;

	// Check that the contents match: i.e., only the written-to address has changed.
	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::TOTAL_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Read from both data and ID page", "", atmels) {
	global_clock.reset(4711L);
	TestType dut;
	const uint8_t base = 0x3a;
	std::memset(&dut.m_storage[0], base, TestType::TOTAL_SIZE_BYTES);

	auto onpage = GENERATE(take(17, random((int)0, (int)TestType::PAGE_SIZE_BYTES-1)));
	auto opposite = onpage ^ TestType::PAGE_OFFSET_MASK;
	auto i = TestType::ID_PAGE_OFFSET + onpage;
	auto mi = TestType::ID_PAGE_OFFSET + opposite;

	uint8_t v = GENERATE(take(3, random(0, 255)));
	uint8_t w = 0xff ^ v;
	uint8_t x = 0x5a ^ v;

	// Put the expected data into the device's storage
	dut.m_storage[i] = v;
	dut.m_storage[i + TestType::PAGE_SIZE_BYTES] = w;
	dut.m_storage[mi] = x;

	dut.start();

	REQUIRE_BYTES(dut.read(i), v);
	REQUIRE_BYTES(dut.read(mi), x);

	dut.set_access_id_page(true);

	REQUIRE_BYTES(dut.read(i), w);
	REQUIRE_BYTES(dut.read(mi), base);

	dut.set_access_id_page(false);

	REQUIRE_BYTES(dut.read(i), v);
	REQUIRE_BYTES(dut.read(mi), x);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Software Chip Erase", "", atmels) {
	global_clock.reset(4711L);
	TestType dut;
	const uint8_t base = 0x3a;
	std::memset(&dut.m_storage[0], base, TestType::TOTAL_SIZE_BYTES);

	dut.start();

	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_1);

	global_clock.advance(TestType::T_BLC_USEC / 2);

	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);
	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_2);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0x80);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_PROTECION_DISABLE_3);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0xaa);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_PROTECION_DISABLE_4);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x2aaa & TestType::ADDRESS_MASK, 0x55);

	REQUIRE(dut.m_state == TestType::STATE_BUFFERING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_PROTECION_DISABLE_5);

	global_clock.advance(TestType::T_BLC_USEC / 2);
	dut.write(0x5555 & TestType::ADDRESS_MASK, 0x10);  // The Software Chip Erase Command

	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);

	if (TestType::T_CE_USEC > 0) {
		REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);
		global_clock.advance(TestType::T_CE_USEC);
	}

	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	// Expected: all bytes are not 0xff
	std::array<uint8_t, TestType::TOTAL_SIZE_BYTES> expected;
	std::memset(&expected[0], 0xff, TestType::TOTAL_SIZE_BYTES);

	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::TOTAL_SIZE_BYTES) == 0);

	cleanup_global_timers();
}

TEMPLATE_LIST_TEST_CASE("Hardware Chip Erase", "", atmels) {
	global_clock.reset(4711L);
	TestType dut;
	const uint8_t base = 0x3a;
	std::memset(&dut.m_storage[0], base, TestType::TOTAL_SIZE_BYTES);

	dut.start();

	dut.set_chip_erase(true);

	// A write _anywhere_ will trigger chip erase.
	dut.write(0, 0);

	REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);
	REQUIRE(dut.m_command_state == TestType::COMMAND_STATE_NONE);

	if (TestType::T_CE_USEC > 0) {
		REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);
		global_clock.advance(TestType::T_CE_USEC);
	}

	REQUIRE(dut.m_state == TestType::STATE_IDLE);

	// Expected: all bytes are not 0xff
	std::array<uint8_t, TestType::TOTAL_SIZE_BYTES> expected;
	std::memset(&expected[0], 0xff, TestType::TOTAL_SIZE_BYTES);

	REQUIRE(std::memcmp(&dut.m_storage[0], &expected[0], TestType::TOTAL_SIZE_BYTES) == 0);

	cleanup_global_timers();
}
