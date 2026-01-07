#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Priority inversion lab:
// - mode=1: classic 3-thread inversion; enable PI and recover (expected PASS)
// Deadlock lab (2 locks / 2 threads + watchdog):
// - mode=2: intentionally deadlock (AB/BA) and detect it
// - mode=3: avoid deadlock via global lock ordering
// - mode=4: avoid deadlock via trylock + backoff
// - mode=5: avoid deadlock via simplified lockdep (cycle detection)
void sync_lab_setup(unsigned mode);

#ifdef __cplusplus
}
#endif
