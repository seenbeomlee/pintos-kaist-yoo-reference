#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

/** 2
 * extend file descriptor
 * 0의 경우, NULL이나 인자값과 구분하기 위한 dummy value로 사용될 것이다.
 * 따라서, 0,1,2가 아니라 1,2,3을 사용하게된다.
 */
#define STDIN 1
#define STDOUT 2
#define STDERR 3

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/** 2
 * process_exec ()에서 불러온 argument_stack () 함수를 구현하기 위해 선언부터 해준다.
 * char **argv로 받은 문자열 배열과 int argc로 받은 인자 개수를 처리한다.
 */
void argument_stack(char **argv, int argc, struct intr_frame *if_);

/** 2
 * file descriptor functions
 */
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
int process_close_file(int fd);

/** 2
 * hierarchical process structure
 * 현재 프로세스의 자식 리스트를 검색하여 해당 pid에 맞는 process descriptor를 반환한다.
 * pid를 갖는 프로세스 디스크립터가 존재하지 않을 경우 NULL을 반환한다.
 */
struct thread *get_child_process(int pid);

process_insert_file (int fd, struct file *f);

#endif /* userprog/process.h */
