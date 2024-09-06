#include <stdbool.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

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

#endif /* userprog/syscall.h */
