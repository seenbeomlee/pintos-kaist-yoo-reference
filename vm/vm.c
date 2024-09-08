/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/** 3
 * frame management
 * frame table 추가
 */
#include "threads/mmu.h"
static struct list frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table); // 추가한 frame table 초기화
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	// 존재하지 않을 경우에만 삽입한다.
	return hash_insert(&spt->spt_hash, &page->hash_elem) ? false : true;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/** 3
 * frame management
 * palloc_get_page() 함수를 호출하여 사용자 풀에서 새로운 physical page(frame)를 가져온다.
 * 물리 페이지를 할당하고, 해당 페이지의 커널 가상 주소를 반환하는 함수인 palloc_get_page()를 사용한다.
 * 사용자 풀에서 페이지를 성공적으로 가져오면, 프레임을 할당하고 해당 프레임의 멤버를 초기화한 후 반환한다.
 * 페이지 할당을 실패할 경우, PANIC ("todo")로 표시한다. - 사용가능한 page가 없다면 swap out을 수행한다. => swap out을 구현한 이후 변경한다.
 */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	ASSERT (frame != NULL);

	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO); // user pool에서 새로운 physical page를 가져온다.

	if (frame->kva == NULL) // page 할당 실패 -> 나중에 swap_out 처리
		frame = vm_evict_frame(); 
	else
		list_push_back(&frame_table, &frame->frame_elem);

	frame->page = NULL;

	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
/** 3
 * 'f' : page fault 예외가 발생할 때 실행되던 context 정보가 담겨있는 interrupt frame 이다.
 * 
 * 'addr' : page fault 예외가 발생할 때 접근한 virtual address 이다.
 * 					즉, 이 virtual address에 접근했기 때문에 page fault가 발생한 것이다.
 * 
 * 'not_present'
 * - true : addr에 매핑된 physical page가 존재하지 않는 경우에 해당한다.
 * - false : read only page에 writing 작업을 하려는 시도에 해당한다.
 * 
 * 'write'
 * - true : addr에 writing 작업을 시도한 경우에 해당한다.
 * - false : addr에 read 작업을 시도한 경우에 해당한다.
 * 
 * 'user'
 * - true : user에 의한 접근에 해당한다.
 * - false : kernel에 의한 접근에 해당한다.
 */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/** 3
 * frame management
 * 인자로 주어진 va에 페이지를 하나 할당한다.
 * 해당 페이지로 vm_do_claim_page를 호출한다.
 * 
 * 주소 va에 해당하는 page로 vm_do_claim_page() 함수를 호출한다.
 * 먼저, va에 해당하는 page(virtual page)를 가져온다.
 * 그 다음, 해당 페이지로 vm_do_claim_page()를 호출한다.
 */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	struct page *page = spt_find_page(&thread_current()->spt, va);

	if (page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/** 3
 * vm_get_frame() 함수를 통해 프레임 하나를 얻는다.
 * 프레임의 페이지로 얻은 페이지를 연결한다.
 * 프레임의 물리적 주소로 얻은 프레임을 연결한다.
 * 현재 페이지 테이블에 가상 주소에 따른 frame을 매핑한다.
 * 
 * 인자로 주어진 page에 physical frame을 할당한다.
 * 먼저, vm_get_frame 함수를 호출하여 프레임을 가져온다.
 * 그런 다음, MMU를 설정한다.
 * 즉, 가상 주소와 물리 주소 간의 매핑을 페이지 테이블에 추가한다.
 */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) // 가상주소와 물리 주소를 매핑한다.
		return false;

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
// hash table을 사용하므로, hash_init()을 사용하여 초기화한다.
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(spt, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

/** 3
 * hash_entry : elem이 소속되어있는 structure의 포인터를 반환한다. structure의 type은 page가 된다.
 * hash_bytes : buf에서 인자 size만큼의 hash를 반환한다.
 */
uint64_t page_hash(const struct hash_elem *e, void *aux) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	return hash_bytes(page->va, sizeof *page->va);
}

bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	struct page *page_a = hash_entry(a, struct page, hash_elem);
	struct page *page_b = hash_entry(b, struct page, hash_elem);

	return page_a->va < page_b->va;
}

struct page* spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page *)malloc(sizeof(struct page));     
	page->va = pg_round_down(va); // va에 해당하는 hash_elem 찾기                                       
	struct hash_elem *e = hash_find(&spt->spt_hash, &page->hash_elem);  
	free(page);                                              

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL; // 있다면, e에 해당하는 페이지를 반환한다. 없다면, NULL 반환한다.
}