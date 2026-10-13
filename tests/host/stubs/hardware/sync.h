#pragma once
#include "pico.h"
// The cores' memory barrier: a full fence between the test's threads.
static inline void __dmb() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
