/*
 *  powctrl.c - DTL-T10000 Power Control
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: powctrl.c,v 1.3 2000/09/26 05:42:37 takemura Exp $
 */


#define __KERNEL_SYSCALLS__

#include <linux/version.h>
#include <linux/config.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
#include <linux/param.h>
#include <linux/fcntl.h>
#include <linux/pci.h>
#include <linux/bios32.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <asm/io.h>
#include <asm/system.h>

#include <linux/ps2mrp.h>
#include <linux/ps2powctrl.h>

void powctrl_system_poweroff(void);
void powctrl_poweroff_request(void);
int powctrl_init(void);

#define	MRP_TIMER_UNIT		(HZ/2)	// .5 sec
#define LED_BLINK_WAIT_HIGH	(1)
#define LED_BLINK_WAIT_LOW	(3)
#define LED_BLINK_OFF		(0)

#define	MRP_POWEROFF_REQ	(1 << 10)
#define	MRP_POWEROFF_ACK	(1 << 3)
#define	MRP_SHUTDOWN_REQ	(1 << 2)
#define MRP_TLED_MASK		(3)
#define MRP_TLED_ORANGE		(0)
#define MRP_TLED_GREEN		(1)
#define MRP_TLED_RED		(2)
#define MRP_TLED_OFF		(3)

#define	MRP_POWCTRL_POWEROFF_DISABLE	1
#define	MRP_POWCTRL_POWEROFF_ENABLE	0

#define	LED_ON	1
#define	LED_OFF	0

const char *POWCTRL_DEVICE_NAME= "powctrl";

static int powctrl_dbglevel = POWCTRL_DBG_NORMAL;
static int powctrl_major = 0;
static struct timer_list powctrl_timer;
static unsigned int powctrl_timer_enable = 0;
static int powctrl_blink = 0;
static int powctrl_blkcnt = 0;
static int powctrl_color = 0;
static struct mrp_unit mrp_unit;

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

static unsigned short powctrl_get_f1c(void)
{
	return mrp_unit.mrpregs->f1c;
}

static void powctrl_set_f1c(unsigned short f1c)
{
	mrp_unit.mrpregs->f1c = f1c;
}

static void powctrl_set_color(unsigned short color)
{
	unsigned short temp;
	temp = powctrl_get_f1c();
	temp &= ~MRP_TLED_MASK;
	temp |= color & MRP_TLED_MASK;
	powctrl_set_f1c(temp);
}

static void powctrl_turn_tled(void)
{
	unsigned short color;
	color = powctrl_get_f1c() & MRP_TLED_MASK;
	if (color != powctrl_color)
		powctrl_set_color(powctrl_color);
	else
		powctrl_set_color(MRP_TLED_OFF);
}

static void powctrl_poweroff_ack(int acknak)
{
	unsigned short temp;

	if (mrp_unit.mrpregs->bid != 0x4126)
		return;

	temp = powctrl_get_f1c();

	if (acknak)
		temp |= MRP_POWEROFF_ACK;
	else
		temp &= ~MRP_POWEROFF_ACK;

	powctrl_set_f1c(temp);
}

void powctrl_poweroff_request(void)
{
	int i, k;
	unsigned short temp;

	temp = powctrl_get_f1c();
	temp |= MRP_SHUTDOWN_REQ;
	powctrl_set_f1c(temp);   

	powctrl_timer_enable = 1;
	for (i = 0; i < 10000; i++)
		for (k = 0; k < 10000; k++);
	// TODO: delay?

	temp = powctrl_get_f1c();
}

void powctrl_system_poweroff(void)
{
	powctrl_poweroff_ack(MRP_POWCTRL_POWEROFF_ENABLE);
}

void powctrl_reset_mrp(void)
{
	mrp_unit.mrpregs->rst = 0x8000;
	udelay(10);
	mrp_unit.mrpregs->fst = 0xc0c0;
	udelay(10);
	mrp_unit.mrpregs->fst = 0;
	mrp_unit.mrpregs->rst = 0;
	udelay(10);
}

void powctrl_reset_pif(void)
{
	int temp;
	temp = mrp_unit.base0->idk50;
	temp &= ~0x20;
	temp |= 0x40000000;
	mrp_unit.base0->idk50 = temp;
	udelay(10);
	temp &= ~0x40000000;
	mrp_unit.base0->idk50 = temp;
	temp = mrp_unit.base0->idk4c;
	temp |= 0x40;
	mrp_unit.base0->idk4c = temp;
	//mrp_unit.mrpregs->ier = 0;
}

static void powctrl_observe_status(unsigned long p)
{
	unsigned short temp;

	if (powctrl_timer_enable) {
		powctrl_reset_pif();
		if (powctrl_blink) {
			if (powctrl_blkcnt >= powctrl_blink) {
				powctrl_turn_tled();
				powctrl_blkcnt = 0;
			} else {
				powctrl_blkcnt++;
			}
		} else {
			powctrl_blkcnt = 0;
		}
		temp = powctrl_get_f1c();
		if (powctrl_dbglevel >= POWCTRL_DBG_ALL)
			printk( "%s: timer interrupt occurs. powstat(%x)\n",
				POWCTRL_DEVICE_NAME, temp);

		if (temp & MRP_POWEROFF_REQ) {
			if (powctrl_dbglevel >= POWCTRL_DBG_MIDIUM)
				printk( "%s: POWEROFF Request Asserted.(%x)\n",
					POWCTRL_DEVICE_NAME, temp);
			return;
		}
	}

	powctrl_timer.function = powctrl_observe_status;
	powctrl_timer.data = (unsigned long)NULL;
	powctrl_timer.expires = jiffies + MRP_TIMER_UNIT;
	add_timer(&powctrl_timer);
}

static int powctrl_ioctl( struct inode *inode, struct file *file,
		       u_int cmd, u_long arg)
{
	int	size = _IOC_SIZE(cmd);
	int	r = 0;
	POWCTRL_RWDATA	rwdata;
	int	value = 0, rslt = 0;

	if (_IOC_TYPE(cmd) != POWCTRL_IOC_MAGIC)
		return -EINVAL;
	if (_IOC_NR(cmd) > POWCTRL_IOC_MAXNR)
		return -EINVAL;

	if (_IOC_DIR(cmd) & _IOC_READ)
		r = verify_area(VERIFY_WRITE, (void *)arg, size);
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		r = verify_area(VERIFY_READ, (void *)arg, size);

	if (r)
		return r;

	if (powctrl_timer_enable)
		powctrl_reset_pif();

	switch (cmd) {
        case POWCTRL_IOCPOWEROFF:
		if (powctrl_dbglevel >= POWCTRL_DBG_MIDIUM)
			printk("%s: Enter Poweroff mode\n",
					POWCTRL_DEVICE_NAME);
		powctrl_poweroff_request();
		return 0;

        case POWCTRL_IOCPOWSTAT:
		if (powctrl_timer_enable)
			rslt = powctrl_get_f1c();
		else
			rslt = -1;
	 	return rslt;

        case POWCTRL_IOCTIMER:
		copy_from_user((void *)&rwdata, (void *)arg, size);
		rslt = powctrl_timer_enable;
		if (rwdata.value)
			powctrl_timer_enable = 1;
		else
			powctrl_timer_enable = 0;

		if (powctrl_dbglevel >= POWCTRL_DBG_MIDIUM)
			printk("%s: timer mode %s to %s\n",
				POWCTRL_DEVICE_NAME,
				rslt ? "enable" : "disable",
				powctrl_timer_enable ? "enable" : "disable");
		powctrl_reset_pif();
		return rslt;

        case POWCTRL_IOC_TLED_COLOR:
		copy_from_user((void *)&rwdata, (void *)arg, size);
		rslt = powctrl_color;
		powctrl_blink = LED_BLINK_OFF;
		powctrl_blkcnt = 0;
		powctrl_color = rwdata.value & MRP_TLED_MASK;
		powctrl_set_color(powctrl_color);
		return rslt;

        case POWCTRL_IOC_TLED_BLINK:
		copy_from_user((void *)&rwdata, (void *)arg, size);
		rslt = powctrl_blink;
		powctrl_blink = LED_BLINK_WAIT_HIGH;
		powctrl_blkcnt = 0;
		powctrl_color = rwdata.value & MRP_TLED_MASK;
		powctrl_set_color(powctrl_color);
		return rslt;

        case POWCTRL_IOCPOWDBG:
	    copy_from_user((void *)&value, (void *)arg, size);
	    rslt = powctrl_dbglevel;
	    powctrl_dbglevel = value;

	    return rslt;
    }

    return -EINVAL;
}


static int powctrl_open(struct inode *inode, struct file *file)
{
	MOD_INC_USE_COUNT;
	return 0;
}


static void powctrl_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
}

static struct file_operations powctrl_fops = {
	.ioctl		= powctrl_ioctl,
	.open		= powctrl_open,
	.release	= powctrl_release,
};



int powctrl_base(unsigned char bus, unsigned char dev_fn, unsigned char where)
{
	int base = ((where - 0x10) >> 2);
	int val;
	if (pcibios_read_config_dword(bus, dev_fn, where, &val)) {
		printk("%s: can't read config (BASE%d)\n", POWCTRL_DEVICE_NAME, base);
		return 0;
	}
	if ((val & 7) == 0) {
		return val & PCI_BASE_ADDRESS_MEM_MASK;
	}
	printk("%s: unsupported address type (BASE%d=0x%x)\n", POWCTRL_DEVICE_NAME, base, val);
	return 0;
}

void *powctrl_remap(int base)
{
	void *rc;
	if (MAP_NR(base) < MAP_NR(high_memory)) {
		printk("%s: base < high_memory ?? (base=0x%x)\n", POWCTRL_DEVICE_NAME, base);
		return 0;
	}
	
	rc = vremap(PAGE_ALIGN(base), PAGE_SIZE);
	if (!rc) {
		printk("%s: can't vremap (base=0x%x)\n", POWCTRL_DEVICE_NAME, base);
		return 0;
	}
	return rc + (base & ~PAGE_MASK);
}

int powctrl_init(void)
{
	int iRc;
	int base0, base2, base3;
	unsigned char pci_bus, pci_device_fn, pci_irq_line;
	void *rc;

	if (!pcibios_present()) {
		return 0;
	}
	printk("%s: start DTL-T10000 Power Control.\n",
				POWCTRL_DEVICE_NAME);
	if (pcibios_find_device(PCI_VENDOR_ID_SONY,
				PCI_DEVICE_ID_SONY_PS2_MRP,
				0, &pci_bus,
				&pci_device_fn)) {
		printk("%s: couldn't find mrp.\n", POWCTRL_DEVICE_NAME);
		return 0;
	}
		
	base0 = powctrl_base(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_0);
	if (!base0) return 0;
	base2 = powctrl_base(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_2);
	if (!base2) return 0;
	base3 = powctrl_base(pci_bus, pci_device_fn, PCI_BASE_ADDRESS_3);
	if (!base3) return 0;
	
	rc = powctrl_remap(base0);
	if (!rc) return 0;
	mrp_unit.base0 = rc;
	
	rc = powctrl_remap(base2);
	if (!rc) return 0;
	mrp_unit.base2 = rc;
	
	rc = powctrl_remap(base3);
	if (!rc) return 0;
	mrp_unit.mrpregs = rc;

	if (pcibios_read_config_byte(pci_bus, pci_device_fn,
				PCI_INTERRUPT_LINE, &pci_irq_line)) {
		printk("%s: can't read config (IRQ)\n", POWCTRL_DEVICE_NAME);
		return 0;
	}
	mrp_unit.irq = pci_irq_line;
	printk("%s: unit %d at 0x%x,0x%x,0x%x (irq = %d)\n",
		POWCTRL_DEVICE_NAME, 0, base0, base2, base3, pci_irq_line);
	mrp_unit.flags |= MRPF_DETECT;
	
	iRc = register_chrdev(0, POWCTRL_DEVICE_NAME, &powctrl_fops);
	if (iRc <= 0) {
		printk("%s: unable to get dynamic major %d\n", POWCTRL_DEVICE_NAME, iRc);
		return 0;
	}
	powctrl_major = iRc;
	printk("%s: registered character major %d\n", POWCTRL_DEVICE_NAME, iRc);

	powctrl_reset_mrp();
	powctrl_reset_pif();

	powctrl_poweroff_ack(MRP_POWCTRL_POWEROFF_DISABLE);
	powctrl_blink = LED_BLINK_OFF;
	powctrl_color = MRP_TLED_ORANGE;
	powctrl_set_color(powctrl_color);

	init_timer(&powctrl_timer);
	powctrl_timer_enable = 1;
	powctrl_timer.function = powctrl_observe_status;
	powctrl_timer.data= (unsigned long)NULL;
	powctrl_timer.expires= jiffies + MRP_TIMER_UNIT;
	add_timer(&powctrl_timer);

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return powctrl_init();
}

void cleanup_module(void)
{
	if( 0 >= powctrl_major)
		return;

	del_timer(&powctrl_timer);

	unregister_chrdev(powctrl_major, POWCTRL_DEVICE_NAME);
	if (dbglevel > POWCTRL_DBG_NONE)
		printk("%s: unregistered character major %d\n",
			POWCTRL_DEVICE_NAME, powctrl_major); 
	vfree(mrp_unit.base0);
	vfree(mrp_unit.base2);
	vfree(mrp_unit.mrpregs);
}
#endif

