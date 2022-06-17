/*
 *  ee_powctrl.c - DTL-T10000 Power Control
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: ee_powctrl.c,v 1.3 2000/09/26 05:42:37 takemura Exp $
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
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ioctl.h>
#include <linux/param.h>
#include <linux/fcntl.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#ifndef CONFIG_T10000
#error "ee_powctrl.c: support DTL-T10000 only"
#endif

typedef unsigned char	byte;
typedef unsigned short	half;
typedef unsigned int	word;

typedef volatile byte	vbyte;
typedef volatile half	vhalf;
typedef volatile word	vword;

#include "powctrl_ioctl.h"

void ee_powctrl_system_poweroff(void);
void ee_powctrl_poweroff_request(void);
int ee_powctrl_init(void);

#define	T10K_POWCTRL_IOBASE	(KSEG1+0x1f803800)
#define	T10K_ADD_WSLED		(T10K_POWCTRL_IOBASE+0x1c)
#define	T10K_ADD_POWCTRL	(T10K_POWCTRL_IOBASE+0x1c)

#define	T10K_TIMER_UNIT		(HZ/2)	// .5 sec
#define LED_BLINK_WAIT_HIGH	(1)
#define LED_BLINK_WAIT_LOW	(3)
#define LED_BLINK_OFF		(0)

#define	T10K_WS_LED		(1 << 0)
#define	T10K_POWEROFF_REQ	(1 << 10)
#define	T10K_POWEROFF_ACK	(1 << 2)
#define	T10K_SHUTDOWN_REQ	(1 << 1)

#define	T10K_POWCTRL_POWEROFF_DISABLE	1
#define	T10K_POWCTRL_POWEROFF_ENABLE	0

#define	LED_ON	1
#define	LED_OFF	0

const char	*POWCTRL_DEVICE_NAME= "ee_powctrl";

static int			dbglevel= POWCTRL_DBG_NORMAL;
static int			powctrl_major = 0;
static struct timer_list	powctrl_timer;
static unsigned int		timer_enable= 0;
static int			blink= 0;
static int			blkcnt= 0;

#define	readhw(ad)	(*(volatile u16 *)(ad))
#define	writehw(v,ad)	(*(volatile u16 *)(ad))= (v)


static void ee_powctrl_wsled( int on)
{
	u16	powctrl;

	powctrl= readhw(T10K_ADD_POWCTRL);

	if (on)		powctrl&= ~T10K_WS_LED;
	else		powctrl|= T10K_WS_LED;

	writehw( powctrl, T10K_ADD_POWCTRL);

	return;
}


static void ee_powctrl_turn_wsled(void)
{
	u16	powctrl;

	powctrl= readhw(T10K_ADD_POWCTRL);
	powctrl^= T10K_WS_LED;
	writehw( powctrl, T10K_ADD_POWCTRL);

	return;
}


static void ee_powctrl_poweroff_ack( int newctrl)
{
	u16	powctrl;

	powctrl= readhw(T10K_ADD_POWCTRL);

	if (newctrl)	powctrl|= T10K_POWEROFF_ACK;
	else		powctrl&= ~T10K_POWEROFF_ACK;

	writehw( powctrl, T10K_ADD_POWCTRL);

	return;
}


void ee_powctrl_poweroff_request(void)
{
	int	i, k;
	u16	powctrl;

	powctrl= readhw(T10K_ADD_POWCTRL);
	powctrl|= T10K_SHUTDOWN_REQ;
    
	writehw( powctrl, T10K_ADD_POWCTRL);

	timer_enable= 1;
	for( i= 0; i< 10000; i++)
		for( k= 0; k< 10000; k++);

	powctrl= readhw(T10K_ADD_POWCTRL);

	return;
}


void ee_powctrl_system_poweroff(void)
{
	ee_powctrl_poweroff_ack(T10K_POWCTRL_POWEROFF_ENABLE);
}


static void ee_powctrl_observe_status( unsigned long p)
{
	u16	powctrl;

	if (timer_enable) {
		if (blink) {
			if (blkcnt >= blink) {
				ee_powctrl_turn_wsled();
				blkcnt= 0;
			}
			else
				blkcnt++;
		} else
				blkcnt= 0;

		powctrl= readhw(T10K_ADD_POWCTRL);
		if (dbglevel >= POWCTRL_DBG_ALL)
			printk( "%s: timer interrupt occurs. powstat(%x)\n",
				POWCTRL_DEVICE_NAME, powctrl);

		if (powctrl & T10K_POWEROFF_REQ) {
			if (dbglevel >= POWCTRL_DBG_MIDIUM)
				printk( "%s: POWEROFF Request Asserted.(%x)\n",
					POWCTRL_DEVICE_NAME, powctrl);
			return;
		}
	}

	powctrl_timer.function= ee_powctrl_observe_status;
	powctrl_timer.data= (unsigned long)NULL;
	powctrl_timer.expires= jiffies + T10K_TIMER_UNIT;
	add_timer( &powctrl_timer);

	return;
}


static u16 ee_powctrl_status(void)
{
	u16	powctrl;

	powctrl= readhw( T10K_ADD_POWCTRL);

	return powctrl;
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
		r= verify_area( VERIFY_WRITE, (void *)arg, size);
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		r= verify_area(VERIFY_READ, (void *)arg, size);

	if(r)	return r;


	switch(cmd){
        case POWCTRL_IOCPOWEROFF:
		if (dbglevel >= POWCTRL_DBG_MIDIUM)
			printk("%s: Enter Poweroff mode\n",
					POWCTRL_DEVICE_NAME);
		ee_powctrl_poweroff_request();
		return 0;

        case POWCTRL_IOCPOWSTAT:
		if (timer_enable)
			rslt= ee_powctrl_status();
		else
			rslt= -1;
	 	return rslt;

        case POWCTRL_IOCTIMER:
		copy_from_user((void *)&rwdata, (void *)arg, size);
		rslt= timer_enable;
		if (rwdata.value)	timer_enable= 1;
		else			timer_enable= 0;

		if (dbglevel >= POWCTRL_DBG_MIDIUM)
			printk("%s: timer mode %s to %s\n",
				POWCTRL_DEVICE_NAME,
				rslt ? "enable" : "disable",
				timer_enable ? "enable" : "disable");
		return rslt;

        case POWCTRL_IOC_WLED_COLOR:
		copy_from_user((void *)&rwdata, (void *)arg, size);
		blink= LED_BLINK_OFF;
		blkcnt= 0;
		if (rwdata.value & POWCTRL_WLED_GREEN) {
			if (rwdata.value & POWCTRL_WLED_BLINK_HIGH)
				blink= LED_BLINK_WAIT_HIGH;
			else if (rwdata.value & POWCTRL_WLED_BLINK)
				blink= LED_BLINK_WAIT_LOW;
			ee_powctrl_wsled( LED_ON);
		}
		else
			ee_powctrl_wsled( LED_OFF);
		return rslt;

        case POWCTRL_IOC_WLED_BLINK:
		copy_from_user((void *)&rwdata, (void *)arg, size);
		rslt= blink;
		blink= LED_BLINK_OFF;
		blkcnt= 0;
		if (rwdata.value & POWCTRL_WLED_BLINK_HIGH)
			blink= LED_BLINK_WAIT_HIGH;
		if (rwdata.value & POWCTRL_WLED_BLINK)
			blink= LED_BLINK_WAIT_LOW;
		ee_powctrl_wsled( LED_ON);

		return rslt;

        case POWCTRL_IOCPOWDBG:
	    copy_from_user((void *)&value, (void *)arg, size);
	    rslt= dbglevel;
	    dbglevel= value;

	    return rslt;
    }

    return -EINVAL;
}


static int powctrl_open(struct inode *inode, struct file *file)
{
	MOD_INC_USE_COUNT;
	return(0);
}


static int powctrl_release(struct inode *inode, struct file *file)
{
    MOD_DEC_USE_COUNT;
    return(0);
}

static struct file_operations powctrl_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= powctrl_ioctl,
	.open		= powctrl_open,
	.release	= powctrl_release,
};


int ee_powctrl_init(void)
{
	printk( "%s: start DTL-T10000 Power Control.\n",
				POWCTRL_DEVICE_NAME);

#ifdef PS2POWCTRL_MAJOR
	powctrl_major= PS2POWCTRL_MAJOR;
	if( register_chrdev( powctrl_major,
			POWCTRL_DEVICE_NAME, &powctrl_fops) < 0){
		if (dbglevel > POWCTRL_DBG_NONE)
			printk("%s: unable to assign major [%d]\n",
				POWCTRL_DEVICE_NAME,
				powctrl_major);
		return(0);
	}
#else // dynamic assign
	if(( powctrl_major = register_chrdev( 0,
			POWCTRL_DEVICE_NAME, &powctrl_fops)) <= 0){
		if (dbglevel > POWCTRL_DBG_NONE)
			printk("%s: unable to get dynamic major\n",
				POWCTRL_DEVICE_NAME);
		return(0);
	}
#endif // PS2POWCTRL_MAJOR

	if (dbglevel > POWCTRL_DBG_NONE)
		printk("%s: registered character major %d\n",
			POWCTRL_DEVICE_NAME, powctrl_major); 

	ee_powctrl_poweroff_ack(T10K_POWCTRL_POWEROFF_DISABLE);

	blink= LED_BLINK_WAIT_HIGH;
	ee_powctrl_wsled(LED_ON);

	init_timer(&powctrl_timer);
	timer_enable= 1;
	powctrl_timer.function= ee_powctrl_observe_status;
	powctrl_timer.data= (unsigned long)NULL;
	powctrl_timer.expires= jiffies + T10K_TIMER_UNIT;
	add_timer( &powctrl_timer);

	return(0);
}


#if defined(POWCTRL_MODULE)
int init_module(void)
{
	return( ee_powctrl_init());
}


void cleanup_module(void)
{
	if(0 >= powctrl_major)
		return;

	del_timer(&powctrl_timer);

	unregister_chrdev(powctrl_major, POWCTRL_DEVICE_NAME);
	if (dbglevel > POWCTRL_DBG_NONE)
		printk("%s: unregistered character major %d\n",
			POWCTRL_DEVICE_NAME, powctrl_major); 
}
#endif	/* POWCTRL_MODULE */

