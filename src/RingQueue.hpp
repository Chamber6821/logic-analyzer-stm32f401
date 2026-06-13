#pragma once

#include "Finally.hpp"
#include "main.h"

#include <array>
#include <cstddef>
#include <optional>

template <typename T, std::size_t Size> class RingQueue {
private:
  std::array<T, Size> m_buffer;
  std::size_t m_head = 0;
  std::size_t m_tail = 0;

public:
  using value_type = T;
  using pop_type = std::optional<T>;

  bool empty() const { return m_head == m_tail; }

  bool push(const T &item) {
    __disable_irq();
    Finally enable([]() { __enable_irq(); });
    if (not empty())
      return false;
    m_buffer[m_tail] = item;
    m_tail = (m_tail + 1) % Size;
    return true;
  }

  pop_type pop() {
    __disable_irq();
    Finally enable([]() { __enable_irq(); });
    if (empty())
      return std::nullopt;
    T item = m_buffer[m_head];
    m_head = (m_head + 1) % Size;
    return item;
  }

  std::size_t size() const {
    if (m_tail >= m_head) {
      return m_tail - m_head;
    }
    return Size - (m_head - m_tail);
  }
};
