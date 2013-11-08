#ifndef _ALLOC_PAGES_H
#define _ALLOC_PAGES_H

#define USER_SP

#ifdef USER_SP

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "list.h"

#ifndef NULL
#define NULL   ((void *) 0)
#endif

// it really needs to be dinamically allocated 
// bootmem allocator is really welcome here   
typedef struct block_s
{
	struct list_head list;
	size_t start_fno;
	size_t size;
	uint8_t type;
} block_t;

/*
 enum {
 PF_FREE,
 GFP_NORMAL, 
 } frame_status_t;
 */
typedef struct frame_s
{
	size_t fno;
	/*frame_status_t*/uint8_t status;
	block_t block;
// pid
// other stuffs
} frame_t;
//EXTERN frame_t *mmap;
#define MAX_MMAP 20
frame_t mmap[MAX_MMAP];

#define PUBLIC 

#define PAGE_SHIFT 12

#define printk(fmt, args...) printf(fmt, args)

#endif 

#define ZONE_DMA_BOUNDARY    0x1000000
#define ZONE_NORMAL_BOUNDARY 0x40000000
/*
// 0x4000 >> 12 = 4
#define ZONE_DMA_BOUNDARY    0x4000
// 0x10000 >> 12 = 16 The rest should be HIGH_MEM
#define ZONE_NORMAL_BOUNDARY 0x10000
*/

#define ZONE_DMA_PAGE_BOUNDARY (ZONE_DMA_BOUNDARY >> PAGE_SHIFT)
#define ZONE_NORMAL_PAGE_BOUNDARY (ZONE_NORMAL_BOUNDARY >> PAGE_SHIFT)
//#define ZONE_HIGH_PAGE_BOUNDARY

#define ZONE_DMA    0
#define ZONE_NORMAL 1
#define ZONE_HIGH   2

#define GFP_DMA 0x1
#define GFP_NORMAL 0x2
#define GFP_HIGH 0x4

#define BUDDY_MAX_ORDER 2 /* Buddy block max order */

#endif

/* 000000000100 */
/* 001000000000 */
