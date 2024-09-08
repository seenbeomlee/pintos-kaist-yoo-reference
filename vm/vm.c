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
/** 3
 * anonymous page
 * upage가 이미 사용 중인지 확인한다.
 * 페이지를 생성한다.
 * type에 따라 초기화 함수를 가져온다. (VM_UNINIT, VM_ANON, VM_FILE)
 * uninit 타입의 페이지로 초기화한다.
 * 필드 수정은 uninit_new를 호출한 이후에 해야 한다.
 * 생성한 페이지를 SPT에 추가한다.
 * 
 * 페이지가 초기화되는 과정은 다음과 같다.
 * uninit 페이지 구조체 생성 : 커널이 새로운 페이지 요청을 받으면, vm_allock_page_with_initializer 함수가 호출된다.
 * 이 함수는 페이지 구조체를 생성하고, 이 구조체에 페이지 유형에 맞는 적절한 초기화 함수들을 담아둔다.
 * (이때, 초기화를 하지는 않고, 초기화할 때 어떤 함수를 사용해야 하는지 담아만! 둔다.)
 * 이렇게 초기화되지 않은 상태의 페이지 타입은 VM과 uninitialized의 약자를 합친 VM_UNINIT이다.
 * 
 * lazy_loading을 구현하게 되면, 위에서 언급한 바와 같이 user 프로그램이 새 페이지에 처음 접근했을 때 page fault가 발생하게 되는데,
 * 그 이유는, 내용이 없을 뿐더러 할당된 물리 프레임도 없기 때문이다.
 * 
 * page fault가 발생했을 때 접근한 메모리의 physical frame이 존재하지 않는다는 것이 확인되면 이때가 바로 loading()이 필요한 시점이다.
 * 이때 물리 프레임을 할당하고나서 uninit_initialize가 호출되면서 앞서 설정한 초기화 함수들이 호출되어 드디어 초기화가 이루어진다.
 * 초기화 함수는 익명 페이지의 경우 anon_initializer, 파일 기반 페이지의 경우 file_backed_initializer이다.
 * 페이지 별 초기화 함수 외에도 lazy_load_segment 함수가 호출되며 이 함수에서 내용이 로드된다.
 */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) { // upage가 이미 사용중인지 확인한다.
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
		// 1) 페이지를 생성하고,
		struct page *page = malloc(sizeof(struct page));

		if (!page)
			goto err;

		// 2) type에 따라 초기화 함수를 가져와서,
		typedef bool (*initializer_by_type)(struct page *, enum vm_type, void *);
		initializer_by_type initializer = NULL; // NULL은 VM_UNINIT의 initializer 인가보다.

		switch (VM_TYPE(type)) {
			case VM_ANON :
				initializer = anon_initializer;
				break;
			case VM_FILE :
				initializer = file_backed_initializer;
				break;
		}

/** 3
 * page == 초기화할 page 구조체
 * upage == page를 할당한 가상 주소
 * init() == page의 내용을 초기화하는 함수
 * aux == init()에 필요한 보조값
 * initializer == page를 type에 맞게 초기화하는 함수
 * 
 * 실제로 C 언어에서는 '클래스'나 '상속' 개념이 없지만, 객체지향 프로그래밍의 '클래스 상속' 개념을 도입하기 위해 함수 포인터를 사용한다.
 */
		// 3) uninit 타입의 페이지로 초기화한다.
		uninit_new(page, upage, init, type, aux, initializer);
/** 3
 * 필드의 값을 수정할 때는 uninit_new 함수가 호출된 이후에 수정해야 한다.
 * uninit_new 함수 안에서 구조체 내용이 전부 새로 할당되기 때문이다.
 * uninit_new 함수 호출 이전에는 아무리 값을 추가해도 다 날라간다.
 */
		page->writable = writable;
		
		// 4) 생성한 페이지를 spt에 추가한다.
		return spt_insert_page(spt, page);
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
	// spt에서 va에 해당하는 page 찾기
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