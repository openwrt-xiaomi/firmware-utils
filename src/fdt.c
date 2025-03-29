// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2025 OpenWrt.org
 *  Copyright (C) 2025 Oleg S <remittor@gmail.com>
 */

#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#include "fdt.h"

#define ROUNDUP(x, n) (((x) + (n - 1)) & ~(n - 1))


int get_fdt_totalsize(const void * image, size_t offset, bool check)
{
	uint8_t * data = (uint8_t *)image;
	uint32_t * img = (uint32_t * )(data + offset);
	fdt_header_t * hdr = (fdt_header_t *)img;
	uint32_t hdrsize, totalsize, version, last_comp_version;
	uint32_t off_dt_struct, off_dt_strings;
	uint32_t size_dt_struct, size_dt_strings;
	uint32_t boot_cpuid_phys;
	
	if (be32toh(img[0]) != FDT_MAGIC)
		return -1;
	
	hdrsize = sizeof(fdt_header_t);
	totalsize = be32toh(hdr->totalsize);
	if (totalsize >= INT_MAX || totalsize < hdrsize + 128)
		return -1;
	
	if (!check)
		return (int)totalsize;
	
	version = be32toh(hdr->version);
	last_comp_version = be32toh(hdr->last_comp_version);
	if (version != 17 || last_comp_version != 16)
		return -1;
	
	off_dt_struct = be32toh(hdr->off_dt_struct); 
	size_dt_struct = be32toh(hdr->size_dt_struct); 
	if (off_dt_struct <= hdrsize || off_dt_struct >= totalsize)
		return -1;

	if (off_dt_struct + size_dt_struct > totalsize)
		return -1;

	off_dt_strings = be32toh(hdr->off_dt_strings); 
	size_dt_strings = be32toh(hdr->size_dt_strings); 
	if (off_dt_strings <= hdrsize || off_dt_strings >= totalsize)
		return -1;

	if (off_dt_strings + size_dt_strings > totalsize)
		return -1;

	boot_cpuid_phys = be32toh(hdr->boot_cpuid_phys);
	if (boot_cpuid_phys != 0)
		return -1;
	
	return (int)totalsize;
}

int find_fdt_offset(const void * image, size_t size, int max_fdt_size)
{
	uint8_t * img;
	size_t pos;
	int totalsize;
	
	for (pos = 0; pos < size - sizeof(fdt_header_t) - 8; pos++) {
		img = (uint8_t *)image + pos;
		if (be32toh(*(uint32_t *)img) != FDT_MAGIC)
			continue;
		
		totalsize = get_fdt_totalsize(img, 0, true);
		if (totalsize <= 0)
			continue;
		
		if (max_fdt_size > 0 && totalsize > max_fdt_size)
			continue;
		
		return (int)pos;
	}
	return -1;
}

fdt_header_t * find_fdt(const void * image, size_t size, int max_fdt_size)
{
	int pos = find_fdt_offset(image, size, max_fdt_size);
	if (pos >= 0) {
		return (fdt_header_t *)( (uint8_t *)image + pos );
	}
	return NULL;
}

#define MAX_FDT_DEPTH 32

typedef struct fdt_ctx {
	fdt_header_t * hdr;
	uint8_t *    image;
	uint32_t *   img;
	uint32_t     image_size;
	uint32_t *   tree;
	uint32_t     tree_size;
	char *       strings;
	uint32_t     strings_size;
	int          depth;          /* current depth in tree */
	const char * path[MAX_FDT_DEPTH + 1];
	uint32_t *   pos;            /* current pos in tree */
	const char * target_path;    /* path for search */
	int          target_depth;
	const char * target_name;    /* name of property for search */
	fdt_property_t * res;        /* result of search */
	bool         show_error;
	bool         show_tree;
} fdt_ctx_t;


static
const char * get_fdt_string(const fdt_ctx_t * ctx, uint32_t str_offset)
{
	return (const char *)(ctx->strings + str_offset);
}

static
int show_fdt_prop(fdt_ctx_t * ctx, fdt_property_t * prop, const char * name)
{
	uint32_t size;
	int i;
	bool is_str;

	if (!ctx->show_tree)
		return 0;
	
	if (ctx)
		name = get_fdt_string(ctx, be32toh(prop->nameoff));

	size = be32toh(prop->len);
	is_str = true;
	for (i = 0; i < (int)size - 1; i++) {
		if (prop->data[i] < 0x20 || prop->data[i] > 0x7E) {
			is_str = false;
			break;
		}
	}
	if (size > 0 && is_str) {
		printf("  %s = \"%s\" \n", name, prop->data);
	} else {
		printf("  %s = [size:%d] \n", name, (int)size);
	}
	return 0;
}

static
int get_path_depth(const char * path) 
{
	const char * tp = path;
	size_t tplen = strlen(path);
	const char * dn;
	size_t dnlen;
	size_t i;
	int depth = 0;
	
	if (tp[0] == '/') {
		tp++;
		tplen--;
	}
	if (tplen == 0) {
		return 0;
	}
	if (tp[tplen - 1] == '/') {
		tplen--;
	}
	if (tplen == 0) {
		return 0;
	}
	dn = tp;
	for (i = 0; i <= tplen; i++) {
		if (tp[i] == '/'  || i == tplen) {
			dnlen = (size_t)tp + i - (size_t)dn;
			if (dnlen == 0) {
				//printf("ERR: incorrect target path format \n");
				return -12;
			}
			//printf("\"%.*s\" \n", (int)dnlen, dn);
			depth++;
			dn = tp + i + 1;
			dnlen = 0;
		}
	}
	return depth;
}

static
int check_search_target(fdt_ctx_t * ctx, fdt_property_t * prop)
{
	const char * name;
	size_t tplen = strlen(ctx->target_path);
	const char * tp = ctx->target_path;
	const char * dn;
	size_t dnlen;
	size_t i;
	int depth = 0;

	if (!ctx->target_name)
		return -1;

	if (ctx->depth != ctx->target_depth)
		return -1;

	name = get_fdt_string(ctx, be32toh(prop->nameoff));
	if (strcmp(name, ctx->target_name) != 0)
		return -1;
	
	if (ctx->target_depth <= 0)
		return -1;
	
	if (tp[0] == '/') {
		tp++;
		tplen--;
	}
	if (tplen == 0) {
		//printf("ERR: path is empty \n");
		return -10;
	}
	if (tp[tplen - 1] == '/') {
		tplen--;
	}
	if (tplen == 0) {
		//printf("ERR: path is empty \n");
		return -11;
	}
	dn = tp;
	for (i = 0; i <= tplen; i++) {
		if (tp[i] == '/'  || i == tplen) {
			dnlen = (size_t)tp + i - (size_t)dn;
			if (dnlen == 0) {
				//printf("ERR: incorrect target path format \n");
				return -12;
			}
			//printf("\"%.*s\" \n", (int)dnlen, dn);
			if (dn[dnlen - 1] == '*') {
				dnlen--;
				if (strlen(ctx->path[depth]) < dnlen) {
					return -6;
				}
				if (strncmp(dn, ctx->path[depth], dnlen) != 0) {
					return -7;
				}
			} else {
				if (strlen(ctx->path[depth]) != dnlen) {
					return -2;
				}
				if (strncmp(dn, ctx->path[depth], dnlen) != 0) {
					return -3;
				}
			}
			depth++;
			dn = tp + i + 1; // next node name
		}
	}
	return 0;
}

static
uint32_t * enum_fdt_nodes(fdt_ctx_t * ctx)
{
	int i;
	uint32_t tag = 0;
	uint32_t * pos;
	fdt_node_header_t * node;
	fdt_property_t * prop;
	const char * name;
	uint32_t size;
	
	while (true) {
		tag = be32toh(*ctx->pos);
		switch (tag) {
		case FDT_BEGIN_NODE:
			node = (fdt_node_header_t *)ctx->pos;
			if (ctx->depth < 0) {
				if (node->name[0] != 0) {
					if (ctx->show_error)
						printf("ERROR: FDT root name = \"%s\"\n", node->name);
					return NULL;
				}
				ctx->depth = 0;  // init root
			} else {
				if (node->name[0] == 0) {
					if (ctx->show_error)
						printf("ERROR: FDT node name is empty\n");
					return NULL;
				}
				ctx->path[ctx->depth++] = node->name;
			}
			if (ctx->show_tree && ctx->depth >= 0) {
				printf("/");
				for (i = 0; i < ctx->depth; i++) {
					printf("%s/", ctx->path[i]);
				}
				printf("\n");
			}
			if (ctx->depth == MAX_FDT_DEPTH) {
				if (ctx->show_error)
					printf("ERROR: FDT tree too deep \n");
				return NULL;
			}
			ctx->pos += 1 + (strlen(node->name) + 4) / 4;
			pos = enum_fdt_nodes(ctx);
			if (pos == NULL)
				return NULL;
			continue;
		case FDT_PROP:
			prop = (fdt_property_t *)ctx->pos;
			name = get_fdt_string(ctx, be32toh(prop->nameoff));
			size = be32toh(prop->len);
			if (size >= INT_MAX) {
				if (ctx->show_error)
					printf("ERROR: prop '%s' size = %u\n", name, size);
				return NULL;
			}
			if (ctx->show_tree) {
				show_fdt_prop(ctx, prop, name);
			}
			if (check_search_target(ctx, prop) == 0) {
				ctx->res = prop;
				return NULL;
			}
			ctx->pos += 3;  // tag + len + nameoff
			ctx->pos += (size + 3) / 4;
			continue;
		case FDT_NOP:
			ctx->pos += 1;
			continue;
		case FDT_END_NODE:
			if (ctx->depth > 0) {
				ctx->depth--;
			}
			ctx->pos += 1;
			continue;
		case FDT_END:
			return NULL;  /* EOF */
		default:
			if (ctx->show_error)
				printf("ERROR: Incorrect FDT tag id = 0x%X \n", tag);
			return NULL;
		}
	}
	return NULL;
}

static
int init_fdt_ctx(fdt_ctx_t * ctx, const void * image)
{
	fdt_header_t * hdr = (fdt_header_t *)image;
	int size;
	
	memset(ctx, 0, sizeof(fdt_ctx_t));
	size = get_fdt_totalsize(image, 0, true);
	if (size <= 0)
		return -1;

	ctx->hdr = hdr;
	ctx->image = (uint8_t *)image;
	ctx->img = (uint32_t *)image;
	ctx->image_size = size;
	ctx->tree = (uint32_t *)(ctx->image + be32toh(hdr->off_dt_struct));
	ctx->tree_size = be32toh(hdr->size_dt_struct);
	ctx->strings = (char *)(ctx->image + be32toh(hdr->off_dt_strings));
	ctx->strings_size = be32toh(hdr->size_dt_strings);
	return 0;
}

fdt_property_t * get_fdt_prop(const void * img, const char * path, const char * name)
{
	fdt_ctx_t ctx;
	
	if (init_fdt_ctx(&ctx, img) != 0)
		return NULL;
	
	if (!name) {
		ctx.show_error = true;
		ctx.show_tree = true;
		if (!path)
			return NULL;
	}
	if (path) {
		ctx.target_depth = get_path_depth(path);
		if (ctx.target_depth < 0) {
			if (ctx.show_error)
				printf("ERROR: Incorrect path! \n");
			return NULL;
		}
		ctx.target_path = path;
		ctx.target_name = name;
	}
	ctx.pos = ctx.tree;
	if (be32toh(ctx.pos[0]) != FDT_BEGIN_NODE)
		return NULL;
	
	if (ctx.pos[1] != 0)  /* empty string = root of nodes */
		return NULL;
		
	ctx.res = NULL;
	ctx.depth = -1;
	enum_fdt_nodes(&ctx);
	if (name && ctx.res) {
		return ctx.res;
	}
	return NULL;
}

void * get_fdt_prop_val(const void * img, const char * path, const char * name, int * size)
{
	fdt_property_t * prop = get_fdt_prop(img, path, name);
	if (prop) {
		if (size) {
			*size = be32toh(prop->len);
		}
		return (void *)prop->data;
	}
	return NULL;
}

int64_t get_fdt_prop_u32(const void * img, const char * path, const char * name)
{
	int size = -1;
	uint32_t * val = (uint32_t *)get_fdt_prop_val(img, path, name, &size);
	if (val && size == 4) {
		return be32toh(*val);
	}
	return -1;
}

const char * get_fdt_prop_str(const void * img, const char * path, const char * name)
{
	int size = -1;
	const char * val = (const char *)get_fdt_prop_val(img, path, name, &size);
	if (val && size > 0 && val[size - 1] == 0) {
		return val;
	}
	return NULL;
}
