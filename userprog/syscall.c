#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/mmu.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/process.h"

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

	// read & write 용 lock 초기화
	lock_init(&filesys_lock);
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
		case SYS_DUP2:
			f->R.rax = dup2(f->R.rdi, f->R.rsi);
			break;
		default:
			exit(-1);
	}
}

#ifndef VM
/** 2
 * 주소 값이 user 영역에서 사용하는 주소 값인지 확인한다.
 * user 영역을 벗어난 영역일 경우, process를 종료한다. (exit (-1))
 * pintos에서는 시스템 콜이 접근할 수 있는 주소를 0cx0000000 ~ 0x8048000(== KERN_BASE) 으로 제한한다. (이 이상은 커널 영역이다.)
 * 유저 영역을 벗어난 영역일 경우, 비정상 접근이라고 판단하여 exit(-1)로 프로세스를 종료한다.
 */
void check_address (void *addr) {
	/** 2
	 * 1. null 포인터,
	 * 2. 매핑되지 않은 가상 메모리를 가리키는 포인터,
	 * 3. 커널 가상 주소 공간(KERN_BASE 이상)을 요청하는 경우 거부해야 한다.
	 */
    thread_t *curr = thread_current();

    if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(curr->pml4, addr) == NULL) {
			exit(-1);
		}
}
#else
/** 3
 * system call 수정
*/
struct page *check_address(void *addr) {
	struct thread *curr = thread_current();

	if (is_kernel_vaddr(addr) || addr == NULL || !spt_find_page(&curr->spt, addr)) {
		exit(-1);
	}

	return spt_find_page(&curr->spt, addr);
}
#endif

void halt (void) {
 power_off(); // 핀토스를 종료시키는 시스템 콜이다.
}

void exit (int status) {
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

int open(const char *file) {
	check_address(file);
	struct file *newfile = filesys_open(file);

	if (newfile == NULL)
		return -1;

	int fd = process_add_file(newfile);

	// fd table이 가득 찼다면,
	if (fd == -1)
		file_close(newfile);

	return fd;
}

int filesize(int fd) {
	struct file *file = process_get_file(fd);

	if (file == NULL)
		return -1;

	return file_length(file);
}

int read(int fd, void *buffer, unsigned length) {
	struct thread *curr = thread_current();
	check_address(buffer);

	struct file *file = process_get_file(fd);

	if (file == STDIN) { 
		int i = 0; 
		char c;
		unsigned char *buf = buffer;

		for (; i < length; i++) {
			c = input_getc();
			*buf++ = c;
			if (c == '\0')
				break;
		}
		return i;
	}

	if (file == NULL || file == STDOUT || file == STDERR)  // 빈 파일, stdout, stderr를 읽으려고 할 경우
		return -1;

	// if (fd < 0) // 음의 인덱스를 읽으려고 할 경우
	// 	return -1;

	off_t bytes = -1;

	lock_acquire(&filesys_lock);
	bytes = file_read(file, buffer, length);
	lock_release(&filesys_lock);

	return bytes;
}

int write(int fd, const void *buffer, unsigned length) {
	check_address(buffer);

	struct thread *curr = thread_current();
	off_t bytes = -1;

	struct file *file = process_get_file(fd);

	if (file == STDIN || file == NULL || fd < 0)  
		return -1;

	if (file == STDOUT) { 
		putbuf(buffer, length);
		return length;
	}

	if (file == STDERR) { 
		putbuf(buffer, length);
		return length;
	}

	lock_acquire(&filesys_lock);
	bytes = file_write(file, buffer, length);
	lock_release(&filesys_lock);

	return bytes;
}

void seek(int fd, unsigned position) {
	struct file *file = process_get_file(fd);

	if (fd < 3 || file == NULL)
		return;

	file_seek(file, position);
}

int tell(int fd) {
	struct file *file = process_get_file(fd);

	if (fd < 3 || file == NULL)
		return -1;

	return file_tell(file);
}

void close(int fd) {
	struct thread *curr = thread_current();
	struct file *file = process_get_file(fd);

	if (file == NULL)
		return;

	process_close_file(fd);

	if (file == STDIN) {
		file = 0;
		return;
	}

	if (file == STDOUT) {
		file = 0;
		return;
	}

	if (file == STDERR) {
		file = 0;
		return;
	}

	if (file->dup_count == 0) // 더이상 해당 파일을 참조하는 곳이 없다면, 해당 파일은 닫혀야 한다.
		file_close(file);
	else
		file->dup_count--;
}

pid_t fork(const char *thread_name) {
	check_address(thread_name);
	return process_fork(thread_name, NULL);
}

int 
exec(const char *cmd_line) 
{
	check_address(cmd_line);

	off_t size = strlen(cmd_line) + 1; // 파일 사이즈는 NULL을 포함하기 위해서 +1을 해준다.
	char *cmd_copy = palloc_get_page(PAL_ZERO);
	if (cmd_copy == NULL) // 메모리 할당에 실패했을 경우
		return -1;

/** 2
 * cmd_line을 대신해서 process_exec()에 전달할 복사본(cmd_copy)을 만든다.
 * cmd_line은 const char* 이라서 수정할 수 없기 때문이다.
 */
	memcpy(cmd_copy, cmd_line, size);

// 만약 프로그램이 프로세스를 로드하지 못하거나, 다른 이유로 돌리지 못하게 되면 exit_status == -1을 반환하며 프로세스가 종료된다.
	if (process_exec(cmd_copy) == -1)
		return -1;

	return 0;  // process_exec 성공시 리턴 값 없음 (do_iret)
}

int wait(pid_t tid) {
	return process_wait(tid);
}

/** 2
 * extend file discriptor
 */
int dup2(int oldfd, int newfd) {
	if (oldfd < 0 || newfd < 0)
		return -1;

	struct file *oldfile = process_get_file(oldfd);

	if (oldfile == NULL)
		return -1;

	if (oldfd == newfd)
		return newfd;

	struct file *newfile = process_get_file(newfd);

	if (oldfile == newfile)
		return newfd;

	close(newfd);

	newfd = process_insert_file(newfd, oldfile);

	return newfd;
}