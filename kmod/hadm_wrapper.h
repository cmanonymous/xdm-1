/*
 * This file is taken from the old hadm source code
 *
 * The origin file located in: hadm/kmod/hadm_wrapper.h
 */

#ifndef HADM_WRAPPER_H
#define HADM_WRAPPER_H

#include <linux/blkdev.h>

/*
 * see kernel 2.6.37,
 *
 *     d4d7762 block: clean up blkdev_get() wrappers and their users
 *     e525fd8 block: make blkdev_get/put() handle exclusive access
 *
 * and kernel 2.6.28
 *
 *     30c40d2 [PATCH] propagate mode through open_bdev_excl/close_bdev_excl
 *
 * Also note that there is no FMODE_EXCL before
 *
 *     86d434d [PATCH] eliminate use of ->f_flags in block methods
 */

#ifdef COMPAT_HAVE_BLKDEV_GET_BY_PATH
static inline struct block_device *blkdev_get_by_path(const char *path, fmode_t mode,void *holder)
{
	struct block_device *bdev;
	int err;

	bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return bdev;

	err = blkdev_get(bdev, mode);
	if (err)
		return ERR_PTR(err);

	if ((mode & FMODE_WRITE) && bdev_read_only(bdev)) {
		blkdev_put(bdev, mode);
		return ERR_PTR(-EACCES);
	}

	return bdev;
}
#endif	/* COMPAT_HAVE_BLKDEV_GET_BY_PATH */

/*
 * in Commit 5a7bbad27a410350e64a2d7f5ec18fc73836c14f (between Linux-3.1 and
 * 3.2) make_request() becomes type void. Before it had type int.
 */

#ifdef COMPAT_HAVE_VOID_MAKE_REQUEST
#  define MAKE_REQUEST_TYPE void
#  define MAKE_REQUEST_RETURN(a) return
#else
#  define MAKE_REQUEST_TYPE int
#  define MAKE_REQUEST_RETURN(a) return a
#endif	/* COMPAT_HAVE_VOID_MAKE_REQUEST */

/*
 * PTR_RET was first appear in fa9ee9c4b9885dfdf8eccac19b8b4fc8a7c53288, version
 * is 2.6.38
 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38)
static inline int __must_check PTR_RET(const void *ptr)
{
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	else
		return 0;
}
#endif  /* PTR_RET */

#endif	/* HADM_WRAPPER_H */
