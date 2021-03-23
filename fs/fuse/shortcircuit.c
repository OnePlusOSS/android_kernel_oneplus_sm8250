// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "fuse_i.h"

#include <linux/file.h>
#include <linux/aio.h>
#include <linux/fs_stack.h>
#include <linux/uio.h>

#include <linux/moduleparam.h>

int __read_mostly sct_mode = 2;
module_param(sct_mode, int, 0644);

static char *__dentry_name(struct dentry *dentry, char *name)
{
	char *p = dentry_path_raw(dentry, name, PATH_MAX);

	if (IS_ERR(p)) {
		__putname(name);
		return NULL;
	}

	/*
	 * This function relies on the fact that dentry_path_raw() will place
	 * the path name at the end of the provided buffer.
	 */
	BUG_ON(p + strlen(p) + 1 != name + PATH_MAX);

	if (p > name)
		strlcpy(name, p, PATH_MAX);

	return name;
}

static char *dentry_name(struct dentry *dentry)
{
	char *name = __getname();

	if (!name)
		return NULL;

	return __dentry_name(dentry, name);
}

char *inode_name(struct inode *ino)
{
	struct dentry *dentry;
	char *name;

	if (sct_mode != 1)
		return NULL;

	dentry = d_find_alias(ino);
	if (!dentry)
		dentry = d_find_any_alias(ino);

	if (!dentry)
		return NULL;

	name = dentry_name(dentry);

	dput(dentry);

	return name;
}

int fuse_shortcircuit_setup(struct fuse_conn *fc, struct fuse_req *req)
{
	int  fd, flags;
	struct fuse_open_out *open_out;
	struct file *lower_filp;
	unsigned short open_out_index;
	struct fuse_package *fp = current->fpack;

	req->sct.filp = NULL;

	if (!fc->shortcircuit)
		return 0;

	if (!sct_mode)
		return 0;

	if ((req->in.h.opcode != FUSE_OPEN) &&
		(req->in.h.opcode != FUSE_CREATE))
		return 0;

	open_out_index = req->in.numargs - 1;

	if ((open_out_index != 0 && open_out_index != 1) ||
		(req->out.args[open_out_index].size != sizeof(*open_out)))
		return 0;

	open_out = req->out.args[open_out_index].value;
	if (!open_out->fh)
		return 0;

	flags = open_out->open_flags;
	if ((flags & FOPEN_DIRECT_IO) || !(flags & FOPEN_KEEP_CACHE)) {
		pr_info("fuse: bypass sct #flags:%d\n", flags);
		return 0;
	}

	if (sct_mode == 1) {
		if (fp) {
			req->sct.filp = fp->filp;
			req->sct.cred = prepare_creds();
			fp->filp = NULL;
		}
		return 0;
	}

	if (fp && fp->filp) {
		fput(fp->filp);
		fp->filp = NULL;
	}

	if (get_user(fd, (int __user *)open_out->fh))
		return -EINVAL;

	if (fd <= 1 || fd >= current->signal->rlim[RLIMIT_NOFILE].rlim_max) {
		pr_info("fuse: bypass sct:%d, %d\n", fd, flags);
		return -EINVAL;
	}

	lower_filp = fget(fd);
	if (!lower_filp) {
		pr_err("fuse: invalid file descriptor for sct.\n");
		return -EINVAL;
	}

	if (!lower_filp->f_op->read_iter ||
	    !lower_filp->f_op->write_iter) {
		pr_err("fuse: sct file misses file operations.\n");
		fput(lower_filp);
		return -EINVAL;
	}

	req->sct.filp = lower_filp;
	req->sct.cred = prepare_creds();
	pr_info("fuse: setup sct:%d, %d\n", fd, flags);
	return 0;
}

struct fuse_aio_req {
	struct kiocb iocb;
	struct kiocb *iocb_fuse;
};

static void fuse_copyattr(struct file *dst_file, struct file *src_file)
{
	struct inode *dst = file_inode(dst_file);
	struct inode *src = file_inode(src_file);

	i_size_write(dst, i_size_read(src));
}

static inline rwf_t iocb_to_rw_flags(int ifl)
{
	rwf_t flags = 0;

	if (ifl & IOCB_APPEND)
		flags |= RWF_APPEND;
	if (ifl & IOCB_DSYNC)
		flags |= RWF_DSYNC;
	if (ifl & IOCB_HIPRI)
		flags |= RWF_HIPRI;
	if (ifl & IOCB_NOWAIT)
		flags |= RWF_NOWAIT;
	if (ifl & IOCB_SYNC)
		flags |= RWF_SYNC;

	return flags;
}

static void fuse_aio_cleanup_handler(struct fuse_aio_req *aio_req)
{
	struct kiocb *iocb = &aio_req->iocb;
	struct kiocb *iocb_fuse = aio_req->iocb_fuse;

	if (iocb->ki_flags & IOCB_WRITE) {
		__sb_writers_acquired(file_inode(iocb->ki_filp)->i_sb,
				      SB_FREEZE_WRITE);
		file_end_write(iocb->ki_filp);
		fuse_copyattr(iocb_fuse->ki_filp, iocb->ki_filp);
	}

	iocb_fuse->ki_pos = iocb->ki_pos;
	kfree(aio_req);
}

static void fuse_aio_rw_complete(struct kiocb *iocb, long res, long res2)
{
	struct fuse_aio_req *aio_req =
		container_of(iocb, struct fuse_aio_req, iocb);
	struct kiocb *iocb_fuse = aio_req->iocb_fuse;

	fuse_aio_cleanup_handler(aio_req);
	iocb_fuse->ki_complete(iocb_fuse, res, res2);
}

static inline void kiocb_clone(struct kiocb *kiocb, struct kiocb *kiocb_src,
			       struct file *filp)
{
	*kiocb = (struct kiocb){
		.ki_filp = filp,
		.ki_flags = kiocb_src->ki_flags,
		.ki_hint = kiocb_src->ki_hint,
		.ki_ioprio = kiocb_src->ki_ioprio,
		.ki_pos = kiocb_src->ki_pos,
	};
}

ssize_t fuse_shortcircuit_read_iter(struct kiocb *iocb_fuse,
				   struct iov_iter *iter)
{
	ssize_t ret;
	const struct cred *old_cred;
	struct file *fuse_filp = iocb_fuse->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct file *lower_filp = ff->sct.filp;

	if (!iov_iter_count(iter))
		return 0;

	old_cred = override_creds(ff->sct.cred);
	if (is_sync_kiocb(iocb_fuse)) {
		ret = vfs_iter_read(lower_filp, iter, &iocb_fuse->ki_pos,
				    iocb_to_rw_flags(iocb_fuse->ki_flags));
	} else {
		struct fuse_aio_req *aio_req;

		aio_req = kmalloc(sizeof(struct fuse_aio_req), GFP_KERNEL);
		if (!aio_req)
			return -ENOMEM;

		aio_req->iocb_fuse = iocb_fuse;
		kiocb_clone(&aio_req->iocb, iocb_fuse, lower_filp);
		aio_req->iocb.ki_complete = fuse_aio_rw_complete;
		ret = call_read_iter(lower_filp, &aio_req->iocb, iter);
		if (ret != -EIOCBQUEUED)
			fuse_aio_cleanup_handler(aio_req);
	}
	revert_creds(old_cred);
	return ret;
}

ssize_t fuse_shortcircuit_write_iter(struct kiocb *iocb_fuse,
				    struct iov_iter *iter)
{
	ssize_t ret;
	const struct cred *old_cred;
	struct file *fuse_filp = iocb_fuse->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct inode *fuse_inode = file_inode(fuse_filp);
	struct file *lower_filp = ff->sct.filp;
	struct inode *lower_inode = file_inode(lower_filp);

	if (!iov_iter_count(iter))
		return 0;

	inode_lock(fuse_inode);
	old_cred = override_creds(ff->sct.cred);
	if (is_sync_kiocb(iocb_fuse)) {
		file_start_write(lower_filp);
		ret = vfs_iter_write(lower_filp, iter, &iocb_fuse->ki_pos,
				     iocb_to_rw_flags(iocb_fuse->ki_flags));
		file_end_write(lower_filp);
		if (ret > 0)
			fuse_copyattr(fuse_filp, lower_filp);
	} else {
		struct fuse_aio_req *aio_req;

		aio_req = kmalloc(sizeof(struct fuse_aio_req), GFP_KERNEL);
		if (!aio_req) {
			ret = -ENOMEM;
			goto out;
		}

		file_start_write(lower_filp);
		__sb_writers_release(lower_inode->i_sb, SB_FREEZE_WRITE);

		aio_req->iocb_fuse = iocb_fuse;
		kiocb_clone(&aio_req->iocb, iocb_fuse, lower_filp);
		aio_req->iocb.ki_complete = fuse_aio_rw_complete;
		ret = call_write_iter(lower_filp, &aio_req->iocb, iter);
		if (ret != -EIOCBQUEUED)
			fuse_aio_cleanup_handler(aio_req);
	}
out:
	revert_creds(old_cred);
	inode_unlock(fuse_inode);

	return ret;
}

ssize_t fuse_shortcircuit_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	const struct cred *old_cred;
	struct fuse_file *ff = file->private_data;
	struct file *lower_filp = ff->sct.filp;
	struct inode *fuse_inode = file_inode(file);
	struct inode *lower_inode = file_inode(lower_filp);

	if (!lower_filp->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	vma->vm_file = get_file(lower_filp);
	old_cred = override_creds(ff->sct.cred);
	ret = call_mmap(vma->vm_file, vma);
	revert_creds(old_cred);

	if (ret)
		fput(lower_filp);
	else
		fput(file);

	if (lower_inode && !(file->f_flags & O_NOATIME)) {
		if ((!timespec64_equal(&fuse_inode->i_mtime, &lower_inode->i_mtime) ||
		     !timespec64_equal(&fuse_inode->i_ctime, &lower_inode->i_ctime))) {
			fuse_inode->i_mtime = lower_inode->i_mtime;
			fuse_inode->i_ctime = lower_inode->i_ctime;
		}
		touch_atime(&file->f_path);
	}

	return ret;
}

void fuse_shortcircuit_release(struct fuse_file *ff)
{
	if (ff->sct.filp) {
		fput(ff->sct.filp);
		ff->sct.filp = NULL;
	}
	if (ff->sct.cred) {
		put_cred(ff->sct.cred);
		ff->sct.cred = NULL;
	}
}
