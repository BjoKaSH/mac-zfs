/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)kernel.c	1.3	06/03/16 SMI"

#ifdef __APPLE__
#import <maczfs-thread.h>
#import <sys/mount.h>
#endif /* __APPLE__ */
#include <assert.h>
#include <sys/zfs_context.h>
#include <poll.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/spa.h>
#ifndef __APPLE__
#include <sys/processor.h>
#endif

/*
 * Emulation of kernel services in userland.
 */

uint64_t physmem;
vnode_t *rootdir = (vnode_t *)0xabcd1234;

/*
 * =========================================================================
 * threads
 * =========================================================================
 */
/*ARGSUSED*/
kthread_t *
zk_thread_create(void (*func)(), void *arg)
{
	thread_t tid;

	VERIFY(thr_create(0, 0, (void *(*)(void *))func, arg, THR_DETACHED,
	    &tid) == 0);

	return ((void *)(uintptr_t)tid);
}

/*
 * =========================================================================
 * mutexes
 * =========================================================================
 */
void
zmutex_init(kmutex_t *mp)
{
	mp->m_owner = NULL;
	(void) _mutex_init(&mp->m_lock, USYNC_THREAD, NULL);
}

void
zmutex_destroy(kmutex_t *mp)
{
	ASSERT(mp->m_owner == NULL);
	(void) _mutex_destroy(&(mp)->m_lock);
	mp->m_owner = (void *)-1UL;
}

void
mutex_enter(kmutex_t *mp)
{
	ASSERT(mp->m_owner != (void *)-1UL);
	ASSERT(mp->m_owner != curthread);
	(void) mutex_lock(&mp->m_lock);
	ASSERT(mp->m_owner == NULL);
	mp->m_owner = curthread;
}

int
mutex_tryenter(kmutex_t *mp)
{
	ASSERT(mp->m_owner != (void *)-1UL);
	if (0 == mutex_trylock(&mp->m_lock)) {
		ASSERT(mp->m_owner == NULL);
		mp->m_owner = curthread;
		return (1);
	} else {
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	(void) mutex_unlock(&mp->m_lock);
}

void *
mutex_owner(kmutex_t *mp)
{
	return (mp->m_owner);
}

/*
 * =========================================================================
 * rwlocks
 * =========================================================================
 */
/*ARGSUSED*/
void
rw_init(krwlock_t *rwlp, char *name, int type, void *arg)
{
	rwlock_init(&rwlp->rw_lock, USYNC_THREAD, NULL);
	rwlp->rw_owner = NULL;
}

void
rw_destroy(krwlock_t *rwlp)
{
	rwlock_destroy(&rwlp->rw_lock);
	rwlp->rw_owner = (void *)-1UL;
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	ASSERT(!RW_LOCK_HELD(rwlp));
	ASSERT(rwlp->rw_owner != (void *)-1UL);
	ASSERT(rwlp->rw_owner != curthread);

	if (rw == RW_READER)
		(void) rw_rdlock(&rwlp->rw_lock);
	else
		(void) rw_wrlock(&rwlp->rw_lock);

	rwlp->rw_owner = curthread;
}

void
rw_exit(krwlock_t *rwlp)
{
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	rwlp->rw_owner = NULL;
	(void) rw_unlock(&rwlp->rw_lock);
}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int rv;

	ASSERT(rwlp->rw_owner != (void *)-1UL);

	if (rw == RW_READER)
		rv = rw_tryrdlock(&rwlp->rw_lock);
	else
		rv = rw_trywrlock(&rwlp->rw_lock);

	if (rv == 0) {
		rwlp->rw_owner = curthread;
		return (1);
	}

	return (0);
}

/*ARGSUSED*/
int
rw_tryupgrade(krwlock_t *rwlp)
{
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	return (0);
}

/*
 * =========================================================================
 * condition variables
 * =========================================================================
 */
/*ARGSUSED*/
void
cv_init(kcondvar_t *cv, char *name, int type, void *arg)
{
	(void) cond_init(cv, type, NULL);
}

void
cv_destroy(kcondvar_t *cv)
{
	(void) cond_destroy(cv);
}

void
cv_wait(kcondvar_t *cv, kmutex_t *mp)
{
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	(void) cond_wait(cv, &mp->m_lock);
	mp->m_owner = curthread;
}

clock_t
cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime)
{
	int error;
	timestruc_t ts;
	clock_t delta;

top:
	delta = abstime - lbolt;
	if (delta <= 0)
		return (-1);

	ts.tv_sec = delta / hz;
	ts.tv_nsec = (delta % hz) * (NANOSEC / hz);

	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	error = cond_reltimedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = curthread;

	if (error == ETIME)
		return (-1);

	if (error == EINTR)
		goto top;

	ASSERT(error == 0);

	return (1);
}

void
cv_signal(kcondvar_t *cv)
{
	(void) cond_signal(cv);
}

void
cv_broadcast(kcondvar_t *cv)
{
	(void) cond_broadcast(cv);
}

/*
 * =========================================================================
 * vnode operations
 * =========================================================================
 */
/*
 * Note: for the xxxat() versions of these functions, we assume that the
 * starting vp is always rootdir (which is true for spa_directory.c, the only
 * ZFS consumer of these interfaces).  We assert this is true, and then emulate
 * them by adding '/' in front of the path.
 */

/*ARGSUSED*/
int
vn_open(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2, int x3)
{
	int fd;
	vnode_t *vp;
	int old_umask;
	char realpath[MAXPATHLEN];
	struct stat64 st;

	/*
	 * If we're accessing a real disk from userland, we need to use
	 * the character interface to avoid caching.  This is particularly
	 * important if we're trying to look at a real in-kernel storage
	 * pool from userland, e.g. via zdb, because otherwise we won't
	 * see the changes occurring under the segmap cache.
	 * On the other hand, the stupid character device returns zero
	 * for its size.  So -- gag -- we open the block device to get
	 * its size, and remember it for subsequent VOP_GETATTR().
	 */
	if (strncmp(path, "/dev/", 5) == 0) {
		char *dsk;
		fd = open64(path, O_RDONLY);
		if (fd == -1)
			return (errno);
		if (fstat64(fd, &st) == -1) {
			close(fd);
			return (errno);
		}
		close(fd);
		(void) sprintf(realpath, "%s", path);
		dsk = strstr(path, "/dsk/");
		if (dsk != NULL)
			(void) sprintf(realpath + (dsk - path) + 1, "r%s",
			    dsk + 1);
	} else {
		(void) sprintf(realpath, "%s", path);
		if (!(flags & FCREAT) && stat64(realpath, &st) == -1)
			return (errno);
	}

	if (flags & FCREAT)
		old_umask = umask(0);

	/*
	 * The construct 'flags - FREAD' conveniently maps combinations of
	 * FREAD and FWRITE to the corresponding O_RDONLY, O_WRONLY, and O_RDWR.
	 */
	fd = open64(realpath, flags - FREAD, mode);

	if (flags & FCREAT)
		(void) umask(old_umask);

	if (fd == -1)
		return (errno);

	if (fstat64(fd, &st) == -1) {
		close(fd);
		return (errno);
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	*vpp = vp = umem_zalloc(sizeof (vnode_t), UMEM_NOFAIL);

	vp->v_fd = fd;
	vp->v_size = st.st_size;
	vp->v_path = spa_strdup(path);

	return (0);
}

int
vn_openat(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2,
    int x3, vnode_t *startvp)
{
	char *realpath = umem_alloc(strlen(path) + 2, UMEM_NOFAIL);
	int ret;

	ASSERT(startvp == rootdir);
	(void) sprintf(realpath, "/%s", path);

	ret = vn_open(realpath, x1, flags, mode, vpp, x2, x3);

	umem_free(realpath, strlen(path) + 2);

	return (ret);
}

/*ARGSUSED*/
int
vn_rdwr(int uio, vnode_t *vp, void *addr, ssize_t len, offset_t offset,
	int x1, int x2, rlim64_t x3, void *x4, ssize_t *residp)
{
	ssize_t iolen, split;

	if (uio == UIO_READ) {
		iolen = pread64(vp->v_fd, addr, len, offset);
	} else {
		/*
		 * To simulate partial disk writes, we split writes into two
		 * system calls so that the process can be killed in between.
		 */
		split = (len > 0 ? rand() % len : 0);
		iolen = pwrite64(vp->v_fd, addr, split, offset);
		iolen += pwrite64(vp->v_fd, (char *)addr + split,
		    len - split, offset + split);
	}

	if (iolen == -1)
		return (errno);
	if (residp)
		*residp = len - iolen;
	else if (iolen != len)
		return (EIO);
	return (0);
}

void
vn_close(vnode_t *vp)
{
	close(vp->v_fd);
	spa_strfree(vp->v_path);
	umem_free(vp, sizeof (vnode_t));
}

#ifdef ZFS_DEBUG

/*
 * =========================================================================
 * Figure out which debugging statements to print
 * =========================================================================
 */

static char *dprintf_string;
static int dprintf_print_all;

int
dprintf_find_string(const char *string)
{
	char *tmp_str = dprintf_string;
	int len = strlen(string);

	/*
	 * Find out if this is a string we want to print.
	 * String format: file1.c,function_name1,file2.c,file3.c
	 */

	while (tmp_str != NULL) {
		if (strncmp(tmp_str, string, len) == 0 &&
		    (tmp_str[len] == ',' || tmp_str[len] == '\0'))
			return (1);
		tmp_str = strchr(tmp_str, ',');
		if (tmp_str != NULL)
			tmp_str++; /* Get rid of , */
	}
	return (0);
}

void
dprintf_setup(int *argc, char **argv)
{
	int i, j;

	/*
	 * Debugging can be specified two ways: by setting the
	 * environment variable ZFS_DEBUG, or by including a
	 * "debug=..."  argument on the command line.  The command
	 * line setting overrides the environment variable.
	 */

	for (i = 1; i < *argc; i++) {
		int len = strlen("debug=");
		/* First look for a command line argument */
		if (strncmp("debug=", argv[i], len) == 0) {
			dprintf_string = argv[i] + len;
			/* Remove from args */
			for (j = i; j < *argc; j++)
				argv[j] = argv[j+1];
			argv[j] = NULL;
			(*argc)--;
		}
	}

	if (dprintf_string == NULL) {
		/* Look for ZFS_DEBUG environment variable */
		dprintf_string = getenv("ZFS_DEBUG");
	}

	/*
	 * Are we just turning on all debugging?
	 */
	if (dprintf_find_string("on"))
		dprintf_print_all = 1;
}

/*
 * =========================================================================
 * debug printfs
 * =========================================================================
 */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	va_list adx;

	/*
	 * Get rid of annoying "../common/" prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	if (dprintf_print_all ||
	    dprintf_find_string(newfile) ||
	    dprintf_find_string(func)) {
		/* Print out just the function name if requested */
		flockfile(stdout);
		if (dprintf_find_string("pid"))
			(void) printf("%d ", getpid());
		if (dprintf_find_string("tid"))
			(void) printf("%u ", thr_self());
		if (dprintf_find_string("cpu"))
			(void) printf("%u ", getcpuid());
		if (dprintf_find_string("time"))
			(void) printf("%llu ", gethrtime());
		if (dprintf_find_string("long"))
			(void) printf("%s, line %d: ", newfile, line);
		(void) printf("%s: ", func);
		va_start(adx, fmt);
		(void) vprintf(fmt, adx);
		va_end(adx);
		funlockfile(stdout);
	}
}

#endif /* ZFS_DEBUG */

/*
 * =========================================================================
 * cmn_err() and panic()
 * =========================================================================
 */
static char ce_prefix[CE_IGNORE][10] = { "", "NOTICE: ", "WARNING: ", "" };
static char ce_suffix[CE_IGNORE][2] = { "", "\n", "\n", "" };

void
vpanic(const char *fmt, va_list adx)
{
	(void) fprintf(stderr, "error: ");
	(void) vfprintf(stderr, fmt, adx);
	(void) fprintf(stderr, "\n");

	abort();	/* think of it as a "user-level crash dump" */
}

void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vpanic(fmt, adx);
	va_end(adx);
}

/*PRINTFLIKE2*/
void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	if (ce == CE_PANIC)
		vpanic(fmt, adx);
	if (ce != CE_NOTE) {	/* suppress noise in userland stress testing */
		(void) fprintf(stderr, "%s", ce_prefix[ce]);
		(void) vfprintf(stderr, fmt, adx);
		(void) fprintf(stderr, "%s", ce_suffix[ce]);
	}
	va_end(adx);
}

/*
 * =========================================================================
 * kobj interfaces
 * =========================================================================
 */
struct _buf *
kobj_open_file(char *name)
{
	struct _buf *file;
	vnode_t *vp;

	/* set vp as the _fd field of the file */
	if (vn_openat(name, UIO_SYSSPACE, FREAD, 0, &vp, 0, 0, rootdir) != 0)
		return ((void *)-1UL);

	file = umem_zalloc(sizeof (struct _buf), UMEM_NOFAIL);
	file->_fd = (intptr_t)vp;
	return (file);
}

int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	ssize_t resid;

	vn_rdwr(UIO_READ, (vnode_t *)file->_fd, buf, size, (offset_t)off,
	    UIO_SYSSPACE, 0, 0, 0, &resid);

	return (0);
}

void
kobj_close_file(struct _buf *file)
{
	vn_close((vnode_t *)file->_fd);
	umem_free(file, sizeof (struct _buf));
}

int
kobj_fstat(intptr_t fd, struct bootstat *bst)
{
	struct stat64 st;
	vnode_t *vp = (vnode_t *)fd;
	if (fstat64(vp->v_fd, &st) == -1) {
		vn_close(vp);
		return (errno);
	}
	bst->st_size = (uint64_t)st.st_size;
	return (0);
}

/*
 * =========================================================================
 * misc routines
 * =========================================================================
 */

void
delay(clock_t ticks)
{
	poll(0, 0, ticks * (1000 / hz));
}

/*
 * Find highest one bit set.
 *	Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * High order bit is 31 (or 63 in _LP64 kernel).
 */
int
highbit(ulong_t i)
{
	register int h = 1;

	if (i == 0)
		return (0);
#ifdef _LP64
	if (i & 0xffffffff00000000ul) {
		h += 32; i >>= 32;
	}
#endif
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}

static int
random_get_bytes_common(uint8_t *ptr, size_t len, char *devname)
{
	int fd = open(devname, O_RDONLY);
	size_t resid = len;
	ssize_t bytes;

	ASSERT(fd != -1);

	while (resid != 0) {
		bytes = read(fd, ptr, resid);
		ASSERT(bytes >= 0);
		ptr += bytes;
		resid -= bytes;
	}

	close(fd);

	return (0);
}

int
random_get_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, "/dev/random"));
}

int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, "/dev/urandom"));
}

/*
 * =========================================================================
 * kernel emulation setup & teardown
 * =========================================================================
 */
static int
umem_out_of_memory(void)
{
	char errmsg[] = "out of memory -- generating core dump\n";

	write(fileno(stderr), errmsg, sizeof (errmsg));
	abort();
	return (0);
}

void
kernel_init(int mode)
{
	umem_nofail_callback(umem_out_of_memory);

	physmem = sysconf(_SC_PHYS_PAGES);

	dprintf("physmem = %llu pages (%.2f GB)\n", physmem,
	    (double)physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));

	spa_init(mode);
}

void
kernel_fini(void)
{
	spa_fini();
}
