#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"

/** 3
 * supplemental page table
 */
#include <hash.h>

enum vm_type {
	/* page not initialized */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	VM_ANON = 1,
	/* page that realated to the file */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page {
	const struct page_operations *operations;
	void *va;              /* Address in terms of user space */
	struct frame *frame;   /* Back reference for frame */

	/* Your implementation */

	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	struct hash_elem hash_elem; // supplemental page table
	bool writable; // frame management
	};
};

/* The representation of "frame" */
struct frame {
	void *kva;
	struct page *page;
	struct list_elem frame_elem; // frame elem 추가
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

/** 3
 * anonymous page
 */
struct vm_load_arg {
	struct file *file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
struct supplemental_page_table {
	struct hash spt_hash;
};

#include "threads/thread.h"
/** 3
 * supplemental page table을 초기화한다.
 * 이 함수는 새로운 프로세스가 시작될 때 initd()와 프로세스가 fork()될 때 호출된다.
 */
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);

/** 3
 * supplementary page table에서 va에 해당하는 구조체 페이지를 찾아 반환한다.
 */
struct page *spt_find_page (struct supplemental_page_table *spt, void *va);

/** 3
 * supplementary page table에 struct page를 삽입한다.
 * 가상 주소가 이미 supplementary page table에 존재하면 삽입하지 않고, 존재하지 않으면 삽입한다.
 */
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

/** 3
 * spt에 넣을 인덱스를 해시 함수를 돌려서 도출한다.
 * hash table이 hash elem을 원소로 가지고 있으므로 페이지 자체에 대한 정보를 가져온다.
 * 인덱스를 리턴해야하므로, hash_bytes로 리턴한다.
 */
uint64_t page_hash(const struct hash_elem *e, void *aux);

/** 3
 * 체이닝 방식의 spt를 구현하기 위한 함수이다.
 * 해시 테이블 버킷 내의 두 페이지의 주솟값을 비교한다.
 */
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);

/** 3
 * supplmental page
 */
void hash_page_destroy(struct hash_elem *e, void *aux);

#endif  /* VM_VM_H */
