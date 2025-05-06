/*
 * mrp-psnet
 * reverse-engineered from:
 * 	/usr/local/sce/driver/psnet/2.0.36/mrp.o
 * 	md5sum: 77187b9f4c836004398021a7ea32cd97
 */

#include <asm/byteorder.h>
#include <linux/bios32.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/ps2mrp-psnet.h>

/*
 * .data
 *
 */

int mrp_debug = 99;

#ifdef CONFIG_PROC_FS
#define MRP_PROCFILE_NAME "mrp"
#define MRP_PROCFILE_NAME_LENGTH 3
static struct proc_dir_entry mrp_proc_de = {
	0, MRP_PROCFILE_NAME_LENGTH, MRP_PROCFILE_NAME,
	S_IFREG | S_IRUSR | S_IRGRP | S_IROTH,
	1, 0, 0, 0,
	NULL,
	mrp_get_info
};
#endif /* PROC_FS */

static struct file_operations mrp_fops = {
	.read		= mrp_read,
	.write		= mrp_write,
	.select		= mrp_select,
	.ioctl		= mrp_ioctl,
	.open		= mrp_open,
	.release	= mrp_release
};

static unsigned int mrp_major = 0;

/*
 * .bss
 *
 */

struct mrp_unit mrp_units[4];

/*
 * .text
 *
 */

/* Compatibility Linux-2.0.X <-> Linux-2.1.X */
/* Shamelessly stolen from include/linux/isdnif.h */

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif
#if (LINUX_VERSION_CODE < 0x020100)
#include <linux/mm.h>

static inline unsigned long copy_from_user(void *to, const void *from, unsigned long n)
{
        int i;
	dprintk("copy_from_user: to %ph, from %ph, n %lxh\n", to, from, n);
        if ((i = verify_area(VERIFY_READ, from, n)) != 0)
                return i;
        memcpy_fromfs(to, from, n);
	dprintk("copy_from_user returning 0");
        return 0;
}

static inline unsigned long copy_to_user(void *to, const void *from, unsigned long n)
{
        int i;
	dprintk("copy_to_user: to %ph, from %ph, n %lxh\n", to, from, n);
        if ((i = verify_area(VERIFY_WRITE, to, n)) != 0)
                return i;
        memcpy_tofs(to, from, n);
	dprintk("copy_to_user returning 0");
        return 0;
}
#endif

static void mrp_dump_regs(struct mrpregs *regs)
{
	dprintk("mrp_dump_regs, regs %ph\n", regs);
	if (mrp_debug >= 3) {
		printk("* BID=%04x RST=%04x CPS=%04x CPR=%04x\n",
			regs->bid,
			regs->rst,
			regs->cps,
			regs->cpr);
		printk("* FST=%04x TXC=%04x RXC=%04x F1C=%04x\n",
			regs->fst,
			regs->txc,
			regs->rxc,
			regs->f1c);
		printk("* IST=%04x ISP=%04x IER=%04x CSI=%04x\n",
			regs->ist,
			regs->isp,
			regs->ier,
			regs->csi);
		printk("* FSI=%04x AEO=%04x AFO=%04x\n",
			regs->fsi,
			regs->aeo,
			regs->afo);
	}
	dprintk("mrp_dump_regs returning void\n");
}

static int mrp_send(struct mrp_unit *mrp)
{
	int rc;
	mrp_printk(2, "mrp_send: slen=%d\n", mrp->slen);

	rc = mrp_put_fifo(mrp, mrp->deci_out, mrp->slen);
	dprintk("mrp_send returning %d\n", rc);
	return rc;
}

static int mrp_recv(struct mrp_unit *mrp)
{
	__label__ out_error;
	struct decihdr_s *hdr;
	
	mrp_printk(2, "mrp_recv:\n");
	if (!(mrp->flags & MRPF_RDONE)) {
		return 0;
	}
	if (mrp->regs->fst & MRP_FSTF_0001) {
		mrp_printk(1, "mrp_recv: invalid fifo status (%04x)\n", mrp->regs->fst);
		mrp->regs->rxc = 1;
		goto out_error;
	}

	do {
		unsigned short fst;
		mrp->deci_in = hdr = (struct decihdr_s *) mrp->recvbuf;	// woof!

		mrp_get_fifo(mrp, mrp->recvbuf, 4);
		fst = mrp->regs->fst;
		if ((fst & MRP_FSTF_0010) == 0) {
			mrp->rlen = hdr->size;
			mrp->deci_in += 4;

			if ( (hdr->magic != DECI_MAGIC) || (hdr->size > MRP_MAX_PKT_SIZE) ) {
				if ((fst & MRP_FSTF_0001) != 0) {
					mrp_printk(1, "mrp_recv: bad mag/len (%04x/%04x)\n",
						hdr->magic,
						hdr->size
					);
					mrp->regs->rxc = 1;
					goto out_error;
				}
				mrp_printk(1, "mrp_recv: rescanning (%04x/%04x)\n",
					hdr->magic,
					hdr->size
				);
			}
		} else {
			/* (fst & MRP_FSTF_0010) != 0 */
			mrp_printk(1, "mrp_recv: invalid fifo status (%04x)\n", fst);
			mrp->regs->rxc = 1;
			goto out_error;
		}
	} while ( (hdr->magic != DECI_MAGIC) || ((hdr->size - sizeof(*hdr)) > (MRP_MAX_PKT_SIZE - sizeof(*hdr))) );

	mrp_get_fifo(mrp, mrp->deci_in, mrp->rlen - 4);
	if (hdr->cksum == deci_cksum((unsigned int *) hdr)) {
		mrp_printk(2, "mrp_recv: len=%d\n", mrp->rlen);
		mrp->flags |= MRPF_RDONE;
		wake_up(&mrp->wake_queue4);
	} else {
		mrp_printk(1, "mrp_recv: bad sum\n");
		mrp->regs->ier |= MRP_STATF_RECV;
	}
	
	dprintk("mrp_recv returning 1\n");
	return 1;

out_error:
	mrp->regs->ier |= MRP_STATF_RECV;
	dprintk("mrp_recv returning -1\n");
	return -1;
}

static int mrp_reset(struct mrp_unit *mrp)
{
	unsigned long flags;
	volatile struct mrpregs *regs = mrp->regs;

	// note: on smp kernels, we should use lock_kernel/unlock_kernel instead of save_flags/cli/restore_flags

	mrp_printk(2, "mrp_reset:\n");

	save_flags(flags);
	cli();

	regs->ier = 0;
	regs->rst = MRP_RESETF_8000;
	udelay(10);
	regs->fst = 0xc0c0;
	udelay(10);
	regs->fst = 0;
	regs->isp = 0;
	regs->csi = 7;
	regs->rst = 0;
	udelay(20);
	regs->ier = MRP_STATF_CPR | MRP_STATF_BOOTP | MRP_STATF_RECV;
	{
		unsigned short tmp;
		tmp = mrp->flags;
		tmp |= MRPF_RESET;
		tmp &= ~MRPF_SBUSY;
		tmp &= ~MRPF_RDONE;
		tmp &= ~MRPF_CPBUSY;
		mrp->flags = tmp;
	}
	wake_up(&mrp->wake_queue3);
	mrp->rlen = 0;
	mrp->slen = 0;
	mrp->sendring.read_index = 0;
	mrp->sendring.write_index = 0;
	mrp->sendring.count = 0;
	mrp->recvring.read_index = 0;
	mrp->recvring.write_index = 0;
	mrp->recvring.count = 0;

	restore_flags(flags);
	dprintk("mrp_reset returning 0\n");
	return 0;
}

static int mrp_bootp(struct mrp_unit *mrp)
{
	unsigned short cpr = mrp->regs->cpr;

	mrp_printk(2, "mrp_bootp: cpr=0x%04x\n", cpr);
	
	mrp->regs->fst = MRP_FSTF_0040 | MRP_FSTF_0080 | MRP_FSTF_4000 | MRP_FSTF_8000;
	udelay(10);
	mrp->regs->fst = 0;
	mrp->flags &= MRPF_SBUSY | MRPF_RDONE;
	mrp->rlen = 0;
	mrp->slen = 0;
	if (cpr & MRP_CPRF_0100) {
		if (cpr & MRP_CPRF_0200) {
			wake_up(&mrp->wake_queue3);
			//cpr = 0x0f00;
		} else {
			if (mrp->buf40) {
				mrp_put_fifo(mrp, mrp->buf40, mrp->nbytes40);
			}
		}
	} else {
		if (mrp->buf38) {
			mrp_put_fifo(mrp, mrp->buf38, mrp->nbytes38);
		}
	}
	mrp->regs->cps = cpr;
	mrp->regs->csi = 4;
	mrp->regs->fsi = 4;
	mrp->regs->ier |= MRP_STATF_BOOTP;
	dprintk("mrp_bootp returning 0\n");
	return 0;
}

static void mrp_cpr(struct mrp_unit *mrp)
{
	dprintk("mrp_cpr\n");
	if (mrp->recvring.count <= 0xfff) {
		ringbuf_put(&mrp->recvring, mrp->regs->cpr);
		mrp->regs->csi = 1;
		mrp->regs->fsi = 2;
		mrp->regs->ier |= MRP_STATF_CPR;
		wake_up(&mrp->wake_queue1);
	}
	dprintk("mrp_cpr returning void\n");
}

static void mrp_cps(struct mrp_unit *mrp)
{
	dprintk("mrp_cps\n");
	if (mrp->flags & MRPF_CPBUSY) {
		if (mrp->sendring.count > 0) {
			mrp->flags |= MRPF_CPBUSY;
			mrp->regs->cpr = ringbuf_get(&mrp->sendring);
			mrp->regs->fsi = 1;
			mrp->regs->ier |= MRP_STATF_CPS;
			wake_up(&mrp->wake_queue2);
		}
	}
	dprintk("mrp_cps returning void\n");
}

static void mrp_interrupt(int irq, void *dev_id, struct pt_regs *pt_regs)
{
	unsigned long flags;
	unsigned short stat;
	int index;
	struct mrp_unit *mrp;
	struct mrpregs *regs;
	mrp = (struct mrp_unit *) dev_id;
	regs = mrp->regs;
	
	dprintk("mrp_interrupt: irq %d, dev_id %ph, pt_regs %ph\n", irq, dev_id, pt_regs);

	if ((mrp < &mrp_units[0]) || (mrp >= &mrp_units[4])) {
		dprintk("mrp_interrupt: returning\n");
		return;
	}

	save_flags(flags);
	cli();

	if ((mrp->base0->idk4c & IDK4CF_0004) == 0) {
		restore_flags(flags);
		dprintk("mrp_interrupt: returning\n");
		return;
	}

	mrp->nintr++;
	stat = regs->ist & regs->ier;
	if (mrp_debug >= 2) {
		mrp_printk(2, "mrp_interrupt: stat=0x%04x\n", stat);
		mrp_dump_regs(regs);
	}

	if (((stat & MRP_STATF_RECV) == 0) && ((stat & MRP_STATF_WAKE) == 0)) {
		if (stat & MRP_STATF_RESET) {
			regs->rst &= (MRP_RESETF_2000 | MRP_RESETF_4000);
			mrp_reset(mrp);
		} else if (stat & MRP_STATF_BOOTP) {
			regs->ier &= ~MRP_STATF_BOOTP;
			mrp_bootp(mrp);
		} else if (stat & MRP_STATF_CPR) {
			regs->ier &= ~MRP_STATF_CPR;
			mrp_cpr(mrp);
		} else if (stat & MRP_STATF_CPS) {
			regs->ier &= ~MRP_STATF_CPS;
			regs->csi = 2;
			mrp->flags &= ~MRPF_CPBUSY;
			mrp_cps(mrp);
			wake_up(&mrp->wake_queue2);
		} else if (stat) {
			index = 666;
			if (mrp == &mrp_units[0]) index = 0;
			if (mrp == &mrp_units[1]) index = 1;
			if (mrp == &mrp_units[2]) index = 2;
			if (mrp == &mrp_units[3]) index = 3;
			printk("mrp%d: unexpected interrupt (stat=0x%x)\n", index, stat);
			regs->ier = 0;
		}
	} else {
		if (stat & MRP_STATF_WAKE) {
			regs->ier &= ~MRP_STATF_WAKE;
			mrp->flags &= ~MRPF_SBUSY;
			wake_up(&mrp->wake_queue3);
		}
		if (stat & MRP_STATF_RECV) {
			regs->ier &= ~MRP_STATF_RECV;
			mrp_recv(mrp);
		}
	}

	restore_flags(flags);
	dprintk("mrp_interrupt returning\n");
	return;
}

static int mrp_read(struct inode *inode, struct file *file, char *buf, int nbytes)
{
	struct mrp_unit *mrp;
	int counter, index, minor, rc;

	dprintk("mrp_read: inode %ph, file %ph, buf %ph, nbytes %xh\n",
		inode, file, buf, nbytes);

	minor = MINOR(inode->i_rdev);
	index = minor & 3;
	mrp = &mrp_units[index];

	mrp_printk(2, "mrp_read: count=%d\n", nbytes);

	if (index > 3) {
		dprintk("mrp_read returning %d\n", -ENODEV);
		return -ENODEV;
	}
	if ((mrp->flags & MRPF_VALID) == 0) {
		dprintk("mrp_read returning %d\n", -ENODEV);
		return -ENODEV;
	}
	if (minor & 0x40) {
		if ((mrp->flags & MRPF_OPENED) == 0) {
		dprintk("mrp_read returning %d\n", -ENODEV);
			return -ENODEV;
		}
		if (mrp->flags & MRPF_RESET) {
		dprintk("mrp_read returning %d\n", -EIO);
			return -EIO;
		}
		if (nbytes >= MRP_MAX_PKT_SIZE) {
		dprintk("mrp_read returning %d\n", -EINVAL);
			return -EINVAL;
		}
	} else if (mrp->flags & MRPF_CPOPEN) {
		/* not a regular inode minor, and not CPOPEN'd. */
		dprintk("mrp_read returning %d\n", -ENODEV);
		return -ENODEV;
	}


	cli();

	rc = verify_area(VERIFY_WRITE, buf, nbytes);
	if (rc != 0) {
		dprintk("mrp_read returning %d\n", rc);
		return rc;
	}
	
	/* 0x8000bc5 */
	for (counter = 0; counter < nbytes; counter++) {
		/* top of loop at loc_8000c63 */
		/* loc_8000bd8 */
		while (mrp->recvring.count == 0) {
			/* 8000be5 */
			if (file->f_flags & O_NONBLOCK) {
				dprintk("mrp_read returning %d\n", -EWOULDBLOCK);
				return -EWOULDBLOCK;
			}
			interruptible_sleep_on(&mrp->wake_queue1);
			if (current->signal & ~current->blocked) {
				dprintk("mrp_read returning %d\n", -EINTR);
				return -EINTR;
			}
		}
		*buf++ = ringbuf_get(&mrp->recvring);
	}
	/* 8000c71 */
	mrp->regs->ier |= MRP_STATF_CPR;
	dprintk("mrp_read returning %d\n", counter);
	return counter;
}

static int mrp_write(struct inode *inode, struct file *file, const char *buf, int len)
{
	int minor, rc;
	unsigned index;
	struct mrp_unit *mrp;
	struct decihdr_s hdr;

	minor = MINOR(inode->i_rdev);
	index = minor & 3;

	dprintk("mrp_write: inode %ph, file %ph, buf %ph, len %d\n",
		inode,
		file,
		buf,
		len
	);

	mrp_printk(2, "mrp_write: count=%d\n", len);

	if (index > 3) {
		dprintk("mrp_write: index out of range, returning -ENODEV\n");
		return -ENODEV;
	}
	mrp = &mrp_units[index];

	if ((mrp->flags & MRPF_VALID) == 0) {
		dprintk("mrp_write: valid flag not set, returning -ENODEV\n");
		return -ENODEV;
	}

	if (minor & 0x40) {
		int bytes_done;
		/* special control plane case */
		if ((mrp->flags & MRPF_CPOPEN) == 0) {
			dprintk("mrp_write: cpopen flag not set, returning -ENODEV\n");
			return -ENODEV;
		}
		rc = verify_area(VERIFY_READ, buf, len);
		if (rc != 0) {
			dprintk("mrp_write: verify_area failed, returning rc (%d)\n", rc);
			return rc;
		}
		cli();
		if (rc < len) {
			mrp_cps(mrp);
			sti();
			dprintk("mrp_write: rc (%d) < len (%d), returning 0\n", rc, len);
			return 0;
		}
		/* loc_8000f4c */
		for (bytes_done = 0; bytes_done < len; bytes_done++) {
			while (mrp->sendring.count > 0xfff) {
				/* loc_8000f5c */
				if (file->f_flags & O_NONBLOCK) {
					sti();
					dprintk("mrp_write: O_NONBLOCK was set, returning -EWOULDBLOCK\n");
					return -EWOULDBLOCK;
				}
				/* 8000f6a */
				interruptible_sleep_on(&mrp->wake_queue2);
				if (current->signal & ~current->blocked) {
					sti();
					dprintk("mrp_write: unblocked signal, returning -EINTR\n");
					return -EINTR;
				}
			}
			/* loc_8000f9f */
			ringbuf_put(mrp->recvbuf, *buf++);
		}

		/* loc_8000fd6 */
		mrp_cps(mrp);
		sti();
		dprintk("mrp_write: returning len (%d)\n", len);
		return len;
	}

	/* normal case */
	if ((mrp->flags & MRPF_OPENED) == 0) {
		dprintk("mrp_write: not opened, so returning -ENODEV\n");
		return -ENODEV;
	}
	if (len < sizeof(hdr)) {
		dprintk("mrp_write: len (%d) < sizeof(hdr) (%u), so returning -EINVAL\n", len, sizeof(hdr));
		return -EINVAL;
	}
	if (len > MRP_MAX_PKT_SIZE) {
		dprintk("mrp_write: len (%d) > MRP_MAX_PKT_SIZE (%d), so returning -EINVAL\n", len, MRP_MAX_PKT_SIZE);
		return -EINVAL;
	}
	/* 8001037 */
	if ((rc = verify_area(VERIFY_READ, buf, sizeof(hdr))) != 0) {
		dprintk("mrp_write: VERIFY_READ failed, so returning rc (%d)\n", rc);
		return rc;
	}
	memcpy_fromfs(&hdr, buf, sizeof(hdr));
	if (hdr.magic != DECI_MAGIC) {
		dprintk("mrp_write: hdr.magic (%04xh) != DECI_MAGIC (%04xh) so returning -EINVAL\n",
			hdr.magic, DECI_MAGIC);
		return -EINVAL;
	}
	if (len != hdr.size) {
		dprintk("mrp_write: len (%d) != hdr.size (%d), so returning -EINVAL\n", len, hdr.size);
		return -EINVAL;
	}
	if (hdr.cksum != deci_cksum((unsigned int *) &hdr)) {
		dprintk("mrp_write: checksum invalid, so returning -EINVAL\n");
		return -EINVAL;
	}

	if (hdr.req == REQ_TRESET) {
		//struct deci_reset_pkt_s pkt;
		//struct decihdr_s hdr2 = (struct decihdr_s *) buf;
		int reset_mode;
		if (!(len > 0x23u)) {
			reset_mode = 0;
		} else {
			int *frompkt = (int *)(buf + 0x20);
			memcpy_fromfs(&reset_mode, frompkt, 4);
			reset_mode >>= 4;
			reset_mode &= 0xf;
		}
		cli();
		switch (reset_mode) {
		case 1:
			if (mrp->buf38) {
				kfree(mrp->buf38);
			}
			mrp->buf38 = kmalloc(len, GFP_KERNEL);
			if (!mrp->buf38) {
				sti();
				dprintk("mrp_write: buf38 is null, so returning -ENOMEM\n");
				return -ENOMEM;
			}
			mrp->nbytes38 = len;
			memcpy_fromfs(mrp->buf38, buf, len);
			break;
		case 0:
			if (mrp->buf40) {
				kfree(mrp->buf40);
			}
			mrp->buf40 = kmalloc(len, GFP_KERNEL);
			if (!mrp->buf40) {
				sti();
				dprintk("mrp_write: buf40 is null, so returning -ENOMEM\n");
				return -ENOMEM;
			}
			mrp->nbytes40 = len;
			memcpy_fromfs(mrp->buf40, buf, len);
			break;
		case 2:
		default:
			break;
		}
		if (reset_mode == 0) {
			mrp_reset(mrp);
		}
		mrp->deci_tag_for_reset = hdr.tag;
		if (hdr.tag) {
			mrp->flags |= MRPF_REQTAG;
			wake_up(&mrp->wake_queue4);
		}
		sti();
		dprintk("mrp_write: returning len (%d)\n", len);
		return len;
	}
	
	if ((mrp->flags & MRPF_RESET) == 0) {
		dprintk("mrp_write: reset flag not set, so returning -EIO\n");
		return -EIO;
	}

	cli();
	while (mrp->flags & MRPF_SBUSY) {
		if (file->f_flags & O_NONBLOCK) {
			sti();
			dprintk("mrp_write: returning -EWOULDBLOCK\n");
			return -EWOULDBLOCK;
		}
		interruptible_sleep_on(&mrp->wake_queue3);
		if (current->signal & ~current->blocked) {
			sti();
			dprintk("mrp_write: returning -EINTR\n");
			return -EINTR;
		}
	}
	//copy_from_user(mrp->sendbuf, buf, len);
	mrp->deci_out = mrp->sendbuf;
	mrp->slen = len;
	mrp_send(mrp);
	sti();
	dprintk("mrp_write: returning len (%d)\n", len);
	return len;
}

static int mrp_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
	int index, minor;
	struct mrp_unit *mrp;

	minor = MINOR(inode->i_rdev);
	index = minor & 3;

	if (index > 3) {
		dprintk("mrp_select: returning 0\n");
		return 0;
	}
	
	mrp = &mrp_units[index];

	if (minor & 0x40) {
		switch (sel_type) {
		case SEL_IN:
			if (mrp->recvring.count > 0) {
				dprintk("mrp_select: returning 1\n");
				return 1;
			}
			select_wait(&mrp->wake_queue1, wait);
			break;
		case SEL_OUT:
			if (mrp->sendring.count <= (RINGBUF_SIZE - 2)) {
				dprintk("mrp_select: returning 1\n");
				return 1;
			}
			select_wait(&mrp->wake_queue2, wait);
			break;
		}
	} else {
		if (sel_type == SEL_IN) {
			if (mrp->flags & (MRPF_RDONE | MRPF_REQTAG)) {
				dprintk("mrp_select: returning 1\n");
				return 1;
			}
			select_wait(&mrp->wake_queue4, wait);
		}
		if (sel_type == SEL_OUT) {
			if ((mrp->flags & MRPF_SBUSY) == 0) {
				dprintk("mrp_select: returning 1\n");
				return 1;
			}
			select_wait(&mrp->wake_queue3, wait);
		}
	}
	
	dprintk("mrp_select: returning 0\n");
	return 0;
}

static int mrp_open(struct inode *inode, struct file *file)
{
	int bid, flags, index, minor;
	struct mrp_unit *mrp;

	minor = MINOR(inode->i_rdev);
	index = minor & 3;
	mrp = &mrp_units[index];

#ifdef MRP_NOMATCHING
	mrp_printk(2, "mrp_open: index=%d, minor=%d\n", index, minor);
#else
	mrp_printk(2, "mrp_open: index=%d\n", index);
#endif

	if (index > 3) {
		return -ENODEV;
	}

	flags = mrp->flags;
	if (!(flags & MRPF_VALID)) {
		return -ENODEV;
	}

	bid = mrp->regs->bid;
	if (bid != 0x4126) {
		return -EIO;
	}

	if (minor & 0x40) {
		if (flags & MRPF_CPOPEN) {
			return -EBUSY;
		}
		flags |= MRPF_CPOPEN;
	} else {
		if (flags & MRPF_OPENED) {
			return -EBUSY;
		}
		flags |= MRPF_OPENED;
	}
	mrp->flags = flags;
	MOD_INC_USE_COUNT;

	return 0;
}

static void mrp_release(struct inode *inode, struct file *file)
{
	unsigned index, minor;
	int flags, *realflags;
	minor = MINOR(inode->i_rdev);
	index = minor & 3;

	realflags = &mrp_units[index].flags;

	mrp_printk(2, "mrp_release: index=%d\n", index);
	
	if (index > 3) {
		return;
	}

	flags = *realflags;

	if ((flags & MRPF_VALID) == 0) {
		return;
	}

	if (MINOR(inode->i_rdev) & 0x40) {
		if (flags & MRPF_CPOPEN) {
			flags &= ~MRPF_CPOPEN;
		}
	} else {
		if (flags & MRPF_OPENED) {
			flags &= ~MRPF_OPENED;
		}
	}
	*realflags = flags;
	MOD_DEC_USE_COUNT;
}

static int mrp_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int index, minor;
	struct mrp_unit *mrp;

	minor = MINOR(inode->i_rdev);
	index = minor & 3;
	mrp = &mrp_units[index];

	mrp_printk(2, "mrp_ioctl: cmd=0x%x arg=0x%lx\n", cmd, arg);

	if (index > 3) {
		return -ENODEV;
	}

	if ((mrp->flags & MRPF_VALID) == 0) {
		return -ENODEV;
	}

	if (MINOR(inode->i_rdev) & 0x40) {
		if (mrp->flags & MRPF_CPOPEN) {
			return -ENODEV;
		}
		if (cmd != MRP_IOCTL_GT) {
			return -EINVAL;
		}
	}

	switch (cmd) {
	case MRP_IOCTL_RF:
		cli();
		mrp->rlen = 0;
		mrp->regs->ier |= MRP_STATF_RECV;
		mrp->flags &= ~MRPF_RDONE;
		sti();
		break;
	case MRP_IOCTL_GT:
		{
			unsigned int thingy = MRP_IOCTL_DECI;
			copy_to_user((void *)arg, &thingy, sizeof(thingy));
		}
		break;
	case MRP_IOCTL_SF:
		cli();
		mrp->slen = 0;
		mrp->regs->ier &= ~MRP_STATF_WAKE;
		mrp->flags &= ~MRPF_SBUSY;
		sti();
		break;
	case MRP_IOCTL_SY:
		cli();
		while (mrp->flags & MRPF_SBUSY) {
			if (file->f_flags & O_NONBLOCK) {
				return -EWOULDBLOCK;
			}
			interruptible_sleep_on(&mrp->wake_queue3);
			if (current->signal & ~current->blocked) {
				return -EINTR;
			}
		}
		sti();
		break;
	}
	
	return 0;
}

#ifdef CONFIG_PROC_FS
static int mrp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int index, len = 0;
#define cat(fmt, arg...) len += sprintf(buffer+len, fmt, ##arg);

	cat("DECI1 $Revision: 3.10 $ %s %s\n", MRP_PSNET_BUILDDATE, MRP_PSNET_BUILDTIME);
	for (index = 0; index < MRP_UNIT_MAX; index++) {
		struct mrp_unit *mrp;
		typeof(mrp->regs) regs;
		
		cli();
		
		mrp = &mrp_units[index];
		regs = mrp->regs;
		
		cat("unit%d", index);
		if (mrp->flags & MRPF_DETECT) {
			cat(" DETECT");
		}
		if (mrp->flags & MRPF_VALID) {
			cat(" VALID");
		}
		if (mrp->flags & MRPF_RESET) {
			cat(" RESET");
		}
		if (mrp->flags & MRPF_OPENED) {
			cat(" OPENED");
		}
		if (mrp->flags & MRPF_SBUSY) {
			cat(" SBUSY");
		}
		if (mrp->flags & MRPF_RDONE) {
			cat(" RDONE");
		}
		if (mrp->flags & MRPF_REQTAG) {
			cat(" REQTAG");
		}
		if (mrp->flags & MRPF_CPOPEN) {
			cat(" CPOPEN");
		}
		if (mrp->flags & MRPF_CPBUSY) {
			cat(" CPBUSY");
		}

		cat(" nintr=%d", mrp->nintr);
		cat("\n");

		if (mrp->flags & MRPF_VALID) {
			cat(" BID=%04x", regs->bid);
			cat(" RST=%04x", regs->rst);
			cat(" CPS=%04x", regs->cps);
			cat(" CPR=%04x", regs->cpr);
			cat(" FST=%04x", regs->fst);
			cat(" TXC=%04x", regs->txc);
			cat(" RXC=%04x", regs->rxc);
			cat(" F1C=%04x\n", regs->f1c);
			cat(" IST=%04x", regs->ist);
			cat(" ISP=%04x", regs->isp);
			cat(" IER=%04x", regs->ier);
			cat(" CSI=%04x", regs->csi);
			cat(" FSI=%04x", regs->fsi);
			cat(" AEO=%04x", regs->aeo);
			cat(" AFO=%04x\n", regs->afo);
#ifdef MRP_NOMATCHING
			cat(" base4 = 0x%08x\n", mrp->base4->data[0]);
#endif /* MRP_NOMATCHING */
		}
		sti();
	}

	return len;
#undef cat
}

#endif /* PROC_FS */

static int mrp_base(unsigned char bus, unsigned char dev_fn, unsigned char where)
{
	int base;
	int address;

	base = (where - 0x10) >> 2;
	if (pcibios_read_config_dword(bus, dev_fn, where, &address)) {
		printk("mrp: can't read config (BASE%d)\n", base);
		return 0;
	}
	if (address & 0x7) {
		printk("mrp: unsupported address type (BASE%d=0x%x)\n", base, address);
		return 0;
	}
	address &= ~0xf;
	return address;
}

static void *mrp_remap(unsigned int base)
{
	void *rc;
	if (MAP_NR(base) < MAP_NR(high_memory)) {
		printk("mrp: base < high_memory ?? (base=0x%x)\n", base);
		return NULL;
	}

	rc = vremap((base & PAGE_MASK), PAGE_SIZE);
	if (!rc) {
		printk("mrp: can't vremap (base=0x%x)\n", base);
		return NULL;
	}
	rc = (base & ~PAGE_MASK) + rc;
	return rc;
}

int mrp_init(void)
{
	unsigned short index = 0;
	int boards_found = 0;
	int iRc;

#ifdef MRP_NOMATCHING
	if (strlen(MRP_PROCFILE_NAME) != MRP_PROCFILE_NAME_LENGTH) {
		printk("mrp: strlen(\"%s\") != %d\n", MRP_PROCFILE_NAME, MRP_PROCFILE_NAME_LENGTH);
		return 0;
	}
#endif /* MRP_NOMATCHING */

	memset(mrp_units, 0, sizeof(mrp_units));
	
	if (!pcibios_present()) {
		return 0;
	}

	for (index = 0; index < MRP_UNIT_MAX; index++) {
		int base0, base2, base3;
		unsigned char pci_bus, pci_device_fn, pci_irq_line;
		void *rc;
#ifdef MRP_NOMATCHING
		int base4;
#endif /* MRP_NOMATCHING */

		pci_bus = pci_device_fn = pci_irq_line = 69u;

		if (pcibios_find_device(PCI_VENDOR_ID_SONY,
					PCI_DEVICE_ID_SONY_PS2_MRP,
					index, &pci_bus,
					&pci_device_fn))
			break;
		base0 = mrp_base(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_0);
		if (!base0) continue;
		base2 = mrp_base(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_2);
		if (!base2) continue;
		base3 = mrp_base(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_3);
		if (!base3) continue;
#ifdef MRP_NOMATCHING
		base4 = mrp_base(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_4);
		if (!base4) {
			printk("mrp: unit%d: base4 couldn't be base'd\n", index);
		}
#endif /* MRP_NOMATCHING */

		rc = mrp_remap(base0);
		if (!rc) continue;
		mrp_units[index].base0 = rc;
		
		rc = mrp_remap(base2);
		if (!rc) continue;
		mrp_units[index].base2 = rc;
		
		rc = mrp_remap(base3);
		if (!rc) continue;
		mrp_units[index].regs = rc;

#ifdef MRP_NOMATCHING
		if (base4) {
			rc = mrp_remap(base4);
			if (!rc) {
				printk("mrp: unit%d: base4 couldn't be remap'd\n", index);
			} else {
				mrp_units[index].base4 = rc;
			}
		}
#endif /* MRP_NOMATCHING */

		if (pcibios_read_config_byte(pci_bus, pci_device_fn,
					PCI_INTERRUPT_LINE, &pci_irq_line)) {
			printk("mrp: can't read config (IRQ)\n");
			continue;
		}
		mrp_units[index].irq = pci_irq_line;
#ifdef MRP_NOMATCHING
		printk("mrp: unit %d at 0x%x,0x%x,0x%x (irc = %d) base4=0x%x\n",
			index, base0, base2, base3, pci_irq_line, base4);
#else
		printk("mrp: unit %d at 0x%x,0x%x,0x%x (irc = %d)\n",
			index, base0, base2, base3, pci_irq_line);
#endif
		mrp_units[index].flags |= MRPF_DETECT;
		boards_found++;
	}
	if (boards_found == 0) {
		return 0;
	}

	iRc = register_chrdev(0, "mrp", &mrp_fops);
	if (iRc <= 0) {
		printk("mrp: unable to get dynamic major\n");
		return 0;
	}
	mrp_major = iRc;
	printk("mrp: registered character major %d\n", iRc);

#ifdef CONFIG_PROC_FS
	proc_register_dynamic(&proc_root, &mrp_proc_de);
#endif /* PROC_FS */

	for (index = 0; index < 4; index++) {
		void *rc;
		struct mrp_unit *mrp = &mrp_units[index];
		if ((mrp->flags & MRPF_DETECT) == 0) {
			continue;
		}
		
		rc = kmalloc(MRP_MAX_PKT_SIZE, GFP_KERNEL);
		if (!rc) {
			printk("mrp%d: no space for send buffer\n", index);
			continue;
		}
		mrp->sendbuf = rc;

		rc = kmalloc(MRP_MAX_PKT_SIZE, GFP_KERNEL);
		if (!rc) {
			printk("mrp%d: no space for recv buffer\n", index);
			continue;
		}
		mrp->recvbuf = rc;
#if 1
		iRc = request_irq(mrp->irq, mrp_interrupt,
				SA_INTERRUPT|SA_SHIRQ, "MRP", mrp);
		if (!iRc) {
			/* try again, without SA_INTERRUPT */
			iRc = request_irq(mrp->irq, mrp_interrupt,
					SA_SHIRQ, "MRP", mrp);
		}
		if (!iRc) {
			printk("mrp%d: can't register irq\n", index);
			continue;
		}
#endif /* !MRP_NOMATCHING */
		mrp->base0->idk50 &= ~IDK50F_00000020;
		mrp->base0->idk50 |=  IDK50F_40000000;
		udelay(10);
		mrp->base0->idk50 &= ~IDK50F_40000000;
		mrp->base0->idk4c |= IDK4CF_0040;
		mrp->regs->ier = 0;
		mrp->flags |= MRPF_VALID;

	}
	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return mrp_init();
}

void cleanup_module(void)
{
	int index;
	
	if (mrp_major <= 0) {
		return;
	}
	
	for (index = 0; index < 4; index++) {
		struct mrp_unit *mrp = &mrp_units[index];
		if (mrp->flags & MRPF_VALID) {
			mrp->base0->idk50 |= 0x20;
			mrp->base0->idk4c &= ~0x40;
			free_irq(mrp->irq, mrp);
		}
		if (mrp->sendbuf)
			kfree(mrp->sendbuf);
		if (mrp->recvbuf)
			kfree(mrp->recvbuf);
		if (mrp->buf38)
			kfree(mrp->buf38);
		if (mrp->buf40)
			kfree(mrp->buf40);
		if (mrp->base0)
			vfree(mrp->base0);
		if (mrp->base2)
			vfree(mrp->base2);
		if (mrp->regs)
			vfree(mrp->regs);
	}
	unregister_chrdev(mrp_major, "mrp");
	printk("mrp: unregistered character major %d\n", mrp_major);
#ifdef CONFIG_PROC_FS
	proc_unregister(&proc_root, mrp_proc_de.low_ino);
#endif /* PROC_FS */
}
#endif
