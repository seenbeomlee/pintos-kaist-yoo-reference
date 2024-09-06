#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
/** 2
 * 시스템 콜을 호출할 때, 원하는 기능에 해당하는 시스템 콜 번호를 rax에 담는다.
 * 그리고 시스템 콜 핸들러는 rax의 숫자로 시스템 콜을 호출하고, -> 이는 enum으로 선언되어있다.
 * 해달 콜의 반환값을 다시 rax에 담아서 intr_frame(인터럽트 프레임)에 저장한다.
 */
void // void 형식에 return을 추가해야 한다. (디버깅하다 발견한 사실이라 함.)
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
int sys_number = f->R.rax;
	// Argument 순서
	// %rdi %rsi %rdx %r10 %r8 %r9
	switch (sys_number) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = process_wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
	}
}

/** 2
 * 주소 값이 user 영역에서 사용하는 주소 값인지 확인한다.
 * user 영역을 벗어난 영역일 경우, process를 종료한다. (exit (-1))
 */
void 
check_address (void *addr) {
	if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1);
}

void halt (void) {
 power_off(); // 핀토스를 종료시키는 시스템 콜이다.
}

/** 2
 * 현재 프로세스를 종료시키는 시스템 콜이다.
 * 종료시, 프로세스 이름 : 'exit(status)'를 출력한다.
 * 정상적으로 종료시 status는 0이 된다.
 * status : 프로그램이 정상적으로 종료되었는지 확인한다.
 */
void exit( int status) {
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, t->exit_status); // Process Termination Message
	thread_exit();
}

/** 2
 * 파일을 생성하는 시스템 콜이다.
 * 생성할 파일의 이름과 만들 파일의 사이즈를 파라미터로 받고,
 * 디스크에 해당 이름으로 파일을 만드는 시스템 콜이다.
 * 
 * 성공할 경우 true, 실패할 경우 false를 리턴한다.
 * file : 생성할 파일의 이름 및 경로 정보
 * initial_size : 생성할 파일의 크기
 */
bool create (const char *file, unsigned initial_size) {
	check_address(file);
  return filesys_create(file, initial_size);
}

/** 2
 * 파일을 삭제하는 시스템 콜이다.
 * 지울 파일의 이름을 파라미터로 받고,
 * 디스크에서 해당 이름과 같은 이름을 가진 파일을 지우는 시스템 콜이다.
 * 
 * file : 제거할 파일의 이름 및 경로 정보
 * 성공할 경우 true, 실패할 경우 false를 리턴한다.
 */
bool remove (const char *file) {
	check_address(file);
	return filesys_remove(file);
}