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
 * Copyright 2010 Bjoern Kahl
 * Using an idea from Alex Blewitt.
 *
 * This file does not contain code from OpenSolaris, it is an
 * independent reimplementations of (parts of) libumems API.
 *
 * All rights reserved.  Use is subject to license terms.
 */

#include <umem.h>


int umem_nofail_default_callback(void) {
	return UMEM_CALLBACK_EXIT(255);
}

umem_nofail_callback_t *umem_no_fail_callback_global_ptr = &umem_nofail_default_callback;

void umem_nofail_callback(umem_nofail_callback_t *cb_a) {
  umem_no_fail_callback_global_ptr = cb_a;
}

int umem_alloc_retry(umem_cache_t *cp, int umflag) {
	/* some "alloc" function ran out of memory. decide what to do: */

	if ( (umflag & UMEM_NOFAIL) == UMEM_NOFAIL) {
		int nofail_cb_res = umem_no_fail_callback_global_ptr();

		if (nofail_cb_res == UMEM_CALLBACK_RETRY)
			/* we are allowed to retry */
			return 1;

		if ( (nofail_cb_res & ~0xff) != UMEM_CALLBACK_EXIT(0) ) {
			/* callback returned unexpected value. */
			log_message("nofail callback returned %x\n", nofail_cb_res);
			nofail_cb_res = UMEM_CALLBACK_EXIT(255);
		}

		exit(nofail_cb_res & 0xff);
		/*NOTREACHED*/
	}

	/* allocation was allowed to fail. */
	return 0;
}


/* umem_alloc(size_t size, int flags) */
void *umem_alloc(size_t size, int _) 
{
	return malloc(size);
}

/* umem_free(void *mem, size_t size_of_object_freed) */
void umem_free(void *mem, size_t _)
{
	free(mem);
}

/* umem_zalloc(size_t size, int flags) */
void *umem_zalloc(size_t size, int _) 
{
	return malloc(size);
}

umem_cache_t *
umem_cache_create(
        char *name,             /* descriptive name for this cache */
        size_t bufsize,         /* size of the objects it manages */
        size_t align,           /* required object alignment */
        umem_constructor_t *constructor, /* object constructor */
        umem_destructor_t *destructor, /* object destructor */
        umem_reclaim_t *reclaim, /* memory reclaim callback */
        void *private,          /* pass-thru arg for constr/destr/reclaim */
        void *vmp,            /* vmem source for slab allocation */
        int cflags)             /* cache creation flags */
{
	/* in this simple implementation we ignore the vmem source.  all
	   memory comes from malloc().

	   In fact not even have a real cache here!
	*/

	umem_cache_t *cp = (umem_cache_t *)malloc(sizeof(umem_cache_t));

	if (0 == cp) {
		if (umem_alloc_retry(0, cflags)) {
			/* retry requested.  Do so, but don't allow another retry
			   to avoid infinit loops. */
			cp = umem_cache_create(name, bufsize, align,
								   constructor, destructor, reclaim, private,
								   vmp, cflags & ~UMEM_NOFAIL);
			if (cp)
				return cp;

			/* no luck again, and failing not allowed -> commit suicide. */
			exit(UMEM_CALLBACK_EXIT(255));

		} else {

			/* allocation was allowed to fail. */
			return 0;
		}
	}

	bzero(cp, sizeof(umem_cache_t));

	if (0 == name)
		name = "zfs anon umem cache";
	strncpy(cp->cache_name, name, UMEM_CACHE_NAMELEN);

	cp->cache_bufsize = bufsize;
	cp->cache_constructor = constructor;
	cp->cache_destructor = destructor;
	cp->cache_private = private;

	return cp;
}

void umem_cache_destroy(umem_cache_t *cp) {
	if (cp->cache_objcount != 0)
		log_message("Destroying umem cache with active objects!\n");

	free(cp);
}

void *umem_cache_alloc(umem_cache_t *cp, int umflag) {
	void *buf = malloc(cp->cache_bufsize);
	if (0 == buf) {
		/* check what to do in case of no memory */
		if (umem_alloc_retry(cp, umflag) == 1) {
			/* we are not allowed to fail and should retry.
			 Avoid infinit loop by allowing failure. */
			buf = umem_cache_alloc(cp, umflag & ~UMEM_NOFAIL);
			if (0 == buf) {
				/* no luck & failure not allowed -> commit suicide */
				exit(UMEM_CALLBACK_EXIT(255));
			}

			if (cp->cache_constructor)
				cp->cache_constructor(buf, cp->cache_private, UMEM_DEFAULT);
			cp->cache_objcount++;
		}
		/* reached if (1) got our memory, or (2) ran out of memory and
		   were allowed to fail. */
	}
	return buf;
}

void umem_cache_free(umem_cache_t *cp, void *buf) {
	if (cp->cache_destructor)
		cp->cache_destructor(buf, cp->cache_private);

	free(buf);
	cp->cache_objcount--;
}


/* End */
