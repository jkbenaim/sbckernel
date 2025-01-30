#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <linux/netdevice.h>
#include <linux/ps2mrp.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/stat.h>

static struct mrp_unit mrp_units[4];
static unsigned int mrp_major;
static struct file_operations mrp_fops;
static int mrp_debug = 3;

int mrp_get_info(char *buf, char **start, off_t offset, int len, int unused);
static struct proc_dir_entry mrp_proc_de = {
	.namelen = 3,
	.name = "mrp",
	.mode = S_IFCHR | S_IFIFO | S_ISGID | S_ISVTX | S_IRGRP | S_IWGRP,
	.nlink = 1,
	.get_info = mrp_get_info,
};

void mrp_dump_regs(struct mrpregs *regs)
{
	if (mrp_debug <= 2) return;
	printk("* BID=%04hx RST=%04hx CPS=%04hx CPR=%04hx\n",
		regs->bid, regs->rst, regs->cps, regs->cpr);
	printk("* FST=%04hx TXC=%04hx RXC=%04hx F1C=%04hx\n",
		regs->fst, regs->txc, regs->rxc, regs->f1c);
	printk("* IST=%04hx ISP=%04hx IER=%04hx CSI=%04hx\n",
		regs->ist, regs->isp, regs->ier, regs->csi);
	printk("* FSI=%04hx AEO=%04hx AFO=%04hx\n",
		regs->fsi, regs->aeo, regs->afo);
}

int mrp_send(struct mrp_unit *mrp)
{
	int nw = ((mrp->slen + 3) >> 2);
	int *src = mrp->buf1c;
	volatile int *fifo = &mrp->base2->fifoport;
	int i;
	if (mrp_debug > 1) {
		printk("mrp_send: slen=%d\n", mrp->slen);
	}
	if (mrp_debug > 2) {
		printk("mrp_put_fifo: nw=%d\n", nw);
	}
	for (i = 0; i < mrp->slen; i++) {
		*fifo = *src++;
	}
	mrp->mrpregs->txc = 1;
	mrp->mrpregs->ier |= 0x20;
	mrp->flags |= MRPF_SBUSY;
	return nw;
}

int mrp_recv(struct mrp_unit *mrp)
{
	struct decihdr *decihdr = (struct decihdr *)mrp->recvbuf;
	volatile int *fifo = &mrp->base2->fifoport;
	if (mrp_debug > 1)
		printk("mrp_recv:\n");
	if (mrp->flags & MRPF_RDONE)
		return 0;
	if ((mrp->mrpregs->fst & 1) == 0) {
		while (1) {
			mrp->buf20 = mrp->recvbuf;
			if (mrp_debug > 2)
				printk("mrp_get_fifo: nw=%d\n", 1);
			*(int *)mrp->recvbuf = *fifo;
			if ((mrp->mrpregs->fst & 0x10) != 0) {
				if (mrp_debug > 0)
					printk("mrp_recv: invalid fifo status (%04hx)\n", mrp->mrpregs->fst);
				mrp->mrpregs->rxc = 1;
			}
			mrp->rlen = decihdr->size;
			/* TODO */
		}
	}
}

int mrp_reset(void *mrp)
{
	/* TODO */
	return 0;
}

int mrp_bootp(int mrp)
{
	/* TODO */
	return 0;
}

int mrp_cpr(int a)
{
	/* TODO */
	return 0;
}

int mrp_cps(int a)
{
	/* TODO */
	return 0;
}

void mrp_interrupt(int arg1, void *arg2, struct pt_regs *pt_regs)
{
	/* TODO */
}

int mrp_read(void *f, char *buffer, unsigned count, unsigned *ppos)
{
	/* TODO */
	return 0;
}

int mrp_write(int a, int b, int addr, int size)
{
	/* TODO */
	return 0;
}

int mrp_select(int a, int b, int c, int d)
{
	/* TODO */
	return 0;
}

int mrp_open(int a)
{
	/* TODO */
	return 0;
}

int mrp_release(int a)
{
	/* TODO */
	return 0;
}

int mrp_ioctl(int a, int b, int cmd, int arg)
{
	/* TODO */
	return 0;
}

int mrp_get_info(char *buf, char **start, off_t offset, int len, int unused)
{
	int mylen = 0;
	int i;
	mylen += sprintf(buf, "DECI1 $Revision: 3.10 $ %s %s\n", "Mar 10 1999", "21:15:11");
	for (i=0; i<4; i++) {
		struct mrp_unit *mrp = &mrp_units[i];
		mylen += sprintf(buf+mylen, "unit%d", i);
		if (mrp->flags & MRPF_DETECT)
			mylen += sprintf(buf+mylen, " DETECT");
		if (mrp->flags & MRPF_VALID)
			mylen += sprintf(buf+mylen, " VALID");
		if (mrp->flags & MRPF_RESET)
			mylen += sprintf(buf+mylen, " RESET");
		if (mrp->flags & MRPF_OPENED)
			mylen += sprintf(buf+mylen, " OPENED");
		if (mrp->flags & MRPF_SBUSY)
			mylen += sprintf(buf+mylen, " SBUSY");
		if (mrp->flags & MRPF_RDONE)
			mylen += sprintf(buf+mylen, " RDONE");
		if (mrp->flags & MRPF_REQTAG)
			mylen += sprintf(buf+mylen, " REQTAG");
		if (mrp->flags & MRPF_CPOPEN)
			mylen += sprintf(buf+mylen, " CPOPEN");
		if (mrp->flags & MRPF_CPBUSY)
			mylen += sprintf(buf+mylen, " CPBUSY");
		mylen += sprintf(buf+mylen, " nintr=%d\n", mrp->nintr);
		if (mrp->flags & MRPF_VALID) {
			struct mrpregs *regs = mrp->mrpregs;
			mylen += sprintf(buf+mylen, " BID=%04hx RST=%04hx CPS=%04hx CPR=%04hx FST=%04hx TXC=%04hx RXC=%04hx F1C=%04hx\n",
				regs->bid,
				regs->rst,
				regs->cps,
				regs->cpr,
				regs->fst,
				regs->txc,
				regs->rxc,
				regs->f1c
			);
			mylen += sprintf(buf+mylen, " IST=%04hx ISP=%04hx IER=%04hx CSI=%04hx FSI=%04hx AEO=%04hx AFO=%04hx\n",
				regs->ist,
				regs->isp,
				regs->ier,
				regs->csi,
				regs->fsi,
				regs->aeo,
				regs->afo
			);
		}
	}
	return mylen;
}

int mrp_base(unsigned char bus, unsigned char dev_fn, unsigned char where)
{
	int base = ((where - 0x10) >> 2);
	int val;
	if (pcibios_read_config_dword(bus, dev_fn, where, &val)) {
		printk("mrp: can't read config (BASE%d)\n", base);
		return 0;
	}
	if ((val & 7) == 0) {
		return val & PCI_BASE_ADDRESS_MEM_MASK;
	}
	printk("mrp: unsupported address type (BASE%d=0x%x)\n", base, val);
	return 0;
}

void *mrp_remap(int base)
{
	void *rc;
	if (MAP_NR(base) < MAP_NR(high_memory)) {
		printk("mrp: base < high_memory ?? (base=0x%x)\n", base);
		return 0;
	}
	
	rc = vremap(PAGE_ALIGN(base), PAGE_SIZE);
	if (!rc) {
		printk("mrp: can't vremap (base=0x%x)\n", base);
		return 0;
	}
	return rc + (base & ~PAGE_MASK);
}

int mrp_init(void)
{
	int index;
	int boards_found = 0;
	int iRc;

	if (!pcibios_present()) {
		return 0;
	}
	for (index = 0; index < 4; index++) {
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
		mrp_units[index].mrpregs = rc;

		if (pcibios_read_config_byte(pci_bus, pci_device_fn,
					PCI_INTERRUPT_LINE, &pci_irq_line)) {
			printk("mrp: can't read config (IRQ)\n");
			continue;
		}
		mrp_units[index].irq = pci_irq_line;
		printk("mrp: unit %d at 0x%x,0x%x,0x%x (irq = %d)\n",
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
		rc = kmalloc(0x4000, GFP_KERNEL);
		if (!rc) {
			printk("mrp%d: no space for send buffer\n", index);
			continue; /* yes really */
		}
		mrp->sendbuf = rc;

		rc = kmalloc(0x4000, GFP_KERNEL);
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
		mrp->base0->idk50 &= ~0x20;
		mrp->base0->idk50 |= 0x40000000;
		udelay(10);
		mrp->base0->idk50 &= ~0x40000000;
		mrp->base0->idk50 |= 0x40;
		mrp->mrpregs->ier = 0;
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
	if (!mrp_major) {
		return;
	}
	for (index = 0; index < 4; index++) {
		struct mrp_unit *mrp = &mrp_units[index];
		if (mrp->flags & MRPF_VALID) {
			mrp->base0->idk50 |= 0x20;
			mrp->base0->idk50 &= ~0x40;
			free_irq(mrp->irq, mrp);
		}
		kfree(mrp->sendbuf);
		kfree(mrp->recvbuf);
		kfree(mrp->buf38);
		kfree(mrp->buf40);
		vfree(mrp->base0);
		vfree(mrp->base2);
		vfree(mrp->mrpregs);
	}
	unregister_chrdev(mrp_major, "mrp");
	printk("mrp: unregistered character major %d\n", mrp_major);
	proc_unregister(&proc_root, mrp_proc_de.low_ino);
}
#endif
