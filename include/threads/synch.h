#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>
#include <debug.h> // for using UNUSED

/* A counting semaphore. */
/** Semaphores
 * 여러 스레드에 의해 공유되어 사용되는 자원을 공유자원이라고 한다.
 * 각 스레드에서 이 공유자원을 엑세스 하는 코드구역을 임계구역(Critical Section)이라고 한다.
 * 세마포(Semaphores)는 공유자원을 여러 스레드가 동시에 접근하지 못하도록 하는 도구로,
 * 하나의 non-negative integer == unsigned value
 * 두 개의 operators로 이루어진다. == up & down
 */
struct semaphore {
	unsigned value;             /* Current value. */
	struct list waiters;        /* List of waiting threads. */
};

void sema_init (struct semaphore *, unsigned value);
/** sema_down
 * 어떤 스레드가 임계구역으로 들어와서 공유자원을 사용하고자 요청할 때 실행된다. 
 * 현재 사용 가능한 공유자원의 수가 1개 이상이면,
 * 이 수를 1만큼 줄이고 임계구역을 실행(공유자원을 사용)한다.
 * 만약, 현재 사용 가능한 공유자원의 개수가 0개 이하면, 
 * 이 값이 양수가 될 때까지 임계구역을 실행하지 않고 기다린다.
 * 
 * 1. 0으로 초기화 되는 경우 == 스레드 A가 우선 기다림 -> 스레드 B의 작업이 끝나면 A가 실행됨.
 * 2. 1로 초기화 되는 경우 == 스레드 A가 우선 실행됨 -> A의 작업이 끝나면 기다리던 B가 실행됨.
 */
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
/** sema_up
 * 스레드가 임계구역의 실행을 모두 마치고 공유자원을 반납할 때 실행된다.
 * 사용 가능한 공유자원의 개수를 1만큼 늘린다.
 */
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
/** lock
 * 초기화 값이 '1'인 세마포어와 비슷하게 동작한다.
 * 그리고, 해당 lock을 호출한 thread의 주솟값인 *holder를 인자로 갖는다.
 */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init (struct lock *);
/**
 * lock_acquire를 호출한 스레드만이 해당 lock을 release할 수 있다.
 * 즉, '0'으로 초기화된 세마포의 설명에서 세마포는 스레드 A에서 down을 하고,
 * B에서 up이 가능하였지만, lock은 이러한 동작이 불가능하다.
 * 따라서, 이러한 제약으로 인해 문제가 발생한다면, 세마포를 사용해야 한다.
 */
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/** 1 */
bool sema_compare_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.
 * 즉, 컴파일러가 프로그램의 최적화를 위해 임의로 statements를 reorder하는 것을 막아준다. 
 */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
