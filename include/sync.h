#pragma once

#include "thread.h"

#ifdef __cplusplus
extern "C" {
#endif

// A simple non-recursive mutex with optional priority inheritance.
struct mutex {
  Thread* owner;
  Thread* waiters;      // wait-queue (Thread::wait_next)
  mutex*  owner_next;   // link in Thread::owned_mutexes
  int     pi_enabled;
};

void mutex_init(mutex* m);
void mutex_set_pi_enabled(mutex* m, int enabled);
void mutex_lock(mutex* m);
// Try to acquire a mutex without blocking.
// Returns 0 on success, -1 if the mutex is currently owned by another thread.
int  mutex_trylock(mutex* m);
void mutex_unlock(mutex* m);

// Counting semaphore.
struct semaphore {
  int     count;        // may become negative while waiters exist
  Thread* waiters;      // wait-queue (Thread::wait_next)
};

void sem_init(semaphore* s, int initial_count);
void sem_down(semaphore* s);
void sem_up(semaphore* s);

#ifdef __cplusplus
}
#endif
