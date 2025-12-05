#pragma once
#include <cassert>
#define jassert assert
namespace juce {
    class CriticalSection { public: void enter() {} void exit() {} };
    class ScopedLock { public: ScopedLock(CriticalSection&) {} };
}
