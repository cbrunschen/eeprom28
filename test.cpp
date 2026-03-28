#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include "eeprom28.hpp"

#include "env.hpp"

#include <tuple>

#define quote(x) #x

Clock tc; // the test clock

using types = std::tuple<x28c64, x28c256, x28hc256, x28c512, x28c010>;

TEMPLATE_LIST_TEST_CASE("Simple write steps", "", types) {
  tc.reset(4711L);

  printf("%s: ", typeid(TestType).name());
  printf("AddressBits = %d, total_size_bytes = %d, ", TestType::ADDRESS_BITS, TestType::TOTAL_BYTES);
  printf("T_BLC = %d usec, T_WC = %d usec\r\n", TestType::T_BLC_USEC, TestType::T_WC_USEC);

  TestType dut;
  dut.start();

  uint8_t values[] = { 17, 0, 255, 199 };  

  for (int i = 0; i < TestType::TOTAL_BYTES; i+= 17) {
    for (const auto &v : values) {
      dut.write(i, v);
      REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

      tc.advance(TestType::T_BLC_USEC / 10);
      REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

      tc.advance(TestType::T_BLC_USEC);
      REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);

      REQUIRE(dut.read(i) != v);
      REQUIRE(dut.read(i) != v);
      REQUIRE(dut.read(i) != v);

      tc.advance(TestType::T_WC_USEC);
      REQUIRE(dut.m_state == TestType::STATE_IDLE);
      REQUIRE(dut.read(i) == v);
    }
  }
}

TEMPLATE_LIST_TEST_CASE("Write protection recjects writes", "", types) {
  tc.reset(4711L);

  TestType dut;
  dut.m_write_protected = true;
  for (int i = 0; i < TestType::TOTAL_BYTES; i++) {
    dut.m_storage[i] = (i & 0xff);
  }
  dut.start();

  uint8_t values[] = { 17, 0, 255, 199 };  

  for (int i = 0; i < TestType::TOTAL_BYTES; i+= 23) {
    for (const auto &v : values) {
      uint8_t w = (i & 0xff);
      REQUIRE(dut.read(i) == w);

      dut.write(i, v);
      REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

      tc.advance(TestType::T_BLC_USEC / 10);
      REQUIRE(dut.m_state == TestType::STATE_BUFFERING);

      tc.advance(TestType::T_BLC_USEC);
      REQUIRE(dut.m_state == TestType::STATE_PROGRAMMING);

      uint8_t last = dut.read(i); 
      REQUIRE(last != w);
      uint8_t next = dut.read(i);
      REQUIRE(next != last);
      REQUIRE(next != w);

      last = next;
      next = dut.read(i);
      REQUIRE(next != last);
      REQUIRE(next != w);

      tc.advance(TestType::T_WC_USEC);
      REQUIRE(dut.m_state == TestType::STATE_IDLE);

      REQUIRE(dut.read(i) == w);
    }
  }
}
