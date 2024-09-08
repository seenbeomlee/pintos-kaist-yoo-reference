#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
/** 1
 * thread.c에서 fp 연산을 할 수 있도록 fixed_point.h 파일을 include한다.
 */
#include "threads/fixed_point.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/** 1
 * 현재 핀토스에서 thread를 관리하는 list는 ready_list와 all_list 두 개만 존재한다.
 * 잠이 들어 block 상태가 된 thread들은 all_list에 존재하지만,
 * sleep state인 thread들만 보관하는 리스트를 만들어 관리한다.
 */
static struct list sleep_list;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/** project1-Advanced Scheduler */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/** 1
 * for advanced scheduler
 * 최근 1분 동안 수행 가능한 thread의 평균 개수를 나타내며, 실수 값을 가진다. <= 이 역시도 recent_cpu와 값이 지수 가중 평균이동으로 구한다.
 * priority, recent_cpu 값은 각 thread 별로 그 값을 가지는 반면 load_avg 는 system-wide 값으로 시스템 내에서 동일한 값을 가진다.
 * 매 1초마다 load_avg 값을 재계산한다.
 * LOAD_AVG_DEFAULT == 0
 * load_avg = (59/60) * load_avg + (1/60) * ready_threads이다.
 * 이때, ready_threads값은 업데이트 시점에 ready(running + ready to run) 상태의 스레드의 개수를 나타낸다.
 * 
 * 이 값이 크면 recent_cpu 값은 천천히 감소(priority는 천천히 증가)하고,
 * 이 값이 작으면 recent_cpu 값은 빠르게 감소(priority는 빠르게 증가)한다.
 * 
 * 왜냐하면, 수행 가능한 thread의 평균 개수가 많을 때(load_avg 값이 클때)는 
 * 모든 thread가 골고루 CPU time을 배분받을 수 있도록 이미 사용한 thread의 priority가 천천히 증가해야 한다.
 * 
 * 반대로, 수행 가능한 thread의 평균 개수가 적을 때(load_avg 값이 작을때)는
 * 조금 더 빠르게 증가해도 모든 thread가 골고루 CPU time을 배분받을 수 있기 때문이다.
 */
int load_avg;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list); // 1 sleep_list를 추가하였으므로, 초기화한다.
	list_init (&all_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	/** 1
	 * thread_create하는 순간 idle thread가 생성되고, 동시에 idle 함수가 실행된다.
	 */
	thread_create ("idle", PRI_MIN, idle, &idle_started);

  load_avg = LOAD_AVG_DEFAULT; // idle thread가 start하는 순간 load_avg도 초기화한다.

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	/**
	 * 이렇게 증가한 ticks가 TIME_SLICE보다 커지는 순간에 intr_yield_on_return()이라는 인터럽트가 실행된다.
	 * 이 인터럽트는 결과적으로 thread_yield()를 실행시킨다.
	 * 즉, 하나의 thread에서 scheduling 함수들이 호출되지 않더라도, time_interrupt에 의해서
	 * 일정 시간(ticks >= TIME_SLICE)마다 자동으로 scheduling이 발생한다.
	 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	#ifdef USERPROG
	/** 2
	 * file descriptor
 	 */
	t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES); // thread_create() 시에 동적할당 되도록 한다.
	if (t->fdt == NULL) // 동적할당에 실패했을 경우
		return TID_ERROR;

	t->exit_status = 0; // exit_status 초기화

	t->fd_idx = 3; // 항상 3부터 할당되고, 비어있는 인덱스 중에서 작은 인덱스에 할당된다.
	t->fdt[0] = STDIN; // stdin 예약된 자리 (dummy)
	t->fdt[1] = STDOUT; // stdout 예약된 자리 (dummy)
	t->fdt[2] = STDERR; // stderr 예약된 자리 (dummy)

/** 2
 * hierarchical process structure
 */
	list_push_back(&thread_current()->child_list, &t->child_elem);
#endif

	/* Call the kernel_thread if it scheduled. 
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	/** 1
	 * thread_unblock(), thread_yield, thread_create()의 경우 list_puch_back이 list_insert_ordered로 수정되어야 한다.
	 * thread_create()의 경우, 새로운 thread가 ready_list에 추가되지만, thread_unblock() 함수를 포함하기 때문에 unblock() 수정하면 얘도 같이 수정된다.
	 */
	thread_unblock (t);

	/** 1
	 * runnning thread의 priority 변경으로 인한 priority 재확인
	 */
	thread_test_preemption ();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule (); // running thread가 CPU를 양보한다.
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	// list_push_back (&ready_list, &t->elem); // unblock된 thread를 ready_list에 추가한다.
	list_insert_ordered (&ready_list, &t->elem, thread_compare_priority, 0); // priority scheduling (1)
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif
	/** project1-Advanced Scheduler */
	if (thread_mlfqs)
		list_remove(&thread_current()->all_elem);

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING); // running thread가 CPU를 양보한다.
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		// list_push_back (&ready_list, &curr->elem); // yield 된 thread를 ready_list에 추가한다.
		list_insert_ordered (&ready_list, &curr->elem, thread_compare_priority, 0); // priority scheduling (1)
	do_schedule (THREAD_READY); // running thread가 CPU를 양보한다.
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
/** 1
 * mlfqs scheduler 에서는 priority를 임의로 변경할 수 없기 때문에,
 * thread_set_priority() 함수 역시 비활성화 시켜야 한다.
 */
	if (thread_mlfqs)
		return;

	thread_current ()->init_priority = new_priority;

	/** 1
	 * 만약, 현재 진행중인 running thread의 priority 변경이 일어났을 때, (즉, init_priority가 변경되었을 때)
	 * donations list에 있는 thread들보다 priority가 높아지는 경우가 발생할 수 있다.
	 * 이 경우 priority는 donations list 중 가장 높은 priority가 아니라, 새로 바뀐 priority가 적용될 수 있게 해야 한다.
	 */
	refresh_priority ();
	/** 1
	 * runnning thread의 priority 변경으로 인한 priority 재확인
	 */
	thread_test_preemption ();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/** 1
 * 각 값들을 변경할 시에는 interrupt의 방해를 받지 않도록 interrupt를 비활성화 해야 한다.
 * 1. void thread_set_nice (int);
 * 2. int thread_get_nice (void);
 * 3. int thread_get_load_avg (void);
 * 4. thread_get_recent_cpu (void);
 */

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
	// 현재 스레드의 nice 값을 새 값으로 설정
  enum intr_level old_level = intr_disable ();
  thread_current () -> nice = nice;
  mlfqs_calculate_priority (thread_current ());
  thread_test_preemption ();
  intr_set_level (old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	// 현재 스레드의 nice 값을 반환
  enum intr_level old_level = intr_disable ();
  int nice = thread_current () -> nice;
  intr_set_level (old_level);
  return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	// 현재 시스템의 load_avg * 100 값을 반환
  enum intr_level old_level = intr_disable ();
	// load avg의 getter는 pintos document의 지시대로 100을 곱한 후 정수형으로 만들고 반올림한다.
  int load_avg_value = fp_to_int_round (mult_mixed (load_avg, 100));
  intr_set_level (old_level);
  return load_avg_value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	// 현재 스레드의 recent_cpu * 100 값을 반환
  enum intr_level old_level = intr_disable ();
	// recent cpu의 getter는 pintos document의 지시대로 100을 곱한 후 정수형으로 만들고 반올림한다.
  int recent_cpu = fp_to_int_round (mult_mixed (thread_current () -> recent_cpu, 100));
  intr_set_level (old_level);
  return recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	/**
	 * idle thread는 한 번 schedule을 받고, 바로 sema_up을 하여 thread_start()의 마지막 sema_down을 풀어준다.
	 * thread_start가 작업을 끝내고 run_action()이 실행될 수 있도록 해주고, idle 자신은 block 된다.
	 * idle thread는 pintos에서 실행 가능한 thread가 하나도 없을 때, wake 되어 다시 작동하는데,
	 * 이는 CPU가 무조건 하나의 thread 는 실행하고 있는 상태를 만들기 위함이다.
	 * => 아마 껐다 키는데 소모되는 자원보다 하나를 실행하고 있는 상태에서 소모되는 자원이 더 적기 때문일듯?
	 */
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
/**
 * thread_func *function은 이 kernel이 실행할 함수를 가리킨다.
 * void *aux는 보조 파라미터로, synchronization을 위한 semaphore 등이 들어온다.
 * 여기서 실행시키는 function은 이 thread가 종료될 때까지 실행되는 main 함수이다.
 * 즉, 이 function은 idle thread라고 불리는 thread를 하나 실행시키는데,
 * 이 idle thread는 하나의 c 프로그램에서 하나의 main 함수 안에서 여러 함수의 호출이 이루어지는 것처럼,
 * pintos kernel위에서 여러 thread들이 동시에 실행될 수 있도록 하는 단 하나의 main thread인 셈이다.
 * pintos의 목적은, 이러한 idle thread 위에 여러 thread들이 동시에 실행될 수 있도록 만드는 것이다.
 */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);

	/** project1-Advanced Scheduler */
	if (thread_mlfqs) {
		mlfqs_calculate_priority (t);
		list_push_back(&all_list, &t->all_elem);
	} else {
		t->priority = priority;
	}

	t->wait_on_lock = NULL;
	list_init(&t->donations); // thread의 donations list를 초기화한다.

	t->magic = THREAD_MAGIC;

	/** #Advanced Scheduler */
	t->init_priority = t->priority;
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
	
	// t->exit_status = 0; // for system call

	/** 2
	 * file descriptor
	 */
	t->runn_file = NULL;
	/** 2
	 * hierarchical process structure
	 */
	list_init(&t->child_list);
	sema_init(&t->fork_sema, 0);
	sema_init(&t->exit_sema, 0);
	sema_init(&t->wait_sema, 0);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
	/**
	 * 반환값을 보면, !list_empty일 때는, list_pop_front (&ready_list)를 하고 있다.
	 * 즉, ready_list의 맨 앞 항목을 반환하는 round-robin 방식을 채택하고 있다는 것을 알 수 있다.
	 * 이러한 방식은 우선순위 없이 ready_list에 들어온 순서대로 실행하여 가장 간단하지만,
	 * 제대로 된 우선순위 스케쥴링이 이루어지고 있지 않다고 할 수 있다.
	 * 이를 유지시키면서 priority scheduling을 구현할 수 있도록 -> ready_list에 push()할 때, priority 순서에 맞추어 push 하도록 한다.
	 */
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread (); // 현재 실행중인 thread A를 반환한다.
	struct thread *next = next_thread_to_run (); // 다음에 실행될 thread B를 반환한다.

	ASSERT (intr_get_level () == INTR_OFF); // scheduling하는 동안에는 interrupt가 발생하면 안되기 때문에 INTR_OFF인지 확인한다.
	/** ASSERT (curr->status != THREAD_RUNNING); 
	 * thread A가 CPU 소유권을 thread B에게 넘겨주기 전에 running thread(A)는 그 상태를
	 * running 이외의 다른 상태로 바꾸어주는 작업이 선행되어 있어야 하고, 이를 확인하는 부분이다.
	*/
	ASSERT (curr->status != THREAD_RUNNING); 
	ASSERT (is_thread (next)); // next_thread_to_run()에 의해 올바른 thread가 return 되었는지 확인한다.
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

/** 1
 * 일어날 시간을 저장한 다음에 재워야 할 스레드를 sleep_list에 추가하고,
 * 스레드 상태를 block state로 만들어 준다.
 * CPU가 항상 실행 상태를 유지하게 하기 위해서 idle 스레드는 sleep되지 않아야 한다.
 */
void
thread_sleep (int64_t ticks)
{
  struct thread *cur;
  enum intr_level old_level;

  old_level = intr_disable ();	// 인터럽트 off 하고 old_level에는 off하기 이전의 interrupt state를 넣는다.
  cur = thread_current ();
  
  ASSERT (cur != idle_thread); // idle thread는 sleep 되어서는 안 된다.

  cur->wakeup_tick = ticks;			// 일어날 시간을 저장
  list_push_back (&sleep_list, &cur->elem);	// sleep_list 에 추가
  thread_block ();				// block 상태로 변경

  intr_set_level (old_level);	// 인터럽트 on
}

/**
 * timer_sleep() 함수가 호출되면 thread가 block state로 들어간다.
 * 이렇게 block된 thread들은 일어날 시간이 되었을 때 awake 되어야 한다.
 * 1. sleep_list()를 돌면서 일어날 시간이 지난 thread들을 찾아서 ready_list로 옮겨주고,
 * 2. thread state를 ready state로 변경시킨다.
 */
void
thread_awake (int64_t ticks)
{
  struct list_elem *e = list_begin (&sleep_list);

  while (e != list_end (&sleep_list)){
    struct thread *t = list_entry (e, struct thread, elem);
    if (t->wakeup_tick <= ticks){	// 스레드가 일어날 시간이 되었는지 확인
      e = list_remove (e);	// sleep list 에서 제거
      thread_unblock (t);	// 스레드 unblock
    }
    else 
      e = list_next (e);
  }
}

/** 1
 * list_insert_ordered() 함수는 list_insert() 를 통해 현재 받은 list_elem을 현재 비교대상인 e의 앞(prev)에 삽입(insert)한다.
 * ready_list에서 thread를 pop할 때, 가장 앞에서 꺼내기 때문에 ready_list의 가장 앞에는 priority가 가장 높은 thread가 와야 한다.
 * ready_list는 내림차순으로 정렬되어야 한다.
 * if (less (elem, e, aux))가 elem > e 인 순간에 break;를 실행해야 한다. (볼 것도 없이 그 뒤로는 모조리 elem보다 priority가 낮은 thread)
 * 따라서, less (elem, e, aux)는 elem > e 일 때 true를 반환하는 함수이다. 
 */
bool 
thread_compare_priority (struct list_elem *l, struct list_elem *s, void *aux UNUSED)
{
    return list_entry (l, struct thread, elem)->priority
         > list_entry (s, struct thread, elem)->priority;
}

/** 1
 * 현재 실행중인 running thread의 priority가 바뀌는 순간이 있다.
 * 이때 바뀐 priority of running thread가 ready_list의 가장 높은 priority보다 낮다면, pop해서 CPU 점유를 넘겨주어야 한다.
 * 현재 실행중인 running thread의 priority가 바뀌는 순간은 두 가지 경우이다.
 * 1. thread_create()
 * 2. thread_set_priority()
 * 두 가지 경우의 마지막에 running_thread와 ready_list의 가장 앞의 thread의 priority를 비교하는 코드를 넣어주어야 한다.
 */
void 
thread_test_preemption (void)
{
    if (!list_empty (&ready_list) && 
    thread_current ()->priority < 
    list_entry (list_front (&ready_list), struct thread, elem)->priority)
				if (intr_context()) // project 2 : panic 방지
					intr_yield_on_return();
        else
					thread_yield (); // CPU의 점유권을 넘겨준다.
}

/** 1
 * thread_compare_donate_priority 함수는 thread_compare_priority 의 역할을 donation_elem 에 대하여 하는 함수이다. 
 */
bool
thread_compare_donate_priority (const struct list_elem *l, 
				const struct list_elem *s, void *aux UNUSED)
{
	return list_entry (l, struct thread, donation_elem)->priority
		 > list_entry (s, struct thread, donation_elem)->priority;
}

/** 1
 * 자신의 priority를 필요한 lock을 점유하고 있는 스레드에게 빌려주는 함수이다.
 * nested donation을 해결하기 위해 하위에 연결된 모든 스레드에 대해서 donation이 일어나야 한다.
 */
void
donate_priority (void)
{
  int depth; // nested의 최대 깊이를 지정해주기 위해 사용한다. max_depth == 8 왜?
  struct thread *cur = thread_current ();

  for (depth = 0; depth < 8; depth++){
    if (!cur->wait_on_lock) break; // if wait_on_lock == NULL이면 더 이상 donation을 진행할 필요가 없다.
		/** 1
		 * cur->wait_on_lock이 NULL이면 요청한 해당 lock을 acquire()할 수 있다는 말이다.
		 * 그게 아니라면, 스레드가 lock에 걸려있다는 말이므로, 그 lock을 점유하고 있는 holder thread에게 priority를 넘겨주는 방식을
		 * 최대 깊이 8의 스레드까지 반복한다.
		 */
      struct thread *holder = cur->wait_on_lock->holder;
      holder->priority = cur->priority;
      cur = holder;
  }
}

/** 1
 * thread curr에서 lock_release(B)를 수행한다.
 * lock B를 사용하기 위해서 curr에 priority를 나누어 주었던 thread H는 priority를 빌려줘야 할 이유가 없다.
 * donations list에서 thread H를 지워주어야 한다.
 * 그 후, thread H가 빌려주었던 priority를 지우고, 다음 priority로 재설정(refresh)해야 한다.
 */
void
remove_with_lock (struct lock *lock)
{
  struct list_elem *e;
  struct thread *cur = thread_current ();

  for (e = list_begin (&cur->donations); e != list_end (&cur->donations); e = list_next (e)){
    struct thread *t = list_entry (e, struct thread, donation_elem);
    if (t->wait_on_lock == lock) // wait_on_lock이 이번에 release하는 lock이라면 해당 thread가 remove 대상이다.
      list_remove (&t->donation_elem); // donations list와 관련된 작업에서는 elem이 아니라, donation_elem을 사용해야 한다.
  }
}

/** 1
 * init_priority와 donations list의 max_priority중 더 높은 값으로 curr->priority를 설정한다.
 * 1. lock_release ()를 실행하였을 경우, curr thread의 priority를 재설정(refresh)해야 한다.
 * 2. thread_set_priority ()에서 활용한다.
 */
void
refresh_priority (void)
{
  struct thread *cur = thread_current ();

  cur->priority = cur->init_priority;
  
  if (!list_empty (&cur->donations)) { // list_empty()라면, cur->priority = cur->init_priority 하면 끝임.
    list_sort (&cur->donations, thread_compare_donate_priority, 0); // list_sort()는 priority가 가장 높은 thread를 고르기 위해 priority를 기준으로 내림차순 정렬한다. 

    struct thread *front = list_entry (list_front (&cur->donations), struct thread, donation_elem); // 맨 앞을 뽑는다.
    if (front->priority > cur->priority) // 만일, front->priority > cur->priority라면,
      cur->priority = front->priority; // priority donation을 수행한다.
  } 
}

/********** ********** ********** project 1 : advanced scheduler ********** ********** **********/
/********** ********** ********** project 1 : advanced scheduler ********** ********** **********/
/********** ********** ********** project 1 : advanced scheduler ********** ********** **********/
/** 1
 * 
 * 4BSD scheduler priority를 0 (PRI_MIN)부터 64(PRI_MAX)의 64개의 값으로 나눈다.
 * 각 priority 별로 ready queue가 존재하므로 64개의 ready queue가 존재하며,
 * priority 값이 커질수록 우선순위가 높아짐(먼저 실행됨)을 의미한다.
 * thread의 priority는 thread 생성 시에 초기화되고, 4 ticks의 시간이 흐를 때마다 모든 thread의 priority가 재계산된다.
 * priority의 값을 계산하는 식은 아래와 같다.
 * priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
 * 
 * recent_cpu 값은 이 thread가 최근에 cpu를 얼마나 사용하였는지를 나타내는 값으로, thread가 최근에 사용한 cpu 양이 많을수록 큰 값을 가진다.
 * 이는 오래된 thread 일수록 우선순위를 높게 가져서(recent_cpu 값이 작아짐) 모든 thread들이 골고루 실행될 수 있게 한다.
 * priority는 정수값을 가져야 하므로, 계산 시 소수점은 버림한다.
 */

/**
 * mlfqs_calculate_priority () 함수는 특정 thread의 priority를 계산하는 함수이다.
 * idle_thread의 priority는 고정이므로 제외하고, fixed_point.h 에서 만든 fp 연산 함수를 사용하여 priority를 구한다.
 * 계산 결과의 소수부분은 버림하고 정수의 priority로 설정한다.
 */
void
mlfqs_calculate_priority (struct thread *t)
{
  if (t == idle_thread) 
    return ;
  t->priority = fp_to_int (add_mixed (div_mixed (t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}

void
mlfqs_calculate_recent_cpu (struct thread *t)
{
  if (t == idle_thread)
    return ;
  t->recent_cpu = add_mixed (mult_fp (div_fp (mult_mixed (load_avg, 2), add_mixed (mult_mixed (load_avg, 2), 1)), t->recent_cpu), t->nice);
}

/** 1
 * load_avg 값을 계산하는 함수이다.
 * load_avg 값은 thread 고유의 값이 아니라 system wide한 값이기 때문에, idle_thread가 실행되는 경우에도 계산한다.
 * ready_threads는 현재 시점에서 실행 가능한 thread의 수를 나타내므로,
 * ready_list에 들어 있는 thread의 숫자에 현재 running thread 1개를 더한다.
 * (이때, idle thread는 실행 가능한 thread에 포함시키지 않는다.)
 */
void 
mlfqs_calculate_load_avg (void) 
{
  int ready_threads;
  
  if (thread_current () == idle_thread)
    ready_threads = list_size (&ready_list);
  else
    ready_threads = list_size (&ready_list) + 1;

  load_avg = add_fp (mult_fp (div_fp (int_to_fp (59), int_to_fp (60)), load_avg), 
                     mult_mixed (div_fp (int_to_fp (1), int_to_fp (60)), ready_threads));
}

/** 1
 * 1. 1 tick 마다 running thread의 recent_cpu 값 + 1
 * 2. 4 ticks 마다 모든 thread의 priority 재계산
 * 3. 1초 마다 모든 thread의 recent_cpu 값과 load_avg 값 재계산
 */
void
mlfqs_increment_recent_cpu (void)
{
  if (thread_current () != idle_thread)
    thread_current ()->recent_cpu = add_mixed (thread_current ()->recent_cpu, 1);
}

void
mlfqs_recalculate_recent_cpu (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, all_elem);
    mlfqs_calculate_recent_cpu (t);
  }
}

void
mlfqs_recalculate_priority (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, all_elem);
    mlfqs_calculate_priority (t);
  }
}