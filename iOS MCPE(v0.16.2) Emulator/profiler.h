// Sampling profiler for the guest threads (EMU_PROFILE=<report interval in seconds>).
// Every millisecond each registered thread is suspended briefly and its host
// instruction pointer recorded; reports in emulator.log show, per thread, how much
// time goes to JIT-generated guest code, the JIT compiler, HLE functions and the OS.
#pragma once

namespace profiler {
void init();             // starts the sampler thread when EMU_PROFILE is set
void register_thread(const char* name);  // call on the thread itself
void unregister_thread();
void set_thread_name(const char* name);  // current thread
}  // namespace profiler
