#include "alloc_pages.h"

static void zone_add_freeblocks(uint32_t pfn, size_t zsize);
static inline uint8_t frame_zone(uint32_t pfn);
inline frame_t *alloc_page(unsigned int gfp_mask);
inline frame_t *alloc_pages(unsigned int gfp_mask, uint32_t order);

static frame_t *__alloc_pages(unsigned int gfp_mask, unsigned int order);
static void split_buddy(block_t *bb, unsigned int order);
static void alloc_block(block_t *b, unsigned int gfp_mask);

/*
 static inline unsigned int roundup_h_pow( unsigned int v)
 {
 unsigned int v; // compute the next highest power of 2 of 32-bit v

 v--;
 v |= v >> 1;
 v |= v >> 2;
 v |= v >> 4;
 v |= v >> 8;
 v |= v >> 16;
 v++;

 return v;
 }
 */

PUBLIC inline int ffs(int x)
{
	int r = 1;

	if (!x)
		return 0;

	if (!(x & 0xffff)) {
		x >>= 16;
		r += 16;
	}

	if (!(x & 0xff)) {
		x >>= 8;
		r += 8;
	}

	if (!(x & 0xf)) {
		x >>= 4;
		r += 4;
	}

	if (!(x & 3)) {
		x >>= 2;
		r += 2;
	}

	if (!(x & 1)) {
		x >>= 1;
		r += 1;
	}

	return r;
}

PUBLIC inline int fls(int x)
{
	int msb;

	__asm__ __volatile__("bsr %1, %0\n\t": "=r" (msb): "r" (x));

	return msb;
}

#define ZONE_NUM   3

typedef struct zone_s
{
	/* block_t */
	struct list_head blocks[BUDDY_MAX_ORDER+1];
	// This bitmap is used to conviniently identify which block (>= requested size) is free
	uint32_t free_blocks_bitmap;
// other stuffs here
} zone_t;
zone_t zones[ZONE_NUM];

#define BIT(x) (1 << (x)) 
#define SETBITS(x,y) ((x) |= (y)) 
#define CLEARBITS(x,y) ((x) &= (~(y))) 
#define SETBIT(x,y) SETBITS((x), (BIT((y)))) 
#define CLEARBIT(x,y) CLEARBITS((x), (BIT((y)))) 
#define BITSET(x,y) ((x) & (BIT(y))) 
#define BITCLEAR(x,y) !BITSET((x), (y)) 
#define BITSSET(x,y) (((x) & (y)) == (y)) 
#define BITSCLEAR(x,y) (((x) & (y)) == 0) 
#define BITVAL(x,y) (((x)>>(y)) & 1) 

static void zones_init()
{
	int i, j;

	for (i = 0; i < ZONE_NUM; i++) {
		for (j = 0; j <= BUDDY_MAX_ORDER; j++)
			INIT_LIST_HEAD(&zones[i].blocks[j]/*.list*/);

		CLEARBITS(zones[i].free_blocks_bitmap, 0x00);
	}
}

#define PF_FREE 0
static void alloc_pages_init(unsigned long pf_no)
{
	int start_pfn;
	unsigned int zsize;
	int curr_pfn;

	start_pfn = -1;
	zsize = 0;

	zones_init();

	// this should be rewritten
	for (curr_pfn = 0; curr_pfn < pf_no; curr_pfn++) {
		// we don't want evaluate everything if the page is not a terminal page
		//if (mmap[curr_pfn].status == PF_FREE && 
		//    start_pfn != 0)
		//	continue;

		if ((mmap[curr_pfn].status == PF_FREE) && (start_pfn == -1)) {
			start_pfn = curr_pfn;
			continue;
		}
		// remember to set mmap[last].status = PF_LAST
		if ((mmap[curr_pfn].status != PF_FREE) && (start_pfn != 0)) {
			zsize = curr_pfn - start_pfn;
			zone_add_freeblocks(start_pfn, zsize);
			start_pfn = -1;
			continue;
		}
		// splitted for convinience of readability
		if ((mmap[curr_pfn].status == PF_FREE) && (frame_zone(curr_pfn) != frame_zone(curr_pfn + 1)
		    || curr_pfn == (pf_no - 1))) {
			zsize = curr_pfn - start_pfn + 1; // border included
			zone_add_freeblocks(start_pfn, zsize);
			start_pfn = -1;
		}
	}
}

#define BB_ATOMIC  1
#define BB_START   2
#define BB_END     3

static void add_blocks_to_zone(uint32_t pfn, size_t order)
{
	uint8_t zt;
	block_t *block;

	block = &mmap[pfn].block;

	// case 2^0 = 1 page
	if (order == 0) {
		block->type = BB_ATOMIC;
	} else {
		block->type = BB_START;
		block_t *term_block = &mmap[pfn + (1 << order) - 1].block;
		term_block->type = BB_END;
		term_block->size = (1 << order);
		term_block->start_fno = pfn;
	}

	block->start_fno = pfn;
	block->size = (1 << order);
	zt = frame_zone(pfn)/* >> 1*/;

	// new, head
	list_add(&(block->list), &(zones[zt]).blocks[order]/*.list*/);
	SETBIT(zones[zt].free_blocks_bitmap, order);
}

static void zone_add_freeblocks(uint32_t pfn, size_t zsize)
{
	size_t order;

	/* 
	 Is nice to use recursion in here but it is nicer to reduce the recursion level 
	 so here we add a while loop like this to keep as long as possible the recursion level
	 k = 1
	 */
	while (zsize > (1 << BUDDY_MAX_ORDER + 1) - 1) {
		//add_freebocks_to_zone(pfn, BUDDY_MAX_ORDER);
		zone_add_freeblocks(pfn, 1 << BUDDY_MAX_ORDER);
		zsize -= 1 << BUDDY_MAX_ORDER;
		pfn += 1 << BUDDY_MAX_ORDER;
	}

	order = fls(/*roundup_h_pow(*/zsize/*)*/) /*- 1*/;
	// add to list with the order
	add_blocks_to_zone(pfn, order);

	size_t zsize_next = zsize;		// & ( ~(1 << order));

	CLEARBIT(zsize_next, order);

	if (zsize_next)
		zone_add_freeblocks(pfn + (1 << order), zsize_next);
}

static inline uint8_t frame_zone(uint32_t pfn)
{
	// zero indexing
	pfn++;

	if (pfn > ZONE_NORMAL_PAGE_BOUNDARY)
		return ZONE_HIGH;

	if (pfn <= ZONE_NORMAL_PAGE_BOUNDARY && pfn > ZONE_DMA_PAGE_BOUNDARY)
		return ZONE_NORMAL;

	return ZONE_DMA;
}

//extern inline struct frame_t * alloc_page(unsigned int gfp_mask);
//Allocate a single page and return a struct address
//extern struct frame_t * alloc_pages(unsigned int gfp_mask, unsigned int order);
//Allocate 2order number of pages and returns a struct page

/*
 unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order)
 {
 struct page *page;

 //VM_BUG_ON((gfp_mask & __GFP_HIGHMEM) != 0);

 page = alloc_pages(gfp_mask, order);
 if (!page)
 return 0;
 return (unsigned long) page_address(page);
 }
 */

// Allocate a free page and return frame_t
inline frame_t *alloc_page(unsigned int gfp_mask)
{
	return alloc_pages(gfp_mask, 0);
}

// Allocate block of pages and return frame_t
inline frame_t *alloc_pages(unsigned int gfp_mask, uint32_t order)
{
	return __alloc_pages(gfp_mask, order);
}

#define page_to_virt(p) p->fno
// Allocate block of pages and return logical address
unsigned long get_free_pages(unsigned int gfp_mask, uint32_t order)
{
	frame_t *page;

	//Pull single page from buddy system
	page = alloc_pages(gfp_mask, order);

	if (page == NULL)
		return 0;		//NULL;

	// page_to_virt to be implemented
	return page_to_virt(page);
}

inline unsigned long get_free_page(unsigned int gfp_mask)
{
	return get_free_pages(gfp_mask, 0);
}

static inline uint8_t get_zone_from_mask(unsigned int mask)
{
	if ( BITSSET(mask, GFP_NORMAL))
		return ZONE_NORMAL;

	if ( BITSSET(mask, GFP_DMA ))
		return ZONE_DMA;

	return ZONE_HIGH;
}

#ifndef NDEBUG
#define ASSERT(p)							\
	do { if ( unlikely(!(p)) ) assert_failed(#p); } while (0)

#ifndef assert_failed
#define assert_failed(p)						\
	do {								\
		printk("Assertion '%s' failed, line %d, file %s\n", p ,	\
		       __LINE__, __FILE__);				\
		BUG();							\
} while (0)

// should disable IRQs and PAUSE the system
#define BUG()   while(1)

/*
 #define	BUG()   __bug(__FILE__, __LINE__)

 void __bug(char *file, int line)
 {
 printf("*** bug at %s:%d\n", file, line);
 crash();
 }
 */

#endif /* assert_failed */
#else
#define ASSERT(p) ((void)0)
#endif /* NDEBUG */

// compiler.h
#define barrier()     __asm__ __volatile__("": : :"memory")
#define likely(x)     __builtin_expect((x),1)
#define unlikely(x)   __builtin_expect((x),0)

// Avoiding recursion
block_t *get_free_block(uint8_t pzone, unsigned int order)
{
	unsigned int order_distance;
	unsigned int buddy_bmp = 0;

	if (order > BUDDY_MAX_ORDER)
		return NULL;

	if (BITSET(zones[pzone].free_blocks_bitmap, order))
		///TODO Solve the big problem right here!!!
		return list_entry(zones[pzone].blocks[order].next, block_t, list);

	// zeroing the lowest bits
	// calculating the first free blocks available 
	buddy_bmp = (zones[pzone].free_blocks_bitmap >> order) << order;

	unsigned int first_available_blk;
	first_available_blk = ffs(buddy_bmp) - 1;
	order_distance = first_available_blk - order;

	block_t *buddy = list_entry(zones[pzone].blocks[first_available_blk].next, block_t, list);

	while (order_distance--) {
		split_buddy(buddy, first_available_blk/*--*/);
		first_available_blk >>= 1;
	}

	return buddy;
}

static inline void del_blocks_from_zone(uint32_t pfn, size_t order)
{
	block_t *b = &mmap[pfn].block;

	list_del(&b->list);

	if(list_empty(zones[frame_zone(pfn)].blocks[order].next))
		CLEARBIT(zones[frame_zone(pfn)].free_blocks_bitmap, order);
}

// returns left splitted block, NULL on error
static void split_buddy(block_t *bb, unsigned int order)
{
	size_t bsize = bb->size;

	del_blocks_from_zone(bb->start_fno, order);

	add_blocks_to_zone(bb->start_fno, /*bsize*/ order >> 1);
	add_blocks_to_zone(bb->start_fno + (bsize >> 1), /*bsize*/ order >> 1);
}

/*
 unsigned long get_free_page(unsigned int gfp_mask);
 //Allocate a single page, zero it and return a virtual address
 unsigned long __get_free_page(unsigned int gfp_mask);
 //Allocate a single page and return a virtual address
 unsigned long __get_free_pages(unsigned int gfp_mask, unsigned int order);
 //Allocate 2order number of pages and return a virtual address
 */

static frame_t *__alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	uint8_t pzone;
	block_t *block;

	ASSERT(order < BUDDY_MAX_ORDER);

	pzone = get_zone_from_mask(gfp_mask);
	block = get_free_block(pzone, order);

	if (block == NULL)
		printf("Error while allocating");

	alloc_block(block, gfp_mask);

	return &mmap[block->start_fno];
}

#define __mark_block(start, size, flags)				\
		({							\
	        	int i;						\
	        	for(i=start; i < (start + size); i++)		\
	        		mmap[i].status = flags;			\
		})

static void alloc_block(block_t *b, unsigned int gfp_mask)
{
	__mark_block(b->start_fno, b->size, gfp_mask);

	del_blocks_from_zone(b->start_fno, ffs(b->size) - 1);
}

#define BUDDY_NONE  NULL
#define BUDDY_RIGHT 1
#define BUDDY_LEFT  2

#define RIGHT 0
#define LEFT  1

static inline uint8_t has_neighbour(block_t *b, uint8_t direction)
{
	block_t *nb;

	ASSERT(direction == 0 || direction == 1);

	if(direction == RIGHT){
		if((b->start_fno + b->size) > MAX_MMAP - 1)
			goto none_found;

		nb = &mmap[b->start_fno + b->size].block;
	}else{
		if((b->start_fno - 1) < 0)
			goto none_found;

		nb = &mmap[b->start_fno - 1].block;
	}

	if ((mmap[nb->start_fno].status == PF_FREE) && (b->size == nb->size) &&
	    (frame_zone(b->start_fno) == frame_zone(nb->start_fno)))
		return 1;

none_found:
	return 0;
}

static block_t *check_for_buddy(block_t *b)
{
	block_t *nb;

	// check if 2 buddies are MAX_BUDDY_ORDER
	if(b->size == (1 << BUDDY_MAX_ORDER))
		return BUDDY_NONE;

	if(has_neighbour(b, RIGHT))
		return &mmap[b->start_fno + b->size].block;

	if(has_neighbour(b, LEFT)){
		size_t left_fno_start;
		left_fno_start = mmap[b->start_fno - 1].block.start_fno;
		return &mmap[left_fno_start].block;
	}

	return BUDDY_NONE;
}


static block_t *coalesce_buddies(block_t *b, block_t *nb)
{
	size_t new_buddy_order;
	block_t *left_boundary;

	/* KNOWN ISSUE: actually we accept that the old end of the blocks remain
	 with the flag BB_END but it would be nice to clean that situation */
	new_buddy_order = ffs(b->size + nb->size) - 1;

	// we MUST! del_block_from_zone()
	del_blocks_from_zone(b->start_fno, ffs(b->size) - 1);
	del_blocks_from_zone(nb->start_fno, ffs(b->size) - 1);
	//list_del(&b->list);
	//list_del(&nb->list);

	left_boundary = (b->start_fno < nb->start_fno)? b : nb;
	add_blocks_to_zone(left_boundary->start_fno, new_buddy_order);

	return left_boundary;
}




static block_t *coalesce_buddy(block_t *b, uint8_t which)
{
	block_t *nb;
	size_t bsize, new_buddy_order;

	if (which == BUDDY_RIGHT) {
		nb = &mmap[b->start_fno + b->size].block;
	}

	if (which == BUDDY_LEFT) {
		nb = b;
		size_t left_fno;
		left_fno = mmap[b->start_fno - 1].block.start_fno;
		b = &mmap[left_fno].block;
	}

	/* KNOWN ISSUE: actually we accept that the old end of the blocks remain 
	 with the flag BB_END but it would be nice to clean that situation */
	new_buddy_order = ffs(b->size + nb->size) - 1;
	list_del(&b->list);
	list_del(&nb->list);
	add_blocks_to_zone(b->start_fno, new_buddy_order);

	return b;
}

void dump_buddies()
{
	for (;;) {

	}
}

extern inline void free_page(void *addr);

//inline void free_page()

//Free an order number of pages from the given page
extern void __free_pages(frame_t *page, unsigned int order);
//Free a single page
// extern void __free_page(frame_t *page);
//Free a page from the given virtual address

void __free_pages(frame_t *frame, unsigned int order)
{
	uint8_t fz;
	block_t *b, *nb;
	uint8_t buddy_type;

	fz = frame_zone(frame->fno);
	b = &mmap[frame->fno].block; //frames[fz].

	if (b->type != BB_START && b->type != BB_ATOMIC) {
		printf("Error while deallocating\n");
		return;
	}

	__mark_block(b->start_fno, b->size, PF_FREE);

	//list_add_tail(&(b->list), &(zones[fz]).blocks[order]/*.list*/);
	add_blocks_to_zone(b->start_fno, order);

	while ((nb = check_for_buddy(b)))
		b = coalesce_buddies(b, /*buddy_type*/nb);
}

void free_pages(frame_t *page, unsigned int order)
{
	// add some sanity checks
	__free_pages(page, order);
}

static inline void mmap_init()
{
	int i = 0;

	for(; i < MAX_MMAP; i++){
		mmap[i].fno = i;
		mmap[i].status = PF_FREE;
		memset((void *)&(mmap[i].block), 0, sizeof(block_t));
	}

	return;
}


//#define GFP_NORMAL 0x00
#define GFP_KERNEL 0x08

#ifndef UNIT_TEST

int main()
{
	frame_t *p_no, *pp_no, *ppp_no, *pppp_no;

#ifdef USER_SP
	mmap_init();
#endif
	alloc_pages_init(MAX_MMAP);
	// dump_buddies();
	p_no = alloc_page(/*GFP_NORMAL*/ GFP_DMA | GFP_KERNEL);
	pp_no = alloc_page(GFP_DMA | GFP_KERNEL);
	ppp_no = alloc_page(GFP_DMA | GFP_KERNEL);
	free_pages(p_no, 0);
	free_pages(pp_no, 0);
	pppp_no = alloc_page(GFP_DMA | GFP_KERNEL);
	free_pages(ppp_no, 0);
	free_pages(pppp_no, 0);

	p_no = alloc_page(/*GFP_NORMAL*/ GFP_NORMAL | GFP_KERNEL);
	free_pages(p_no, 0);

	p_no = alloc_page(/*GFP_NORMAL*/ GFP_NORMAL | GFP_KERNEL);
	free_pages(p_no, 0);

	return 0;
}

#endif
