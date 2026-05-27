// SPDX-License-Identifier: GPL-2.0

#include <linux/version.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/thread_info.h>
#include <linux/dirent.h>
#include <linux/if.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sockios.h>
#include "tango32.h"

static bool is_32bit(void)
{
	return test_thread_flag(TIF_32BIT);
}

static void set_32bit(bool val)
{
	if (val)
		set_thread_flag(TIF_32BIT);
	else
		clear_thread_flag(TIF_32BIT);
	/*
	 * Ensure the flag change is visible before any subsequent
	 * operations that depend on it (e.g., in_compat_syscall).
	 */
	smp_mb();
}

static long tango32_get_version(struct tango32_abi_version __user *argp)
{
	bool compat;
	struct tango32_abi_version abi = { .major = TANGO32_ABI_MAJOR,
					   .minor = TANGO32_ABI_MINOR };

	if (!access_ok(argp, sizeof(abi)))
		return -EFAULT;

	/*
	 * Sanity check: ensure that setting TIF_32BIT properly results in
	 * in_compat_syscall() returning true.
	 */
	set_32bit(true);
	compat = in_compat_syscall();
	set_32bit(false);

	if (!compat)
		return -EIO;

	if (copy_to_user(argp, &abi, sizeof(abi)))
		return -EFAULT;

	return 0;
}

/*
* Based on validate_prctl_map_addr in kernel/sys.c.
*/
static int validate_mm_fields(const struct tango32_mm *mm_fields)
{
	unsigned long mmap_max_addr = TASK_SIZE;
	/*
	 * mmap_min_addr is not exported to modules. However this isn't a big
	 * issue here since these values are mostly informational. The only one
	 * which is actually used for VM allocations is brk but this is still
	 * checked again mmap_min_addr in the brk syscall.
	 */
	unsigned long mmap_min_addr = PAGE_SIZE;
	long retval = -EINVAL, i;

	static const unsigned char offsets[] = {
		offsetof(struct tango32_mm, start_code),
		offsetof(struct tango32_mm, end_code),
		offsetof(struct tango32_mm, start_data),
		offsetof(struct tango32_mm, end_data),
		offsetof(struct tango32_mm, start_brk),
		offsetof(struct tango32_mm, brk),
		offsetof(struct tango32_mm, start_stack),
		offsetof(struct tango32_mm, arg_start),
		offsetof(struct tango32_mm, arg_end),
		offsetof(struct tango32_mm, env_start),
		offsetof(struct tango32_mm, env_end),
	};

	/*
	 * Make sure the members are not somewhere outside
	 * of allowed address space.
	 */
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		u64 val = *(u64 *)((char *)mm_fields + offsets[i]);

		if ((unsigned long)val >= mmap_max_addr ||
		    (unsigned long)val < mmap_min_addr)
			goto out;
	}

	/*
	 * Make sure the pairs are ordered.
	 */
#define __prctl_check_order(__m1, __op, __m2)                                  \
	((unsigned long)mm_fields->__m1 __op(unsigned long) mm_fields->__m2) ? \
		0 :                                                            \
		-EINVAL
	retval = __prctl_check_order(start_code, <, end_code);
	retval |= __prctl_check_order(start_data, <=, end_data);
	retval |= __prctl_check_order(start_brk, <=, brk);
	retval |= __prctl_check_order(arg_start, <=, arg_end);
	retval |= __prctl_check_order(env_start, <=, env_end);
	if (retval)
		goto out;
#undef __prctl_check_order

	retval = -EINVAL;

	/*
	 * Neither we should allow to override limits if they set.
	 */
	if (check_data_rlimit(rlimit(RLIMIT_DATA), mm_fields->brk,
			      mm_fields->start_brk, mm_fields->end_data,
			      mm_fields->start_data))
		goto out;

	retval = 0;
out:
	return retval;
}

static long tango32_set_mm(struct tango32_mm __user *argp)
{
	struct mm_struct *mm = current->mm;
	unsigned long user_auxv[AT_VECTOR_SIZE];
	struct tango32_mm mm_fields;
	long retval;

	BUILD_BUG_ON(sizeof(user_auxv) != sizeof(mm->saved_auxv));

	if (!access_ok(argp, sizeof(mm_fields)))
		return -EFAULT;

	if (copy_from_user(&mm_fields, argp, sizeof(mm_fields)))
		return -EFAULT;

	/*
	 * Sanity-check the input values.
	 */
	retval = validate_mm_fields(&mm_fields);
	if (retval)
		return retval;

	if (mm_fields.auxv_size) {
		/*
		 * Someone is trying to cheat the auxv vector.
		 */
		if (!mm_fields.auxv ||
		    mm_fields.auxv_size > sizeof(mm->saved_auxv))
			return -EINVAL;

		memset(user_auxv, 0, sizeof(user_auxv));
		if (copy_from_user(user_auxv,
				   (const void __user *)mm_fields.auxv,
				   mm_fields.auxv_size))
			return -EFAULT;

		/* Last entry must be AT_NULL as specification requires */
		user_auxv[AT_VECTOR_SIZE - 2] = AT_NULL;
		user_auxv[AT_VECTOR_SIZE - 1] = AT_NULL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	if (mmap_read_lock_killable(mm))
		return -EINTR;
#else
	if (down_read_killable(&mm->mmap_sem))
		return -EINTR;
#endif

	spin_lock(&mm->arg_lock);
	mm->start_code = mm_fields.start_code;
	mm->end_code = mm_fields.end_code;
	mm->start_data = mm_fields.start_data;
	mm->end_data = mm_fields.end_data;
	mm->start_brk = mm_fields.start_brk;
	mm->brk = mm_fields.brk;
	mm->start_stack = mm_fields.start_stack;
	mm->arg_start = mm_fields.arg_start;
	mm->arg_end = mm_fields.arg_end;
	mm->env_start = mm_fields.env_start;
	mm->env_end = mm_fields.env_end;
	spin_unlock(&mm->arg_lock);

	if (mm_fields.auxv_size)
		memcpy(mm->saved_auxv, user_auxv, sizeof(user_auxv));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	mmap_read_unlock(mm);
#else
	up_read(&mm->mmap_sem);
#endif

	return retval;
}

static long tango32_set_mmap_base(unsigned long arg)
{
	unsigned long mmap_max_addr = TASK_SIZE;
	/* mmap_min_addr is not exported to modules, pick a safe default. */
	unsigned long mmap_min_addr = 0x10000000;
	struct mm_struct *mm = current->mm;

	if (arg >= mmap_max_addr || arg <= mmap_min_addr)
		return -EINVAL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	if (mmap_write_lock_killable(mm))
		return -EINTR;
#else
	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;
#endif

	mm->mmap_base = arg;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	mmap_write_unlock(mm);
#else
	up_write(&mm->mmap_sem);
#endif

	return 0;
}

/*
 * AviumUI: SIOCGIFCONF for translated 32-bit callers, WITHOUT TIF_32BIT.
 *
 * SIOCGIFCONF is the common socket ioctl whose userspace layout differs
 * between 32- and 64-bit (struct ifconf carries a pointer; struct ifreq is
 * 32 bytes for compat vs sizeof(struct ifreq) native). The kernel's compat
 * path requires in_compat_syscall()==true, i.e. TIF_32BIT set -- and setting
 * that flag on this AArch64-only kernel (no AArch32 EL0) intermittently
 * panics. So translate manually: run the NATIVE ioctl into a scratch user
 * buffer, then repack each native ifreq into the caller's compat ifreq array.
 * The first 32 bytes of a native ifreq (ifr_name[16] + ifr_addr sockaddr) are
 * byte-identical to a compat ifreq, so the repack is a straight 32-byte copy.
 */
#define TANGO32_COMPAT_IFREQ_SZ 32u

struct tango32_compat_ifconf {
	__s32 ifc_len;
	__u32 ifcbuf;
};

static long tango32_getifconf(struct file *file, unsigned long arg32)
{
	struct tango32_compat_ifconf ifc32;
	struct ifconf ifc;
	void __user *uifc32 = (void __user *)arg32;
	unsigned long scratch, map_len, scratch_len;
	long retval;
	u32 cap, n, i;

	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;

	retval = security_file_ioctl(file, SIOCGIFCONF, arg32);
	if (retval)
		return retval;

	if (copy_from_user(&ifc32, uifc32, sizeof(ifc32)))
		return -EFAULT;

	cap = (u32)ifc32.ifc_len / TANGO32_COMPAT_IFREQ_SZ;
	if (cap > 1024)
		cap = 1024; /* sanity clamp */

	/* scratch user buffer: [struct ifconf][cap * native struct ifreq] */
	scratch_len = sizeof(struct ifconf) +
		      (unsigned long)cap * sizeof(struct ifreq);
	map_len = PAGE_ALIGN(scratch_len + PAGE_SIZE);
	scratch = vm_mmap(NULL, 0, map_len, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS, 0);
	if (IS_ERR_VALUE(scratch))
		return -ENOMEM;

	ifc.ifc_len = (int)((unsigned long)cap * sizeof(struct ifreq));
	ifc.ifc_req = (struct ifreq __user *)(scratch + sizeof(struct ifconf));
	if (copy_to_user((void __user *)scratch, &ifc, sizeof(ifc))) {
		retval = -EFAULT;
		goto out;
	}

	/* NATIVE ioctl -- no TIF_32BIT, so no panic. */
	retval = file->f_op->unlocked_ioctl(file, SIOCGIFCONF, scratch);
	if (retval < 0)
		goto out;

	if (copy_from_user(&ifc, (void __user *)scratch, sizeof(ifc))) {
		retval = -EFAULT;
		goto out;
	}
	n = (u32)ifc.ifc_len / (u32)sizeof(struct ifreq);

	for (i = 0; i < n && i < cap; i++) {
		char buf[TANGO32_COMPAT_IFREQ_SZ];
		void __user *src = (void __user *)(scratch +
				sizeof(struct ifconf) +
				(unsigned long)i * sizeof(struct ifreq));
		void __user *dst = (void __user *)(unsigned long)(ifc32.ifcbuf +
				i * TANGO32_COMPAT_IFREQ_SZ);

		if (copy_from_user(buf, src, sizeof(buf)) ||
		    copy_to_user(dst, buf, sizeof(buf))) {
			retval = -EFAULT;
			goto out;
		}
	}

	ifc32.ifc_len = (__s32)(min(n, cap) * TANGO32_COMPAT_IFREQ_SZ);
	if (copy_to_user(uifc32, &ifc32, sizeof(ifc32))) {
		retval = -EFAULT;
		goto out;
	}
	retval = 0;
out:
	vm_munmap(scratch, map_len);
	return retval;
}

static long tango32_compat_ioctl(struct tango32_compat_ioctl __user *argp)
{
	struct tango32_compat_ioctl args;
	struct fd f;
	long retval;

	if (!access_ok(argp, sizeof(args)))
		return -EFAULT;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	f = fdget(args.fd);
	if (!f.file)
		return -EBADF;

	/*
	 * AviumUI: NEVER set TIF_32BIT on this AArch64-only kernel -- doing so
	 * intermittently panics (no AArch32 EL0 hardware; CONFIG_COMPAT is
	 * force-enabled for Tango but the compat-mode machinery is unsafe here).
	 *
	 * SIOCGIFCONF is the one common socket ioctl whose 32/64 layout differs
	 * and genuinely needs translation; handle it manually via the NATIVE
	 * ioctl (see tango32_getifconf). All the other socket ioctls Tango
	 * proxies (SIOCGIFFLAGS/HWADDR/INDEX/NAME/NETMASK/...) are layout-
	 * identical for 32- and 64-bit, so the kernel's ->compat_ioctl handler
	 * translates them correctly even without TIF_32BIT.
	 */
	if (args.cmd == SIOCGIFCONF) {
		retval = tango32_getifconf(f.file, args.arg);
		goto out;
	}

	retval = security_file_ioctl(f.file, args.cmd, args.arg);
	if (retval)
		goto out;

	retval = -ENOIOCTLCMD;
	if (f.file->f_op->compat_ioctl)
		retval = f.file->f_op->compat_ioctl(f.file, args.cmd, args.arg);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#warning "Support for kernels before v5.6 is experimental"
	/*
	 * Older kernels don't provide compat_ioctl on all devices, so as a
	 * workaround we pass the ioctls through unmodified if compat_ioctl is
	 * not provided. This should work for most ioctls.
	 */
	else if (f.file->f_op->unlocked_ioctl)
		retval = f.file->f_op->unlocked_ioctl(f.file, args.cmd,
						      args.arg);
#endif

out:
	fdput(f);

	return retval;
}

static long
tango32_compat_set_robust_list(struct tango32_compat_robust_list __user *argp)
{
	struct tango32_compat_robust_list args;

	if (!access_ok(argp, sizeof(args)))
		return -EFAULT;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (args.len != sizeof(*current->compat_robust_list))
		return -EINVAL;

	current->compat_robust_list = compat_ptr(args.head);

	return 0;
}

static long
tango32_compat_get_robust_list(struct tango32_compat_robust_list __user *argp)
{
	struct tango32_compat_robust_list out;

	if (!access_ok(argp, sizeof(out)))
		return -EFAULT;

	out.head = ptr_to_compat(current->compat_robust_list);
	out.len = sizeof(*current->compat_robust_list);

	if (copy_to_user(argp, &out, sizeof(out)))
		return -EFAULT;

	return 0;
}

/* Copied from fs/readdir.c */
#define unsafe_copy_dirent_name(_dst, _src, _len, label)                       \
	do {                                                                   \
		char __user *dst = (_dst);                                     \
		const char *src = (_src);                                      \
		size_t len = (_len);                                           \
		unsafe_put_user(0, dst + len, label);                          \
		unsafe_copy_to_user(dst, src, len, label);                     \
	} while (0)

/* Copied from fs/readdir.c */
static int verify_dirent_name(const char *name, int len)
{
	if (len <= 0 || len >= PATH_MAX)
		return -EIO;
	if (memchr(name, '/', len))
		return -EIO;
	return 0;
}

/* Copied from fs/readdir.c */
struct getdents_callback64 {
	struct dir_context ctx;
	struct linux_dirent64 __user *current_dir;
	int prev_reclen;
	int count;
	int error;
};

/* Copied from fs/readdir.c */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static bool filldir64(struct dir_context *ctx, const char *name, int namlen,
		     loff_t offset, u64 ino, unsigned int d_type)
{
	struct linux_dirent64 __user *dirent, *prev;
	struct getdents_callback64 *buf =
		container_of(ctx, struct getdents_callback64, ctx);
	int reclen = ALIGN(offsetof(struct linux_dirent64, d_name) + namlen + 1,
		sizeof(u64));
	int prev_reclen;

	buf->error = verify_dirent_name(name, namlen);
	if (unlikely(buf->error))
		return false;
	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return false;
	prev_reclen = buf->prev_reclen;
	if (prev_reclen && signal_pending(current))
		return false;
	dirent = buf->current_dir;
	prev = (void __user *)dirent - prev_reclen;
	if (!user_write_access_begin(prev, reclen + prev_reclen))
		goto efault;

	/* This might be 'dirent->d_off', but if so it will get overwritten */
	unsafe_put_user(offset, &prev->d_off, efault_end);
	unsafe_put_user(ino, &dirent->d_ino, efault_end);
	unsafe_put_user(reclen, &dirent->d_reclen, efault_end);
	unsafe_put_user(d_type, &dirent->d_type, efault_end);
	unsafe_copy_dirent_name(dirent->d_name, name, namlen, efault_end);
	user_write_access_end();

	buf->prev_reclen = reclen;
	buf->current_dir = (void __user *)dirent + reclen;
	buf->count -= reclen;
	return true;

efault_end:
	user_write_access_end();
efault:
	buf->error = -EFAULT;
	return false;
}
#else
static int filldir64(struct dir_context *ctx, const char *name, int namlen,
		     loff_t offset, u64 ino, unsigned int d_type)
{
	struct linux_dirent64 __user *dirent, *prev;
	struct getdents_callback64 *buf =
		container_of(ctx, struct getdents_callback64, ctx);
	int reclen = ALIGN(offsetof(struct linux_dirent64, d_name) + namlen + 1,
			   sizeof(u64));
	int prev_reclen;

	buf->error = verify_dirent_name(name, namlen);
	if (unlikely(buf->error))
		return buf->error;
	buf->error = -EINVAL; /* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	prev_reclen = buf->prev_reclen;
	if (prev_reclen && signal_pending(current))
		return -EINTR;
	dirent = buf->current_dir;
	prev = (void __user *)dirent - prev_reclen;
	if (!user_write_access_begin(prev, reclen + prev_reclen))
		goto efault;

	/* This might be 'dirent->d_off', but if so it will get overwritten */
	unsafe_put_user(offset, &prev->d_off, efault_end);
	unsafe_put_user(ino, &dirent->d_ino, efault_end);
	unsafe_put_user(reclen, &dirent->d_reclen, efault_end);
	unsafe_put_user(d_type, &dirent->d_type, efault_end);
	unsafe_copy_dirent_name(dirent->d_name, name, namlen, efault_end);
	user_write_access_end();

	buf->prev_reclen = reclen;
	buf->current_dir = (void __user *)dirent + reclen;
	buf->count -= reclen;
	return 0;

efault_end:
	user_write_access_end();
efault:
	buf->error = -EFAULT;
	return -EFAULT;
}
#endif

/* fdget_pos and fdput_pos are not exported to modules. */
static struct fd my_fdget_pos(unsigned int fd)
{
	struct fd f = fdget(fd);
	if (f.file && (f.file->f_mode & FMODE_ATOMIC_POS)) {
		if (file_count(f.file) > 1) {
			f.flags |= FDPUT_POS_UNLOCK;
			mutex_lock(&f.file->f_pos_lock);
		}
	}
	return f;
}
static void my_fdput_pos(struct fd fd)
{
	if (fd.flags & FDPUT_POS_UNLOCK)
		mutex_unlock(&fd.file->f_pos_lock);
	fdput(fd);
}

static long
tango32_compat_getdents64(struct tango32_compat_getdents64 __user *argp)
{
	struct tango32_compat_getdents64 args;
	struct fd f;
	struct getdents_callback64 buf = {};
	int error;

	if (!access_ok(argp, sizeof(args)))
		return -EFAULT;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (!access_ok((void __user *)args.dirp, args.count))
		return -EFAULT;

	f = my_fdget_pos(args.fd);
	if (!f.file)
		return -EBADF;

	buf.ctx.actor = filldir64;
	buf.count = args.count;
	buf.current_dir = (struct linux_dirent64 __user *)args.dirp;

	error = iterate_dir(f.file, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	if (buf.prev_reclen) {
		struct linux_dirent64 __user *lastdirent;
		typeof(lastdirent->d_off) d_off = buf.ctx.pos;

		lastdirent = (void __user *)buf.current_dir - buf.prev_reclen;
		if (put_user(d_off, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = args.count - buf.count;
	}

	my_fdput_pos(f);
	return error;
}

static long tango32_compat_lseek(struct tango32_compat_lseek __user *argp)
{
	struct tango32_compat_lseek args;
	int retval;
	struct fd f;
	loff_t offset;

	if (!access_ok(argp, sizeof(args)))
		return -EFAULT;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	f = my_fdget_pos(args.fd);
	if (!f.file)
		return -EBADF;

	retval = -EINVAL;
	if (args.whence > SEEK_MAX)
		goto out_putf;

	offset = vfs_llseek(f.file, args.offset, args.whence);

	retval = (int)offset;
	if (offset >= 0) {
		retval = -EFAULT;
		args.result = offset;
		if (!copy_to_user(argp, &args, sizeof(args)))
			retval = 0;
	}

out_putf:
	my_fdput_pos(f);
	return retval;
}

static long tango32_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long ret;

	/*
	 * Sanity check: we should only get called from 64-bit processes.
	 */
	if (is_32bit())
		return -EINVAL;

	switch (cmd) {
	case TANGO32_GET_VERSION:
		ret = tango32_get_version(argp);
		break;
	case TANGO32_SET_MM:
		ret = tango32_set_mm(argp);
		break;
	case TANGO32_SET_MMAP_BASE:
		ret = tango32_set_mmap_base(arg);
		break;
	case TANGO32_COMPAT_IOCTL:
		ret = tango32_compat_ioctl(argp);
		break;
	case TANGO32_COMPAT_SET_ROBUST_LIST:
		ret = tango32_compat_set_robust_list(argp);
		break;
	case TANGO32_COMPAT_GET_ROBUST_LIST:
		ret = tango32_compat_get_robust_list(argp);
		break;
	case TANGO32_COMPAT_GETDENTS64:
		ret = tango32_compat_getdents64(argp);
		break;
	case TANGO32_COMPAT_LSEEK:
		ret = tango32_compat_lseek(argp);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

static const struct file_operations tango32_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = tango32_ioctl,
};

static struct miscdevice tango32_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tango32",
	.fops = &tango32_fops,
	.mode = 0666,
};

module_misc_device(tango32_device);

MODULE_DESCRIPTION("32-bit syscall support for the Tango binary translator");
MODULE_AUTHOR("Amanieu d'Antras");
MODULE_LICENSE("GPL");
