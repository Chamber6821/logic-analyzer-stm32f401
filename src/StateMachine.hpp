#pragma once

#include <concepts>
#include <functional>
#include <utility>

template <class State, class Queue, class Transition>
  requires std::invocable<Transition, State, typename Queue::pop_type>
class StateMachine {
  [[no_unique_address]] Queue inputQueue;
  [[no_unique_address]] Transition transition;
  [[no_unique_address]] State _state;

public:
  StateMachine(State &&initialState, Queue &&queue, Transition &&transition)
      : inputQueue(std::move(queue)), transition(std::move(transition)),
        _state(std::move(initialState)) {}
  auto state() const { return _state; }
  auto input(typename Queue::value_type input) {
    return inputQueue.push(input);
  }
  void update() { _state = std::invoke(transition, _state, inputQueue.pop()); }
};
