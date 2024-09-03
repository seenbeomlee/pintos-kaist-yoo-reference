/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

	/**
	 * 공유자원(critical region)을 사용하고자 하는 thread는 sema_down을 실행하고,
	 * 사용 가능한 공유자원이 없다면 sema->waiters list에 list_push_back 함수로 맨 뒤에 추가된다. <- list_insert_ordered로 수정해야 하는 지점
	 */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		/**
		 * semaphore에 추가되는 element들은 thread 이므로, thread.c에서 사용하였던 thread_compare_priority 함수를 그대로 사용하면 된다.
		 */
		list_insert_ordered (&sema->waiters, &thread_current ()->elem, thread_compare_priority, 0);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/** 1
 * 현재는 공유자원의 사용을 마친 thread가 sema_up을 하면, thread_unblock을 하는데,
 * list_pop_front() 로 인해서 sema->waiters list의 상단에 있는 waiter thread를 unblock 시킨다.
 * 이를 ready_list의 round-robin을 해결하기 위해 적용했던 것처럼,
 * sema_up에서 list_push_back() 하던 것을 list_insert_ordered()로 변경한다.
 */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		/**
		 * sema_down 했다가, sema_up 하는 동안 waiters_list에 있는 thread들의 priority에 변경이 생겼을 수도 있다.
		 * 따라서, waiters_list를 내림차순으로 정렬하여 준다. -> 이럴거면 sema_up할때 굳이 왜 list_insert_ordered 하는거지?
		 */
		list_sort (&sema->waiters, thread_compare_priority, 0);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	/**
	 * unblock된 thread가 running thread보다 priority가 높을 수 있으므로, thread_test_preemtion ()을 통해 CPU 선점이 일어나도록 한다.
	 */
	thread_test_preemption ();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);
  // 처음 init되는 시점에서는 lock을 release (1->0)한 thread가 존재하지 않는다.
	lock->holder = NULL;
	// lock은 '1'로 초기화된 세마포어와 동일하다.
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/** 1
* lock_acquire()을 요청하는 스레드가 실행되고 있다는 자체로 lock을 가지고 있는 스레드보다 priority가 높다는 뜻이기 때문에,
if(cur->priority > lock->holder->priority) 등의 비교조건은 필요하지 않다.
*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();
  if (lock->holder) { // lock을 점유하고 있는 thread가 있다면,
    cur->wait_on_lock = lock; // lock_acquire()을 요청한 현재 thread의 wait_on_lock에 lock을 추가하고, 
    list_insert_ordered (&lock->holder->donations, &cur->donation_elem, // holder의 donations list에 현재 스레드를 추가한다.
    			thread_compare_donate_priority, 0);
/** 1
 * mlfqs 스케줄러는 시간에 따라 priority가 재조정되므로 priority donation을 사용하지 않는다.
 * 따라서, lock_acquire에서 구현해주었던 priority donation을 mlfqs에서는 비활성화 시켜주어야 한다.
 */
		if(!thread_mlfqs)
			donate_priority ();
  }
	// 현재 lock을 소유하고 있는 스레드가 없다면 해당하는 lock을 바로 차지하면 된다
  sema_down (&lock->semaphore); // sema_down을 기점으로 이전은 lock을 얻기 전, 이후는 lock을 얻은 후이다.
  
  cur->wait_on_lock = NULL;
  lock->holder = cur; // lock_release()를 호출할 수 있는 holder는 lock_acquire()을 호출한 thread가 된다.
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/** 1
 * sema_up하여 lock의 점유를 반환하기 이전에
 * 현재 이 lock을 사용하기 위해 나에게 priority를 빌려준 thread들을 donations list에서 제거하고,
 * 나의 priority를 재설정하는 작업이 필요하다.
 * 1. 남아있는 donations list에서 가장 높은 priority를 가지고 있는 thread의 priority를 받아서 cur의 priority로 설정하던가,
 * 2. donations list == NULL 이라면, 원래 값인 init_priority로 설정해주면 된다.
 */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock)); // lock이 semaphore와 다른 점은, lock_release()는 lock_holder만이 호출할 수 있다는 것이다.

	lock->holder = NULL;

/** 1
 * mlfqs 스케줄러는 시간에 따라 priority가 재조정되므로 priority donation을 사용하지 않는다.
 * 따라서, lock_release에서 구현해주었던 priority donation을 mlfqs에서는 비활성화 시켜주어야 한다.
 */
	if (!thread_mlfqs) {
		remove_with_lock (lock);
  	refresh_priority ();
	}
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	/** 1
	 * semaphore의 waiters는 thread들의 list 였다면,
	 * condtion의 waiters는 semaphore들의 list 이다.
	 * 이 역시도, cond_signal에서 list_pop_front 하므로, list_push_back()이 아니라, list_insert_ordered 해야한다.
	 * 이때, 입력받는 인자는 이전과 달리 thread가 아니라, semaphore_elem 구조체이기 때문에 따로 함수를 선언해야 한다.
	 */
	// list_push_back (&cond->waiters, &waiter.elem); 
	list_insert_ordered (&cond->waiters, &waiter.elem, sema_compare_priority, 0);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
	/**
	 * cond->waiters는 semaphore들의 list이다. (즉 2중 list임)
	 * 이때, semaphore list는 list_push_back()이 아니라, list_insert_ordered()를 통해 내림차순으로 정렬시켰다.
	 * 따라서, waiter의 최상단에는 이미 각 semaphore waiters list 안에서 가장 priority가 높은 thread 가 놓여있다.
	 */
		list_sort (&cond->waiters, sema_compare_priority, 0); // wait 도중에 priority가 바뀌었을 수 있으니, list_sort()로 내림차순 정렬해준다.
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore); 
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

/** 1
 * cond_wait()에서 list_push_back()이 아니라, list_insert_ordered() 하기 위해서 필요하다.
 * 이때, cond_waiters는 thread들의 list가 아니라, semaphore들의 list이기 때문에, 새로운 비교 함수를 정의한다.
 */
bool 
sema_compare_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED) {
	struct semaphore_elem *l_sema = list_entry (l, struct semaphore_elem, elem);
	struct semaphore_elem *s_sema = list_entry (s, struct semaphore_elem, elem);

	struct list *waiter_l_sema = &(l_sema->semaphore.waiters);
	struct list *waiter_s_sema = &(s_sema->semaphore.waiters);

	// l의 priority가 s의 priority보다 작다면 true를 반환하여야 priority를 기준으로 내림차순으로 정렬된다.
	return list_entry (list_begin (waiter_l_sema), struct thread, elem)->priority
		 > list_entry (list_begin (waiter_s_sema), struct thread, elem)->priority;
}