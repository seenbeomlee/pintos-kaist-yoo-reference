#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

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
#endif /* userprog/process.h */
