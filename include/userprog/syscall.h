#include <stdbool.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Process identifier. */
typedef int pid_t;

void syscall_init (void);

void check_address (void *addr);

/** 2
 * 얘는 뭐지 그럼?
 */
void halt (void);
void exit( int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

/** 2
 * 파일을 열 때 사용하는 시스템 콜
 * 파일이 없을 경우 실패한다.
 * 성공시 fd를 반환, 실패시 -1을 반환한다.
 * file == 파일의 이름 및 경로 정보
 */
int open(const char *file);

/** 2
 * 파일의 크기를 알려주는 시스템 콜
 * 성공시 파일의 크기를 반환, 실패시 -1을 반환한다.
 */
int filesize(int fd);

/** 2
 * 열린 파일의 데이터를 읽는 시스템 콜
 * 성공시 읽은 bytes를 반환, 실패시 -1을 반환한다.
 * buffer : 읽은 데이터를 저장할 버퍼의 주소값
 * size : 읽을 데이터 크기
 * fd 값이 0(standard input)이라면, 키보드의 데이터를 읽어 buffer에 저장한다. (input_getc() 사용)
 */
int read(int fd, void *buffer, unsigned length);

/** 2
 * 열린 파일의 데이터를 기록하는 시스템 콜
 * 성공시 기록한 데이터의 바이트 수를 반환, 실패시 -1을 반환
 * buffer : 기록할 데이터를 저장한 버퍼의 주소 값
 * size : 기록할 데이터 크기
 * fd 값이 1(standard output)일 때, 버퍼에 저장된 데이터를 화면에 출력한다. (putbuf () 이용)
 */
int write(int fd, const void *buffer, unsigned length);

/** 2
 * 열린 파일의 위치(offset)를 이동하는 시스템 콜
 * position : 현재 위치(offset)를 기준으로 이동할 거리
 * => 아닌 것 같은데? 그냥 position 이라는 위치로 이동시키는 것 같다.
 */
void seek(int fd, unsigned position);

/** 2
 * 열린 파일의 위치(offset)를 알려주는 시스템 콜
 * 성공시 파일의 위치(offset)를 반환, 실패시 -1을 반환한다.
 */
int tell(int fd);

/** 2
 * 열린 파일을 닫는 시스템 콜
 * 파일을 닫고 file descriptor를 제거한다.
 */
void close(int fd);

pid_t fork(const char *thread_name);

/** 2
 * 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜
 * 프로세스를 생성하는 함수를 이용한다. (command line parsing)
 * 프로세스 생성에 성공시 생성된 프로세스의 pid 값을 반환, 실패시 -1을 반환
 * 부모 프로세스는 자식 프로세스의 응용 프로그램이 메모리에 탑재 될 때 까지 대기한다.
 * semaphore를 이용한다.
 * cmd_line = 새로운 프로세스에 실행할 프로그램 명령어
 * pid_t는 tid_t와 동일한 int 자료형이다.
 */
int exec(const char *cmd_line);

/** 2
 * 현재 process_wait()는 -1을 리턴한다. - init process는 user process가 종료될 때까지 대기하지 않고 핀토스를 종료시킨다.
 * process_wait() 기능은 다음과 같다.
 * 1. 자식 프로세스가 모두 종료될 때까지 대기(sleep state)한다.
 * 2. 자식 프로세스가 올바르게 종료되었는지 확인한다.
 * wait() 시스템 콜을 구현한다.
 */
int wait(pid_t tid);

#endif /* userprog/syscall.h */
