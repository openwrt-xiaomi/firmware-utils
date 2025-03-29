/* SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause) */
#ifndef FDT_H
#define FDT_H
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 * Copyright 2012 Kim Phillips, Freescale Semiconductor.
 */

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

typedef uint16_t fdt16_t;
typedef uint32_t fdt32_t;
typedef uint64_t fdt64_t;

typedef struct fdt_header {
	fdt32_t magic;			 /* magic word FDT_MAGIC */
	fdt32_t totalsize;		 /* total size of DT block */
	fdt32_t off_dt_struct;		 /* offset to structure */
	fdt32_t off_dt_strings;		 /* offset to strings */
	fdt32_t off_mem_rsvmap;		 /* offset to memory reserve map */
	fdt32_t version;		 /* format version */
	fdt32_t last_comp_version;	 /* last compatible version */
	fdt32_t boot_cpuid_phys;	 /* Which physical CPU booting on */
	fdt32_t size_dt_strings;	 /* size of the strings block */
	fdt32_t size_dt_struct;		 /* size of the structure block */
} __attribute__ ((packed)) fdt_header_t;

typedef struct fdt_reserve_entry {
	fdt64_t address;
	fdt64_t size;
} __attribute__ ((packed)) fdt_reserve_entry_t;

typedef struct fdt_node_header {
	fdt32_t tag;
	char    name[0];
} __attribute__ ((packed)) fdt_node_header_t;

typedef struct fdt_property {
	fdt32_t tag;
	fdt32_t len;
	fdt32_t nameoff;
	char    data[0];
} __attribute__ ((packed)) fdt_property_t;

#define FDT_MAGIC	0xd00dfeed	/* 4: version, 4: total size */
#define FDT_TAGSIZE	sizeof(fdt32_t)

#define FDT_BEGIN_NODE	0x1    /* Start node: full name */
#define FDT_END_NODE	0x2    /* End node */
#define FDT_PROP	0x3    /* Property: name off, size, content */
#define FDT_NOP		0x4    /* nop */
#define FDT_END		0x9

#define FDT_V1_SIZE	(7*sizeof(fdt32_t))
#define FDT_V2_SIZE	(FDT_V1_SIZE + sizeof(fdt32_t))
#define FDT_V3_SIZE	(FDT_V2_SIZE + sizeof(fdt32_t))
#define FDT_V16_SIZE	FDT_V3_SIZE
#define FDT_V17_SIZE	(FDT_V16_SIZE + sizeof(fdt32_t))


int get_fdt_totalsize(const void * image, size_t offset, bool check);
int find_fdt_offset(const void * image, size_t size, int max_fdt_size);
fdt_header_t * find_fdt(const void * image, size_t size, int max_fdt_size);

fdt_property_t * get_fdt_prop(const void * img, const char * path, const char * name);
void * get_fdt_prop_val(const void * img, const char * path, const char * name, int * size);
int64_t get_fdt_prop_u32(const void * img, const char * path, const char * name);
const char * get_fdt_prop_str(const void * img, const char * path, const char * name);

#endif /* FDT_H */
