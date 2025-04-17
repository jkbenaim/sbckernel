/*
 * mrp-psnet
 * reverse-engineered from:
 * 	/usr/local/sce/driver/psnet/2.0.36/mrp.o
 * 	md5sum: 77187b9f4c836004398021a7ea32cd97
 */

#include <linux/proc_fs.h>
#include <linux/ps2mrp-psnet.h>

/*
 * .data
 *
 */

unsigned int mrp_debug = 1;
static struct proc_dir_entry mrp_proc_de = {
	0, 3, "mrp", S_IFREG | S_IRUSR | S_IRGRP| S_IROTH, 1, 0, 0, 0, mrp_get_info
};

static struct file_operations mrp_fops = {
	NULL,		/* seek */
	mrp_read,	/* read */
	mrp_write,	/* write */
	NULL,		/* readdir */
	mrp_select,	/* select */
	mrp_ioctl,	/* ioctl */
	NULL,		/* mmap */
	mrp_open,	/* open */
	mrp_release	/* release */
};

unsigned int mrp_major;

/*
 * .bss
 *
 */

struct mrp_unit mrp_units[4];

/*
 * .text
 *
 */

void mrp_dump_regs(struct mrpregs *regs)
{
	if (mrp_debug > 2) {
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
}

int mrp_send(struct mrp_unit *mrp)
{
	int nw;

	if (mrp_debug > 1) {
		printk("mrp_send: slen=%d\n", mrp->slen);
	}

	nw = (mrp->slen + 3)/4;
	mrp_put_fifo(mrp, mrp->buf1c, nw);

	return nw;
}

int mrp_recv(struct mrp_unit *mrp)
{
	if (mrp_debug > 1) {
		printk("mrp_recv:\n");
	}
	if (!(mrp->flags & MRPF_RDONE)) {
		return 0;
	}
	void *rbp = mrp->recvbuf;
	if (!(mrp->mrpregs->fst & 1)) {
		while (true) {
			mrp->buf20 = mrp->recvbuf;
			if (mrp_debug > 2) {
				printk("mrp_get_fifo: nw=%d\n", 1);
			}
			if (!(mrp->mrpregs->fst & 0x10)) {
				if (mrp_debug > 0) {
					printk("mrp_recv: invalid fifo status (%04x)\n", mrp->mrpregs->fst);
				}
				mrp->mrpregs->rxc = 1;
				break;
			}
			/* TODO */
			mrp->rlen = 
		}
	}

}

int mrp_reset(struct mrp_unit *mrp)
{
	unsigned long flags;
	struct mrpregs *regs = mrp->regs;

	// note: on smp kernels, we should use lock_kernel/unlock_kernel instead of cli/save_flags/restore_flags

	if (mrp_debug > 1) {
		printk("mrp_reset:\n");
	}

	save_flags(flags);
	cli();

	regs->ier = 0;
	regs->rst = 0x8000;
	udelay(10);
	regs->fst = 0xc0c0;
	udelay(10);
	regs->fst = 0;
	regs->isp = 0;
	regs->csi = 7;
	regs->rst = 0;
	udelay(20);
	regs->ier = 0x15;
	{
		uint16_t tmp;
		tmp = mrp->flags;
		tmp |= MRPF_RESET;
		tmp &= ~MRPF_SBUSY;
		tmp &= ~MRPF_RDONE;
		tmp &= ~MRPF_CPBUSY;
		mrp->flags = rmp;
	}
	wake_up(mrp->wake_queue_3);
	mrp->rlen = 0;
	mrp->slen = 0;
	mrp->ringbuf2_ptr_2 = 0;
	mrp->unused_104c = 0;
	mrp->ringbuf2_ptr_1 = 0;
	mrp->unused_205c = 0;
	mrp->ringbuf_idx_1 = 0;
	mrp->ringbuf_idx_2 = 0;

	restore_flags(flags);
	return 0;
}

int mrp_bootp(struct mrp_unit *mrp)
{
	// TODO
}

void mrp_cpr(struct mrp_unit *mrp)
{
	struct mrpregs *regs = mrp->base_eb1;
	uint8_t *buf[0x1000] = &mrp->ringbuf;
	if (mrp->ringbuf_idx_2 <= 0xfff) {
		mrp->ringbuf_idx_1 = buf + regs->cpr;
		mrp->ringbuf_idx_1++;
		mrp->ringbuf_idx_2++;
		regs->csi = 1;
		regs->fsi = 2;
		regs->ier |= 1;
		wake_up(&mrp->wake_queue);
	}
}

int mrp_cps(struct mrp_unit *mrp)
{
	struct mrpregs *regs = mrp->regs;
	uint8_t *buf[0x1000] = &mrp->ringbuf2;
	enum mrp_flags flags = mrp->flags;
	if (!(mrp->flags & MRPF_CPBUSY) && (mrp->ringbuf2_ptr_1 > 0)) {
		mrp->flags |= MRPF_CPBUSY;
		regs->cps = (mrp->ringbuf2_ptr_2 & 0xfff) + buf;
		mrp->ringbuf2_ptr_2 = mrp->ringbuf2_ptr_2 + 1;
		mrp->ringbuf2_ptr_1 = mrp->ringbuf2_ptr_2 - 1;
		regs->fsi = 1;
		regs->ier |= 2;
		wake_up(mrp->wake_queue_2);
	}
}

void mrp_interrupt(int arg0, struct mrp_unit *mrp, void *pt_regs)
{
	unsigned long flags;
	uint16_t stat;
	int index;

	index = (mrp - mrp_units) / sizeof(struct mrp_unit);

	if (mrp < &mrp_units[0]) {
		return;
	}

	if (mrp > &mrp_units[3]) {
		return;
	}

	save_flags(flags);
	cli();

	if (mrp->base0->idk4c & IDK4CF_0004) {
		restore_flags(flags);
		return;
	}

	mrp->nintr++;
	stat = mrp->mrpregs.ist & mrp->mrpregs.ier;
	if (mrp_debug > 1) {
		printk("mrp_interrupt: stat=0x%04x\n", stat);
		mrp_dump_regs(mrp);
	}

	if (stat & (MRPF_STATF_10 | MRPF_STATF_20)) {
		if (stat & MRP_STATF_08) {
			mrp->mrpregs.rst &= (MRP_RESETF_2000 | MRP_RESETF_1000);
			mrp_reset(mrp);
		} else if (stat & MRP_STATF_04) {
			mrp->mrpregs.ier &= ~MRP_STATF_04;
			mrp_bootp(mrp);
		} else if (stat & MRP_STATF_01) {
			mrp->mrpregs.ier &= ~MRP_STATF_01;
			mrp_cpr(mrp);
		} else if (stat & MRP_STATF_02) {
			mrp->mrpregs.ier &= ~MRP_STATF_02;
			mrp->mrpregs.csi = 2;
			mrp->flags &= ~MRPF_CPBUSY;
			mrp_cps(mrp);
			wake_up(mrp->wake_queue_2);
		} else if (stat) {
			printk("mrp%d: unexpected interrupt (stat=0x%x)\n", index, stat);
			mrp->mrpregs.ier = 0;
		}
	} else {
		if (stat & MRP_STATF_20) {
			mrp->mrpregs->ier &= ~MRP_STATF_20;
			mrp->flags &= ~MRPF_SBUSY;
			wake_up(mrp->wake_queue_3);
		}
		if (stat & MRP_STATF_10) {
			mrp->mrpregs->ier &= ~MRP_STATF_10;
			mrp_recv(mrp);
		}
	}

	restore_flags(flags);
}

int mrp_read(struct inode *inode, struct file *file, char *buf, int len)
{
	// TODO
	struct mrp_unit *mrp;
	int index, rc;

	index = inode->major & 3;
	mrp = &mrp_units[index];

	if (mrp_debug > 1) {
		printk("mrp_read: count=%d\n", nbytes);
	}

	if (index > 3) {
		return -ENODEV;
	}
	if (!(mrp->flags & MRPF_VALID)) {
		return -ENODEV;
	}
	if (inode->i_rdev.major & 0x40) {
		if (!(mrpf->flags & MRP_OPENED)) {
			return -ENODEV;
		}
		if (mrp->flags & MRPF_RESET) {
			return -EIO;
		}
		if (nbytes > 0x3fff) {
			return -EINVAL;
		}
	} else if (mrp->flags & MRPF_CPOPEN) {
		return -ENODEV;
	}

	rc = verify_area(VERIFY_WRITE, buf, nbytes);
}

int mrp_write(struct inode *inode, struct file *file, const char *buf, int len)
{
	// TODO
}

int mrp_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
	// TODO
}

int mrp_open(struct inode *inode, struct file *file)
{
	(void)file;
	int bid, flags, index, major;
	struct mrp_unit *mrp;

	if (mrp_debug > 1) {
		printk("mrp_open: index=%d\n", index);
	}

	major = inode->i_rdev.major;
	index = major & 3;
	mrp = &mrp_units[index];

	if (index > 3) {
		return -ENODEV;
	}

	flags = mrp->flags;
	if (!(flags & MRPF_VALID)) {
		return -ENODEV;
	}

	bid = mrp->mrpregs->bid;
	if (bid != 0x4126) {
		return -EIO;
	}

	if (major & 0x40) {
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
	MOD_USE_COUNT_INC;
}

void mrp_release(struct inode *inode, struct file *file)
{
	(void)file;
	int index, major;
	int flags;

	major = inode->i_rdev.major;
	index = major & 3;
	flags = mrp_units[index].flags;

	if (mrp_debug > 1) {
		printk("mrp_release: index=%d\n", index);
		return;
	}
	if (index > 4) {
		return;
	}

	if (!(flags & MRPF_VALID)) {
		return;
	}
	if (major & 0x40) {
		if (flags & MRPF_OPENED) {
			return;
		}
	} else if (!(flags & MRPF_CPOPEN)) {
		return;
	}
	mrp_units[index].flags = flags;
	MOD_USE_COUNT_DEC;
}

int mrp_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	// TODO
	return -1;
}

int mrp_get_info(char *buffer, char **sdtart, off_t offset, int length, int dummy)
{
	// TODO
	int size;

	size = sprintf(buffer, "DECI1 $Revision: 3.10 $ %s %s\n", MRP_PSNET_BUILDDATE, MRP_PSNET_BUILDTIME);
	for (int index = 0; index < ARRAY_SIZE(mrp_units); i++) {
		struct mrp_unit *unit = &mrp_units[i];
		struct mrp_regs *regs = &unit->regs;
		size += sprintf(buffer, "unit%d", i);
		if (unit->flags & MRPF_DETECT) {
			size += sprintf(buffer, " DETECT");
		}
		if (unit->flags & MRPF_VALID) {
			size += sprintf(buffer, " VALID");
		}
		if (unit->flags & MRPF_RESET) {
			size += sprintf(buffer, " RESET");
		}
		if (unit->flags & MRPF_OPENED) {
			size += sprintf(buffer, " OPENED");
		}
		if (unit->flags & MRPF_SBUSY) {
			size += sprintf(buffer, " SBUSY");
		}
		if (unit->flags & MRPF_RDONE) {
			size += sprintf(buffer, " RDONE");
		}
		if (unit->flags & MRPF_REQTAG) {
			size += sprintf(buffer, " REQTAG");
		}
		if (unit->flags & MRPF_CPOPEN) {
			size += sprintf(buffer, " CPOPEN");
		}
		if (unit->flags & MRPF_CPBUSY) {
			size += sprintf(buffer, " CPBUSY");
		}

		size += sprintf(buffer, " nintr=%d", unit->nintr);
		size += sprintf(buffer, "\n");

		if (unit->flags & MRPF_VALID) {
			size += sprintf(buffer, " BID=%04x", regs->bid);
			size += sprintf(buffer, " RST=%04x", regs->bid);
			size += sprintf(buffer, " CPS=%04x", regs->bid);
			size += sprintf(buffer, " CPR=%04x", regs->bid);
			size += sprintf(buffer, " FST=%04x", regs->bid);
			size += sprintf(buffer, " TXC=%04x", regs->bid);
			size += sprintf(buffer, " RXC=%04x", regs->bid);
			size += sprintf(buffer, " F1C=%04x", regs->bid);
			size += sprintf(buffer, " IST=%04x", regs->bid);
			size += sprintf(buffer, " ISP=%04x", regs->bid);
			size += sprintf(buffer, " IER=%04x", regs->bid);
			size += sprintf(buffer, " CSI=%04x", regs->bid);
			size += sprintf(buffer, " FSI=%04x", regs->bid);
			size += sprintf(buffer, " AEO=%04x", regs->bid);
			size += sprintf(buffer, " AFO=%04x", regs->bid);
		}
	}
	
	return size;
}

int mrp_base(unsigned char bus, unsigned char dev_fn, unsigned char where)
{
	int address, base;

	base = (where - 0x10) >> 2;
	if (pcibios_read_config_dword(bus, dev_fn, where, &address)) {
		printk("mrp: can't read config (BASE%d)\n", base);
		return 0;
	}
	if (address & 0x7) {
		printk("mrp: unsupported address type (BASE%d=0x%x)\n", base, address);
		return 0;
	}
	return address & 0xf0;
}

void *mrp_remap(void *base)
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
	return (base & PAGE_MASK) + rc;
}

int mrp_init(void)
{
	// TODO
	return -1;
}

#ifdef MODULE
int init_module(void)
{
	return mrp_init();
}

void cleanup_module(void)
{
	if (mrp_major <= 0) {
		return;
	}
	for (int index = 0; index < 4; index++) {
		struct mrp_unit *mrp = &mrp_units[index];
		if (mrp->flags & MRPF_VALID) {
			mrp->eb5->idk50 |= 0x20;
			mrp->eb5->idk4c &= ~0x40;
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
		if (mrp->eb5)
			vfree(mrp->eb5;
		if (mrp->eb0)
			vfree(mrp->eb0);
		if (mrp->mrpregs)
			vfree(mrp->mrpregs);
	}
	unregister_chrdev(mrp_major, "mrp");
	printl("mrp: unregistered character major %d\n", mrp_major);
	proc_unregister(proc_root, mrp_proc_de);
}
#endif
