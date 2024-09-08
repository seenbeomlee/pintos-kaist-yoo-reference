#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

/** 3
 * 파일을 기반으로 하는 file-backed page와 달리,
 * 이름이 지정된 파일 소스가 없기 때문에 anonymous라고 불리며, 스택과 힙 영역에서 사용된다.
 */
struct anon_page {
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
