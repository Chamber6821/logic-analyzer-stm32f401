#pragma once

#include <concepts>
#include <functional>
#include <utility>

template <std::invocable<> F> class Finally {
  [[no_unique_address]] F f;

public:
  Finally(F &&f) : f(std::move(f)) {}
  ~Finally() { std::invoke(f); }
};
