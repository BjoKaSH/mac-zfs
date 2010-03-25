/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2010 Alex Blewitt.
 * Copyright 2010 Björn Kahl.  All rights reserved.
 * Use is subject to license terms.
 */

/* Issue 37: Umem not ported
 * Since Mac OS X doesn't have umem natively, these provide shim routines 
 * until such time as they do have the appropriate routines.
 */

#ifndef _UMEM_H
#define _UMEM_H

#pragma warning When libuumem is ported, remove this

#define UMEM_DEFAULT    0x0000  /* normal -- may fail */
#define UMEM_NOFAIL     0x0100  /* Never fails -- may call exit(2) */
#define UMEM_FLAGS      0xffff  /* all settable umem flags */
#define	UMC_NODEBUG	0x00020000

#define	UMEM_CACHE_NAMELEN	31


extern void *umem_alloc(size_t, int);
/* extern void *umem_alloc_align(size_t, size_t, int); */
/* extern void *umem_zalloc(size_t, int); */
extern void umem_free(void *, size_t);
/* extern void umem_free_align(void *, size_t); */

typedef int umem_nofail_callback_t(void);
#define UMEM_CALLBACK_RETRY             0
#define UMEM_CALLBACK_EXIT(status)      (0x100 | ((status) & 0xFF))

extern void umem_nofail_callback(umem_nofail_callback_t *);

typedef int umem_constructor_t(void *, void *, int);
typedef void umem_destructor_t(void *, void *);
typedef void umem_reclaim_t(void *);

typedef struct umem_cache {
	char		cache_name[UMEM_CACHE_NAMELEN + 1];
	size_t		cache_bufsize;		/* object size */
	int			(*cache_constructor)(void *, void *, int);
	void		(*cache_destructor)(void *, void *);
	void		*cache_private;		/* opaque arg to callbacks */
	int			cache_objcount;		/* number of object in cache. */
} umem_cache_t;

extern umem_cache_t *
umem_cache_create(
        char *name,             /* descriptive name for this cache */
        size_t bufsize,         /* size of the objects it manages */
        size_t align,           /* required object alignment */
        umem_constructor_t *constructor, /* object constructor */
        umem_destructor_t *destructor, /* object destructor */
        umem_reclaim_t *reclaim, /* memory reclaim callback */
        void *private,          /* pass-thru arg for constr/destr/reclaim */
        void *vmp,            /* vmem source for slab allocation */
        int cflags);             /* cache creation flags */

void umem_cache_destroy(umem_cache_t *cp);
void *umem_cache_alloc(umem_cache_t *cp, int umflag);
void umem_cache_free(umem_cache_t *cp, void *buf);

#endif /* _UMEM_H */
