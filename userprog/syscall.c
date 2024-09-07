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
		default:
			exit(-1);
	}
}

/** 2
 * 주소 값이 user 영역에서 사용하는 주소 값인지 확인한다.
 * user 영역을 벗어난 영역일 경우, process를 종료한다. (exit (-1))
 * pintos에서는 시스템 콜이 접근할 수 있는 주소를 0cx0000000 ~ 0x8048000(== KERN_BASE) 으로 제한한다. (이 이상은 커널 영역이다.)
 * 유저 영역을 벗어난 영역일 경우, 비정상 접근이라고 판단하여 exit(-1)로 프로세스를 종료한다.
 */
void 
check_address (void *addr) {
	if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1);
}

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
	check_address(buffer);

	if (fd == 0) {  // 0(stdin) -> keyboard(standard input)로 직접 입력
		int i = 0;  // 쓰레기 값 return 방지
		char c;
		unsigned char *buf = buffer;

		for (; i < length; i++) {
			c = input_getc();
			*buf++ = c;
			if (c == '\0')
				break;
		}

		return i; // i == length
	}
	// 그 외의 경우
	if (fd < 3)  // stdout, stderr를 읽으려고 할 경우 & fd가 음수일 경우
		return -1;

	struct file *file = process_get_file(fd);
	off_t bytes = -1;

	if (file == NULL)  // 파일이 비어있을 경우
		return -1;

	lock_acquire(&filesys_lock);
	bytes = file_read(file, buffer, length);
	lock_release(&filesys_lock);

	return bytes;
}

int write(int fd, const void *buffer, unsigned length) {
	check_address(buffer);

	off_t bytes = -1;

	if (fd <= 0)  // stdin에 쓰려고 할 경우 & fd 음수일 경우
		return -1;

	if (fd < 3) {  // 1(stdout) * 2(stderr) -> console로 출력
		putbuf(buffer, length);
		return length;
	}

	struct file *file = process_get_file(fd);

	if (file == NULL)
		return -1;

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
	struct file *file = process_get_file(fd);

	if (fd < 3 || file == NULL)
		return;

	process_close_file(fd);

	file_close(file);
}

pid_t fork(const char *thread_name) {
	check_address(thread_name);
	return process_fork(thread_name, NULL);
}

int 
exec(const char *cmd_line) 
{
	check_address(cmd_line);

	off_t size = strlen(cmd_line) + 1;
	char *cmd_copy = palloc_get_page(PAL_ZERO);

	if (cmd_copy == NULL)
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