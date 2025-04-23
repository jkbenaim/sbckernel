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

int mrp_debug = 1;

static struct proc_dir_entry mrp_proc_de = {
	0, 3, "mrp",
	S_IFREG | S_IRUSR | S_IRGRP| S_IROTH,
	1, 0, 0, 0,
	NULL,
	mrp_get_info
};

static struct file_operations mrp_fops = {
	.read		= mrp_read,
	.write		= mrp_write,
	.select		= mrp_select,
	.ioctl		= mrp_ioctl,
	.open		= mrp_open,
	.release	= mrp_release
};

unsigned int mrp_major = 0;

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
        if ((i = verify_area(VERIFY_READ, from, n)) != 0)
                return i;
        memcpy_fromfs(to, from, n);
        return 0;
}

static inline unsigned long copy_to_user(void *to, const void *from, unsigned long n)
{
        int i;
        if ((i = verify_area(VERIFY_WRITE, to, n)) != 0)
                return i;
        memcpy_tofs(to, from, n);
        return 0;
}
#endif

void mrp_dump_regs(struct mrp_unit *mrp)
{
	typeof(mrp->regs) regs = mrp->regs;
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
}

int mrp_send(struct mrp_unit *mrp)
{
	mrp_printk(2, "mrp_send: slen=%d\n", mrp->slen);

	return mrp_put_fifo(mrp, mrp->deci_out, mrp->slen);
}

int mrp_recv(struct mrp_unit *mrp)
{
	__label__ out_error;
	struct decihdr_s *hdr;
	
	mrp_printk(2, "mrp_recv:\n");
	if (!(mrp->flags & MRPF_RDONE)) {
		return 0;
	}
	if (mrp->regs->fst & MRP_FSTF_0001) {
		mrp_printk(1, "mrp_recv: invalid fifo status (%04x)\n", mrp->regs->fst);
		goto out_error;
	}

	do {
		mrp->deci_in = hdr = (struct decihdr_s *) mrp->recvbuf;	// woof!

		mrp_get_fifo(mrp, mrp->recvbuf, 4);
		if (!(mrp->regs->fst & MRP_FSTF_0010)) {
			mrp_printk(1, "mrp_recv: invalid fifo status (%04x)\n", mrp->regs->fst);
			goto out_error;
		}

		mrp->rlen = hdr->size;
		mrp->deci_in += 4;

		if ( (hdr->magic != DECI_MAGIC) || (hdr->size > MRP_MAX_PKT_SIZE) ) {
			if (mrp->regs->fst & MRP_FSTF_0001) {
				mrp_printk(1, "mrp_recv: bad mag/len (%04x/%04x)\n",
					hdr->magic,
					hdr->size
				);
				goto out_error;
			}
			mrp_printk(1, "mrp_recv: rescanning (%04x/%04x)\n",
				hdr->magic,
				hdr->size
			);
		}
	} while ( (hdr->magic != DECI_MAGIC) || (hdr->size > MRP_MAX_PKT_SIZE) );

	mrp_get_fifo(mrp, mrp->deci_in, mrp->rlen - 4);
	if (hdr->cksum == deci_cksum((unsigned int *) hdr)) {
		mrp_printk(2, "mrp_recv: len=%d\n", mrp->rlen);
		mrp->flags |= MRPF_RDONE;
		wake_up(&mrp->wake_queue4);
	} else {
		mrp_printk(1, "mrp_recv: bad sum\n");
		mrp->regs->ier |= MRP_STATF_RECV;
	}
	
	return 1;

out_error:
	mrp->regs->rxc = 1;
	mrp->regs->ier |= MRP_STATF_RECV;
	return -1;
}

int mrp_reset(struct mrp_unit *mrp)
{
	unsigned long flags;
	volatile struct mrpregs *regs = mrp->regs;

	// note: on smp kernels, we should use lock_kernel/unlock_kernel instead of save_flags/cli/restore_flags

	mrp_printk(2, "mrp_reset:\n");

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
	return 0;
}

int mrp_bootp(struct mrp_unit *mrp)
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
			cpr = 0x0f00;
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
	return 0;
}

void mrp_cpr(struct mrp_unit *mrp)
{
	if (mrp->recvring.count <= 0xfff) {
		ringbuf_put(&mrp->recvring, mrp->regs->cpr);
		mrp->regs->csi = 1;
		mrp->regs->fsi = 2;
		mrp->regs->ier |= MRP_STATF_CPR;
		wake_up(&mrp->wake_queue1);
	}
}

void mrp_cps(struct mrp_unit *mrp)
{
	if (mrp->flags & MRPF_CPBUSY) {
		if (mrp->sendring.count > 0) {
			mrp->flags |= MRPF_CPBUSY;
			mrp->regs->cpr = ringbuf_get(&mrp->sendring);
			mrp->regs->fsi = 1;
			mrp->regs->ier |= MRP_STATF_CPS;
			wake_up(&mrp->wake_queue2);
		}
	}
}

void mrp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	unsigned short stat;
	int index;
	struct mrp_unit *mrp;
	mrp = (struct mrp_unit *) dev_id;

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
	stat = mrp->regs->ist & mrp->regs->ier;
	if (mrp_debug >= 2) {
		mrp_printk(2, "mrp_interrupt: stat=0x%04x\n", stat);
		mrp_dump_regs(mrp);
	}

	if (stat & (MRP_STATF_RECV | MRP_STATF_WAKE)) {
		if (stat & MRP_STATF_RESET) {
			mrp->regs->rst &= (MRP_RESETF_2000 | MRP_RESETF_1000);
			mrp_reset(mrp);
		} else if (stat & MRP_STATF_BOOTP) {
			mrp->regs->ier &= ~MRP_STATF_BOOTP;
			mrp_bootp(mrp);
		} else if (stat & MRP_STATF_CPR) {
			mrp->regs->ier &= ~MRP_STATF_CPR;
			mrp_cpr(mrp);
		} else if (stat & MRP_STATF_CPS) {
			mrp->regs->ier &= ~MRP_STATF_CPS;
			mrp->regs->csi = 2;
			mrp->flags &= ~MRPF_CPBUSY;
			mrp_cps(mrp);
			wake_up(&mrp->wake_queue2);
		} else if (stat) {
			printk("mrp%d: unexpected interrupt (stat=0x%x)\n", index, stat);
			mrp->regs->ier = 0;
		}
	} else {
		if (stat & MRP_STATF_WAKE) {
			mrp->regs->ier &= ~MRP_STATF_WAKE;
			mrp->flags &= ~MRPF_SBUSY;
			wake_up(&mrp->wake_queue3);
		}
		if (stat & MRP_STATF_RECV) {
			mrp->regs->ier &= ~MRP_STATF_RECV;
			mrp_recv(mrp);
		}
	}

	restore_flags(flags);
}

int mrp_read(struct inode *inode, struct file *file, char *buf, int nbytes)
{
	struct mrp_unit *mrp;
	int counter, index, rc;

	index = MAJOR(inode->i_rdev) & 3;
	mrp = &mrp_units[index];

	mrp_printk(2, "mrp_read: count=%d\n", nbytes);

	if (index > 3) {
		return -ENODEV;
	}
	if ((mrp->flags & MRPF_VALID) == 0) {
		return -ENODEV;
	}
	if (MAJOR(inode->i_rdev) & 0x40) {
		if ((mrp->flags & MRPF_OPENED) == 0) {
			return -ENODEV;
		}
		if (mrp->flags & MRPF_RESET) {
			return -EIO;
		}
		if (nbytes >= MRP_MAX_PKT_SIZE) {
			return -EINVAL;
		}
	} else if (mrp->flags & MRPF_CPOPEN) {
		/* not a regular inode major, and not CPOPEN'd. */
		/* this is the red path in IDA. */
		return -ENODEV;
	}

	cli();

	rc = verify_area(VERIFY_WRITE, buf, nbytes);
	if (rc != 0)
		return rc;
	
	/* 0x8000bc5 */
	for (counter = 0; counter < nbytes; counter++) {
		/* top of loop at loc_8000c63 */
		/* loc_8000bd8 */
		while (mrp->recvring.count == 0) {
			/* 8000be5 */
			if (file->f_flags & O_NONBLOCK) {
				return -EWOULDBLOCK;
			}
			interruptible_sleep_on(&mrp->wake_queue1);
			if (current->signal & ~current->blocked) {
				return -EINTR;
			}
		}
		*buf++ = ringbuf_get(&mrp->recvring);
	}
	/* 8000c71 */
	mrp->regs->ier |= MRP_STATF_CPR;
	return counter;
}

int mrp_write(struct inode *inode, struct file *file, const char *buf, int len)
{
	/* TODO */
	return 0;
}

int mrp_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
	int index, major;
	struct mrp_unit *mrp;

	major = MAJOR(inode->i_rdev);
	index = major & 3;

	if (index > 3) {
		return 0;
	}
	
	mrp = &mrp_units[index];

	if (major & 0x40) {
		switch (sel_type) {
		case SEL_IN:
			if (mrp->recvring.count > 0) {
				return 1;
			}
			select_wait(&mrp->wake_queue1, wait);
			break;
		case SEL_OUT:
			if (mrp->sendring.count <= (RINGBUF_SIZE - 2)) {
				return 1;
			}
			select_wait(&mrp->wake_queue2, wait);
			break;
		}
	} else {
		if (sel_type == SEL_IN) {
			if (mrp->flags & (MRPF_RDONE | MRPF_REQTAG)) {
				return 1;
			}
			select_wait(&mrp->wake_queue4, wait);
		}
		if (sel_type == SEL_OUT) {
			if ((mrp->flags & MRPF_SBUSY) == 0) {
				return 1;
			}
			select_wait(&mrp->wake_queue3, wait);
		}
	}
	
	return 0;
}

int mrp_open(struct inode *inode, struct file *file)
{
	int bid, flags, index, major;
	struct mrp_unit *mrp;

	major = MAJOR(inode->i_rdev);
	index = major & 3;
	mrp = &mrp_units[index];

	mrp_printk(2, "mrp_open: index=%d\n", index);

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
	MOD_INC_USE_COUNT;

	return 0;
}

void mrp_release(struct inode *inode, struct file *file)
{
	int index, major;
	int flags;

	major = MAJOR(inode->i_rdev);
	index = major & 3;
	flags = mrp_units[index].flags;

	mrp_printk(2, "mrp_release: index=%d\n", index);
	
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
	MOD_DEC_USE_COUNT;
}

int mrp_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int index, major;
	struct mrp_unit *mrp;

	major = MAJOR(inode->i_rdev);
	index = major & 3;
	mrp = &mrp_units[index];

	mrp_printk(2, "mrp_ioctl: cmd=0x%x arg=0x%lx\n", cmd, arg);

	if (index > 3) {
		return -ENODEV;
	}

	if ((mrp->flags & MRPF_VALID) == 0) {
		return -ENODEV;
	}

	if (MAJOR(inode->i_rdev) & 0x40) {
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

int mrp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int index, size;

	size = sprintf(buffer, "DECI1 $Revision: 3.10 $ %s %s\n", MRP_PSNET_BUILDDATE, MRP_PSNET_BUILDTIME);
	for (index = 0; index < MRP_UNIT_MAX; index++) {
		struct mrp_unit *mrp = &mrp_units[index];
		typeof(mrp->regs) regs = mrp->regs;
		size += sprintf(buffer, "unit%d", index);
		if (mrp->flags & MRPF_DETECT) {
			size += sprintf(buffer, " DETECT");
		}
		if (mrp->flags & MRPF_VALID) {
			size += sprintf(buffer, " VALID");
		}
		if (mrp->flags & MRPF_RESET) {
			size += sprintf(buffer, " RESET");
		}
		if (mrp->flags & MRPF_OPENED) {
			size += sprintf(buffer, " OPENED");
		}
		if (mrp->flags & MRPF_SBUSY) {
			size += sprintf(buffer, " SBUSY");
		}
		if (mrp->flags & MRPF_RDONE) {
			size += sprintf(buffer, " RDONE");
		}
		if (mrp->flags & MRPF_REQTAG) {
			size += sprintf(buffer, " REQTAG");
		}
		if (mrp->flags & MRPF_CPOPEN) {
			size += sprintf(buffer, " CPOPEN");
		}
		if (mrp->flags & MRPF_CPBUSY) {
			size += sprintf(buffer, " CPBUSY");
		}

		size += sprintf(buffer, " nintr=%d", mrp->nintr);
		size += sprintf(buffer, "\n");

		if (mrp->flags & MRPF_VALID) {
			size += sprintf(buffer, " BID=%04x", regs->bid);
			size += sprintf(buffer, " RST=%04x", regs->bid);
			size += sprintf(buffer, " CPS=%04x", regs->bid);
			size += sprintf(buffer, " CPR=%04x", regs->bid);
			size += sprintf(buffer, " FST=%04x", regs->bid);
			size += sprintf(buffer, " TXC=%04x", regs->bid);
			size += sprintf(buffer, " RXC=%04x", regs->bid);
			size += sprintf(buffer, " F1C=%04x\n", regs->bid);
			size += sprintf(buffer, " IST=%04x", regs->bid);
			size += sprintf(buffer, " ISP=%04x", regs->bid);
			size += sprintf(buffer, " IER=%04x", regs->bid);
			size += sprintf(buffer, " CSI=%04x", regs->bid);
			size += sprintf(buffer, " FSI=%04x", regs->bid);
			size += sprintf(buffer, " AEO=%04x", regs->bid);
			size += sprintf(buffer, " AFO=%04x\n", regs->bid);
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

void *mrp_remap(unsigned int base)
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
	int index = 0;
	int boards_found = 0;
	int iRc;

	memset(mrp_units, 0, sizeof(mrp_units));
	
	if (!pcibios_present()) {
		return 0;
	}

	for (index = 0; index < MRP_UNIT_MAX; index++) {
		int base0, base2, base3;
		unsigned char pci_bus, pci_device_fn, pci_irq_line;
		void *rc;
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

		rc = mrp_remap(base0);
		if (!rc) continue;
		mrp_units[index].base0 = rc;
		
		rc = mrp_remap(base2);
		if (!rc) continue;
		mrp_units[index].base2 = rc;
		
		rc = mrp_remap(base3);
		if (!rc) continue;
		mrp_units[index].regs = rc;

		if (pcibios_read_config_byte(pci_bus, pci_device_fn,
					PCI_INTERRUPT_LINE, &pci_irq_line)) {
			printk("mrp: can't read config (IRQ)\n");
			continue;
		}
		mrp_units[index].irq = pci_irq_line;
		printk("mrp: unit %d at 0x%x,0x%x,0x%x (irc = %d)\n",
			index, base0, base2, base3, pci_irq_line);
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

	proc_register_dynamic(&proc_root, &mrp_proc_de);
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
	proc_unregister(&proc_root, mrp_proc_de.low_ino);
}
#endif
