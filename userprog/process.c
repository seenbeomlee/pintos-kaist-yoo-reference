#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/** 2
	 * 	Project2: for Test Case - 직접 프로그램을 실행할 때에는 이 함수를 사용하지 않지만 make check에서
	 *  이 함수를 통해 process_create를 실행하기 때문에 이 부분을 수정해주지 않으면 Test Case의 Thread_name이
	 *  커맨드 라인 전체로 바뀌게 되어 Pass할 수 없다.
	 */
	char *ptr;
	strtok_r(file_name, " ", &ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
		return true;

	/* 2. Resolve VA from the parent's page map level 4. */
    parent_page = pml4_get_page(parent->pml4, va);
    if (parent_page == NULL)
			return false;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	 newpage = palloc_get_page(PAL_ZERO);
    if (newpage == NULL)
			return false;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
    writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
    if_.R.rax = 0;  // 자식 프로세스의 return값 (0)

	/* 2. Duplicate PT */
	current->pml4 = pml4_create(); // 부모의 pte를 복사하기 위해서 페이지 테이블을 생성한다. 이때 current는 자식 프로세스가 됨?
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent)) // duplicate_pte == 페이지 테이블을 복사하는 함수(부모 -> 자식)
		goto error;
#endif

/* TODO: Your code goes here.
	* TODO: Hint) To duplicate the file object, use `file_duplicate`
	* TODO:       in include/filesys/file.h. Note that parent should not return
	* TODO:       from the fork() until this function successfully duplicates
	* TODO:       the resources of parent.*/

	if (parent->fd_idx >= FDCOUNT_LIMIT)
		goto error;

	struct file *file;

	// 부모의 fdt를 자식의 fdt로 복사한다.
	for (int fd = 0; fd < FDCOUNT_LIMIT; fd++) {
		file = parent->fdt[fd];
		if (file == NULL) // fd 엔트리가 없는 상태에는 그냥 건너뛴다.
			continue;

		if (file > STDERR)
			current->fdt[fd] = file_duplicate(file);
		else
			current->fdt[fd] = file;
}

	current->fd_idx = parent->fd_idx; // 부모의 next_fd를 자식의 next_fd로 옮겨준다.
	sema_up(&current->fork_sema); // fork()가 정상적으로 완료되었으므로, 현재 wait 중인 parent를 다시 실행 가능 상태로 만든다.

	process_init();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);  // 정상 종료 시 자식 Process를 수행하러 감

error: // 제대로 복제가 안된 상태라면, TID_ERROR를 리턴한다.
    sema_up(&current->fork_sema);  // 복제에 실패했으므로 현재 fork용 sema unblock
    exit(TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
/** 2
 * 유저가 입력한 명령어를 수행할 수 있도록, 프로그램을 메모리에 적재하고 실행하는 함수이다.
 * filename을 f_name이라는 인자로 받아서 file_name에 저장한다.
 * 초기에 file_name은 실행 프로그램 파일명과 옵션이 분리되지 않은 상황(통 문자열)이다.
 * thread의 이름을 실행 파일명으로 저장하기 위해 실행 프로그램 파일명만 분리하기 위해 parsing해야 한다.
 * 실행파일명은 cmd line 안에서 첫번째 공백 전의 단어에 해당한다.
 * 다른 인자들 역시 프로세스를 실행하는데 필요하므로, 함께 user stack에 담아줘야한다.
 * arg_list라는 배열을 만들어서, 각 인자의 char*을 담아준다.
 * 실행 프로그램 파일명은 arg_list[0]에 들어간다.
 * 2번째 인자 이후로는 arg_list[i]에 들어간다.
 * load ()가 성공적으로 이루어졌을 때, argument_stack 함수를 이용하여, user stack에 인자들을 저장한다.
 */
int
process_exec (void *f_name) { 
// 유저가 입력한 명령어를 수행하도록 프로그램을 메모리에 적재하고 실행하는 함수. 
// 여기에 파일 네임 인자로 받아서 저장(문자열) => 근데 실행 프로그램 파일과 옵션이 분리되지 않은 상황.
	char *file_name = f_name; // f_name은 문자열인데 위에서 (void *)로 넘겨받음! -> 문자열로 인식하기 위해서 char* 로 변환해줘야한다.
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	/** 2
	 * _if에는 intr_frame 내 구조체 멤버에 필요한 정보를 담는다. 
	 * 여기서 intr_frame은 인터럽트 스택 프레임이다. 
	 * 즉, 인터럽트 프레임은 인터럽트와 같은 요청이 들어와서 기존까지 실행 중이던 context(레지스터 값 포함)를 스택에 저장하기 위한 구조체이다!
	 */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();
// 새로운 실행 파일을 현재 스레드에 담기 전에 먼저 현재 process에 담긴 context를 지워준다.
// 지운다? => 현재 프로세스에 할당된 page directory를 지운다는 뜻.

/** project2-Command Line Parsing */
	char *ptr, *arg;
	int argc = 0;
	char *argv[64];
// 파싱을 통해 제대로 된 파일 이름을 구하도록 정정한다.
  for (arg = strtok_r(file_name, " ", &ptr); arg != NULL; arg = strtok_r(NULL, " ", &ptr))
		argv[argc++] = arg;

/* And then load the binary */
// load() 함수는 실행할 프로그램의 binary 파일을 메모리에 올리는 역할을 한다.
	success = load (file_name, &_if);
// file_name, _if를 현재 프로세스에 load 한다.
// success는 bool type이니까 load에 성공하면 1, 실패하면 0 반환.
// 이때 file_name: f_name의 첫 문자열을 parsing하여 넘겨줘야 한다!

	/* If load failed, quit. */
	if (!success)
		return -1;

	/** project2-Command Line Parsing */
	argument_stack(argv, argc, &_if);

	/** 2
	 * 어라, 근데 page를 할당해준 적이 없는데 왜 free를 하는 거지? 
	 * => palloc()은 load() 함수 내에서 file_name을 메모리에 올리는 과정에서 page allocation을 해준다. 
	 * 이때, 페이지를 할당해주는 걸 임시로 해주는 것.
	 * file_name: 프로그램 파일 받기 위해 만든 임시변수. 따라서 load 끝나면 메모리 반환.
	 */
	palloc_free_page (file_name);

	// /** 2
	//  * argument parsing test를 위한 무한 루프 추가
	//  * 결과를 확인하기 위해 hex_dump()함수를 사용한다.
	//  * 이 함수는 메모리의 내용을 16진수 형식으로 출겨해서 stack에 저장된 값을 확인할 수 있다.
	//  */
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true); // 0x47480000	

	/* Start switched process. */
	do_iret (&_if); // 만약 load가 실행됐다면 context switching을 진행한다.
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	// /** 2
	//  * argument parsing test를 위한 무한 루프 추가
	//  * 핀토스는 user process를 생성한 후 프로세스 종료를 대기해야 하는데, 자식 프로세스가 종료될 때까지 대기한다.
	//  * 현재는 process_wait()를 구현하지 않았으므로, 일단 무한 대기하도록 임의 설정한다.
	//  */
	// while (1) {}
	// return -1;
	struct thread *child = get_child_process(child_tid);
	if (child == NULL) // 해당 자식이 존재하지 않는다면, -1을 반환한다.
		return -1;

/** 2
 * 해당 자식 프로세스가 성공적으로 종료될 때까지 부모 프로세스는 대기한다.
 * init_thread()에서 부모의 wait_sema 값을 '0'으로 초기화하였으므로, 부모 프로세스는 wait_list에 들어가게 된다.
 * 이후, 자식 프로세스가 종료(process_ext()) 된 후, sema_up을 한 뒤에서야 해당 부모 프로세스가 ready 상태로 전환된다.
 */
	sema_down(&child->wait_sema);  // 자식 프로세스가 종료될 때 까지 대기한다. (process_exit에서 자식이 종료될 때 sema_up 해줄 것이다.)
	
	// context switching 발생

	// 자식으로부터 종료인자를 전달받고, 리스트에서 삭제한다.
	int exit_status = child->exit_status;
	list_remove(&child->child_elem); // 자식이 종료됨을 알리는 'wait_sema' signal을 받으면 현재 스레드(부모)의 자식 리스트에서 제거한다.
	sema_up(&child->exit_sema);  // 자식 프로세스가 완전히 종료되고 스케줄링이 이어질 수 있도록 자식에게 signal을 보낸다.

	return exit_status; // 자식의 exit_status를 반환한다.
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	for (int fd = 0; fd < curr->fd_idx; fd++)  // FDT 비우기
		close(fd);

/** 2
 * 현재 프로세스가 실행중인 파일을 종료한다.
 * load() 함수에서 file_close하지 않고, process_exit()에서 닫는다.
 */
	file_close(curr->runn_file); 

	palloc_free_multiple(curr->fdt, FDT_PAGES); // 파일 디스크립터 동적 할당 제거

	process_cleanup();

	sema_up(&curr->wait_sema);  // 자식 프로세스가 종료될 때까지 대기하는 부모에게 자식 프로세스가 정상적으로 종료되었다고 signal

	sema_down(&curr->exit_sema);  // 이후에는 부모 프로세스가 종료될 떄까지 대기한다.

	// process_cleanup ();
}

struct thread 
*get_child_process(int pid) 
{
	struct thread *curr = thread_current(); // struct thread *thread_current(void) == 현재 프로세스의 디스크립터를 반환한다.
	struct thread *t;

	// 자식 리스트를 순회하면서 pid를 검색한다.
	for (struct list_elem *e = list_begin(&curr->child_list); e != list_end(&curr->child_list); e = list_next(e)) {
		t = list_entry(e, struct thread, child_elem);

		if (pid == t->tid) // 해당 pid를 갖는 자식 프로세스가 child_list에 존재하면 해당 thread를 리턴한다.
			return t;
	}

	return NULL; // 자식 리스트에 해당 pid를 갖는 프로세스가 존재하지 않으면 NULL을 리턴한다.
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	// 각 프로세스가 실행이 될 때, 각 프로세스에 해당하는 VM(virtual memory)이 만들어져야 하므로,
	// 이를 위해 페이지 테이블 엔트리를 생성하는 과정이 우선된다.
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	// 스레드가 삭제될 때 파일을 닫을 수 있도록 스레드 구조체에 파일을 저장해둔다.
	t->runn_file = file; 
	/** 2
	 * 열려있는 파일에 쓰기를 방지한다.
	 * User Programs 5번째 과제(Deny Write on Executables)이다.
	 * 이 부분을 구현하면 rox 관련 테스트들을 통과할 수 있다. (rox : Read Only for eXecutables. )
	 * 
	 * 1. file_allow_write ()를 파일 안에서 호출하면 다시 쓰기가 가능해지도록 만들 수 있다.
	 * 2. 파일을 닫아도 다시 쓰기가 가능해지게 된다. 그러므로, 프로세스의 실행 파일에 쓰기를 계속 거부하려면
	 * 		프로세스가 돌아가는 동안에는 실행 파일이 쭉- 열려 있게끔 해야 한다.
	 */
	file_deny_write(file);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
// 그 뒤, 파일을 실제로 VM에 올리는 과정이 진행된다. 
// 파일이 제대로 된 ELF 인지 검사하는 과정이 동반되며, 
// 세그먼트 단위로 PT_LOAD의 헤더 타입을 가진 부분을 하나씩 메모리로 올리는 작업을 진행한다.
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	// 전부 메모리로 올린 뒤에 스택을 만드는 과정이 실행된다.
	if (!setup_stack (if_)) // user stack 초기화
		goto done;

	/* Start address. */
	// 어떤 명령부터 실행되는지를 가리키는, 즉 entry point 역할의 rip를 설정하고, 열었던 실행 파일을 닫는 것으로 load()가 끝난다.
	// rip == 프로그램  카운터(실행할 다음 인스트럭션의 메모리 주소)
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	/** 2
	 * 현재 load() 함수에서는 로드가 완료되면 파일을 바로 close하는데, 
	 * 여기서 close하지 않고 thread가 삭제될 때 파일을 닫도록 변경해야 한다.
	 * 왜냐하면, 파일을 닫으면 쓰기 작업이 다시 허용되기 때문이다.
	 * 따라서, 프로세스의 실행 파일에 대한 쓰기를 거부하려면 해당 파일을 프로세스가 실행 중인 동안 계속 열어두어야 한다.
	 * 파일은 여기서 닫지 않고, thread가 삭제될 때 process_exit()에서 닫는다.
	 */
	// file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

/** project2-Command Line Parsing */
// 유저 스택에 파싱된 토큰을 저장하는 함수
void argument_stack(char **argv, int argc, struct intr_frame *if_) {
	char *arg_addr[100]; // 문자열 주소를 저장하는 배열
	int argv_len;

	for (int i = argc - 1; i >= 0; i--) { // argv 배열을 역순으로 stack에 넣는다.
	/** 2
	 * 문자열의 길이에 1을 더한 만큼의 공간을 할당하여 문자열을 복사한다.
	 * strlen은 NULL(sentinel)을 읽지 않으니, +1을 해주는 것이다.
	 */
		argv_len = strlen(argv[i]) + 1;
		if_->rsp -= argv_len;
		memcpy(if_->rsp, argv[i], argv_len); // 문자열의 길이에 1을 더한 만큼의 공간을 할당하여 문자열을 복사한다.
		arg_addr[i] = if_->rsp;
	}

	while (if_->rsp % 8) // stack을 8 바이트로 정렬한다.
		*(uint8_t *)(--if_->rsp) = 0;

	if_->rsp -= 8;
	memset(if_->rsp, 0, sizeof(char *));

	for (int i = argc - 1; i >= 0; i--) { // arg_addr 배열의 주소를 stack에 넣는다.
		if_->rsp -= 8;
		memcpy(if_->rsp, &arg_addr[i], sizeof(char *));
	}
	// fake return address를 설정한다.
	if_->rsp = if_->rsp - 8;
	memset(if_->rsp, 0, sizeof(void *)); // 마지막으로 NULL 포인터를 넣어 인자들의 끝을 표시한다.

	if_->R.rdi = argc; // &rdi 레지스터에는 인자의 개수가 저장된다.
	if_->R.rsi = if_->rsp + 8; // %rsi 레지스터에는 인자들의 시작 주소가 저장된다.
}

/** 2
 * 현재 thread fdt에 file을 추가한다.
 * 파일이 추가된 위치의 index를 반환한다.
 * 
 * 보통은 여기를 while문으로 돌아야 중간에 삭제된 위치(NULL)에 파일을 add할 것 같은데,
 * 이렇게 index만으로 추가해도 pass가 되는 것이 정상인지 모르겠다.
 */
int process_add_file(struct file *f) {
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;

	if (curr->fd_idx >= FDCOUNT_LIMIT) // fdt가 꽉 찼을 경우에는 error를 return한다.
		return -1;

	fdt[curr->fd_idx++] = f;

	return curr->fd_idx - 1;
}

/** 2
 * 현재 thread의 fd번째 파일 정보 얻기
 */
struct file *process_get_file(int fd) {
	struct thread *curr = thread_current();

	if (fd >= FDCOUNT_LIMIT) // fd < STDIN 체크?
		return NULL;

	return curr->fdt[fd];
}

/** 2
 * 현재 thread의 fdt에서 파일 삭제
 * 삭제된 곳은 NULL로 초기화해야한다.
 * 왜냐하면, 향후 재할당되는데 내용물이 채워져 있으면 보안상의 문제가 존재하기 때문이다.
 */
int process_close_file(int fd) {
	struct thread *curr = thread_current();

	if (fd >= FDCOUNT_LIMIT) // fd < STDIN 체크?
		return -1;

	curr->fdt[fd] = NULL;
	return 0;
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	struct thread *curr = thread_current();

/** 2
 * parent -> if는 사용할 수 없다.
 * fork()를 하면 자식은 부모의 if를 물려받아야 하는데, 
 * 지금은 fork()를 하면서 context switch가 일어난 상태로, 현재 부모의 if에는 커널이 작업하던 정보가 저장되어 있다.
 * 하지만, 자식에게 물려줘야 하는 tf는 커널이 작업하던 정보가 아니라, user-level에서 부모 프로세스가 작업하던 정보를 물려줘야 한다.
 */
	struct intr_frame *f = (pg_round_up(rrsp()) - sizeof(struct intr_frame));  // 현재 쓰레드의 if_는 페이지 마지막에 붙어있다.
/** 2
 * 전달받은 intr_frame을 parent_if 필드에 복사한다.
 * 1. __do_fork()에서 자식 프로세스에 부모의 context 정보를 복사하기 위함이다.
 * 2. 부모의 intr_frame을 찾는 용도로 활용한다.
 */
	memcpy(&curr->parent_if, f, sizeof(struct intr_frame));                   

	/** 2
	 * 현재 스레드를 새 스레드로 복제합니다.
	 * __do_fork()를 실행하는 Thread를 생성한다.
	 * 현재 스레드 curr를 인자로 넘겨준다. */
	tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, curr);

	if (tid == TID_ERROR)
		return TID_ERROR;

	struct thread *child = get_child_process(tid); // 자식이 load될 때까지 대기하기 위해서 방금 생성한 자식 thread를 찾는다.

/** 2
 * 생성만 해놓고 자식 프로세스가 __do_fork에서 fork_sema를 sema_up 해줄 때까지 대기한다.
 * 왜냐하면, 부모 프로세스는 자식 프로세스가 성공적으로 복제되었는지 여부를 알 때까지 fork()에서 반환해서는 안된다.
 * init_thread()에서 fork_sema의 값을 '0'으로 초기화 해두었으므로, 부모 프로세스는 wait_list로 들어간다.
 * 이후 자식 프로세스가 로드된 후, sema_up을 한 뒤에서야 부모 프로세스가 Ready 상태로 전환된다.
 * 실행중인 프로세스가 자식 프로세스로 전환되었으므로, 이제야 위의 __do_fork()를 실행한다.
 */
	sema_down(&child->fork_sema); 

	if (child->exit_status == TID_ERROR) // 자식 프로세스가 리소스를 복제하지 못했을 경우, 부모의 fork() 호출은 TID_ERROR를 반환한다.
		return TID_ERROR;

// 부모 프로세스의 리턴값 : 생성한 자식 프로세스의 tid
// 자식 프로세스의 리턴값 : 0
	return tid;  
}

/** 2
 * extend file descriptor
 */
int process_insert_file(int fd, struct file *f) {
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;

	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return -1;

	if (f > STDERR)
		f->dup_count++;

	fdt[fd] = f;

	return fd;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

/** 3
 * anonymous page
 * 이 함수는 실행 파일의 내용을 페이지로 로딩하는 함수이며, 첫 번째 page fault가 발생할 때 호출된다.
 * 이 함수가 호출되기 이전에 물리 프레임 매핑이 진행되므로, 여기서는 물리 프레임에 내용을 로딩하는 작업만 하면 된다.
 * 
 * 이 함수는 page 구조체와 aux를 인자로 받는다.
 * aux는 load_segment에서 로딩을 위해 설정해둔 정보인 vm_load_arg 이다.
 * aux를 사용하여 읽어올 파일을 찾아서 메모리에 로딩한다.
 */
static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
		struct vm_load_arg *aux_p = aux;
    struct file *file = aux_p->file;
    off_t offset = aux_p->ofs;
    size_t page_read_bytes = aux_p->read_bytes;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// 1) 파일의 position을 ofs로 지정한다.
    file_seek(file, offset);               
		// 2) 파일을 read_bytes만큼 물리 프레임에 읽어들인다.                                              
    if (file_read(file, page->frame->kva, page_read_bytes) != (off_t)page_read_bytes) {  
			palloc_free_page(page->frame->kva);                                            
			return false;
    }
		// 3) 다 읽은 지점부터 zero_bytes만큼 0으로 채운다.
    memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);  

    return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
/** 3
 * anonymous page
 * 이 함수는 프로세스가 실행될 때 실행 파일을 현재 스레드로 로드하는 함수는 load() 에서 호출된다.
 * 파일의 내용을 upage에 로드하는 함수이다.
 * 파일의 내용을 로드하기 위해서 upage를 할당할 page가 필요한데, vm_alloc_page_with_initializer()를 호출해서 page를 생성한다.
 * 
 * Lazy loading을 사용해야하므로, 여기서 바로 파일의 내용을 로드하지 않아야 한다.
 * 그래서 page에 내용을 로드할 때 사용할 함수와 필요한 인자들을 넣어줘야 한다.
 * vm_alloc_page_with_initializer의 네 번째(lazy_load_segment), 다섯 번째(aux) 인자가 각각 로드할 때 사용할 함수와 인자에 해당한다.
 * 
 * 내용을 로드할 때 사용할 함수는 lazy_load_segment를 사용하고, 인자는 직접 만들어서 넘겨주어야 한다.
 * lazy_load_segment에서 필요로 하는 인자는 아래와 같다.
 * 1. file == 내용이 담긴 파일 객체
 * 2. ofs == 이 페이지에서 읽기 시작할 위치
 * 3. read_bytes == 이 페이지에서 읽어야 하는 바이트 수
 * 4. zero_bytes == 이 페이지에서 read_bytes 만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수
 * 
 * vm_load_arg 라는 구조체를 새로 정의해서 위 정보들을 담아, vm_alloc_page_with_initializer의 마지막 인자(aux)로 전달한다.
 * 이제 page fault가 처음 발생했을 때, lazy_load_segment가 실행되고, vm_load_arg 인자로 사용되어 내용이 로딩될 것이다.
 */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0); // read_bytes + zero_bytes가 페이지 크기(PGSIZE)의 배수인지 확인
	ASSERT (pg_ofs (upage) == 0); // upage가 페이지 정렬되어 있는지 확인
	ASSERT (ofs % PGSIZE == 0); // ofs가 페이지 정렬되어 있는지 확인

	while (read_bytes > 0 || zero_bytes > 0) { // read_bytes와 zero_bytes가 0보다 큰 동안 루프를 실행한다.
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		// 이 페이지를 채우는 방법을 계산한다.
		// 파일에서 PAGE_READ_BYTES 만큼 읽고 나머지 PAGE_ZERO_BYTES 만큼 0으로 채운다.
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE; // 최대로 읽을 수 있는 크기는 PGSIZE
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		// void *aux = NULL;
		/** Project 3-Anonymous Page
		 * vm_alloc_page_wtih_initializer에 제공할 aux 인수로 필요한 보조 값들을 설정해야 한다.
		 * loading을 위해 필요한 정보를 포함하는 구조체를 만든다.
		 */
		struct vm_load_arg *aux = (struct vm_load_arg *)malloc(sizeof(struct vm_load_arg));
		aux->file = file; // 내용이 담긴 파일 객체
		aux->ofs = ofs; // 이 페이지에서 읽기 시작할 위치
		aux->read_bytes = page_read_bytes; // 이 페이지에서 읽어야 하는 바이트 수
		// auz->zero_bytes = page_zero_bytes // 이 페이지에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수

		// upage를 할당하기 위해서 vm_alloc_page_with_initializer()를 호출하여 page를 생성한다.
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		// 다음 반복을 위해 읽어들인 만큼 값을 갱신한다.
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes; // 읽어들인 만큼 ofs 이동시킨다.
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/** 3
 * anonymous page
 * 프로세스가 실행될 때 load() 함수 안에서 호출되며, STACK의 페이지를 생성하는 함수이다.
 * 스택은 아래로 성장하므로, 스택의 시작점인 USER_STACK에서 PGSIZE만큼 아래로 내린 지점(stack_bottom)에서 페이지를 생성한다.
 * 첫 번째 스택 페이지는 lazy loading을 할 필요가 없다.
 * 프로세스가 실행될 때 이 함수가 불리고 나서 command line arguments를 스택에 추가하기 위해 이 주소에 바로 접근하기 때문이다.
 * 따라서, 이 주소에서 fault가 발생할 때까지 기다릴 필요없이 바로 물리 프레임을 할당시켜 놓는다.
 * 페이지를 할당받을 때 이 페이지가 스택의 페이지임을 표시하기 위해 보조 마커를 사용한다. (VM_MARKER_0)를 사용한다.
 * => 스택에서 할당받는 페이지에 이 마커로 표시해두면, 나중에 이 페이지가 스택의 페이지인지 아닌지 마커를 통해서 알 수 있다.
 * 마지막으로, rsp값을 USER_STACK으로 변경해준다. 이제, argument_stack 함수에서 이 위치(rsp)부터 인자를 push하게 된다.
 */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	// 스택은 아래로 성장하므로, USER_STACK에서 PGSIZE만큼 아래로 내린 지점에서 페이지를 생성한다.
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	/** stack_bottom에 스택을 매핑하고 페이지를 즉시 요청하세요.
	 * 성공하면, rsp를 그에 맞게 설정하세요.
	 * 페이지가 스택임을 표시해야 합니다.
	 * 
	 * 1) stack_bottom에 페이지를 하나 할당받는다.
	 * VM_MAERKER_0 == 스택이 저장된 메모리 페이지임을 식별하기 위해 추가
	 * 1 == writable은 argument_stack()에서 값을 넣어야 하니 true로 설정한다.
	 */
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1)) { 
		// 2) 할당받은 페이지에 바로 물리 프레임을 매핑한다.
		success = vm_claim_page(stack_bottom);
		if (success) {
			// 3) rsp를 변경한다. (argument_stack에서 이 위치부터 인자를 push 한다.)
			if_->rsp = USER_STACK;
			thread_current()->stack_bottom = stack_bottom;
		}
	}
	return success;
}
#endif /* VM */
