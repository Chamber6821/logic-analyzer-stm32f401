#pragma once

#include <concepts>
#include <functional>
#include <utility>

template <std::invocable<> F> class Finnaly {
  [[no_unique_address]] F f;

public:
  Finnaly(F &&f) : f(std::move(f)) {}
  ~Finnaly() { std::invoke(f); }
};
