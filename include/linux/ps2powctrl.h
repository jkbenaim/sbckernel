/*
 *  powctrl_ioctl.h - Driver for Imagestation Power Control definitions
 *  
 *		Copyright (C) 1998-1999 Sony Corporation
 *		All Right Reserved
 *
 *        First Date: 1999/07/08
 *        Author: sinh@sm.sony.co.jp
 *
 */
#ifndef _LINUX_PS2POWCTRL_H
#define _LINUX_PS2POWCTRL_H
typedef struct 
{
    unsigned short value;
} POWCTRL_RWDATA;

typedef struct 
{
    unsigned short offset;
} POWCTRL_RDATA;

typedef struct 
{
    unsigned short offset;
    unsigned short value;
} POWCTRL_WDATA;


#define POWCTRL_IOC_MAGIC  'b'

#define POWCTRL_IOCREGREAD	_IOW(POWCTRL_IOC_MAGIC, 1, POWCTRL_RDATA)
#define POWCTRL_IOCREGWRITE	_IOW(POWCTRL_IOC_MAGIC, 2, POWCTRL_WDATA)
#define	POWCTRL_IOCPOWEROFF	_IO(POWCTRL_IOC_MAGIC, 3)
#define	POWCTRL_IOCPOWSTAT	_IO(POWCTRL_IOC_MAGIC, 4)
#define	POWCTRL_IOCTIMER	_IOW(POWCTRL_IOC_MAGIC, 5, POWCTRL_RWDATA)
#define	POWCTRL_IOC_TLED_COLOR	_IOW(POWCTRL_IOC_MAGIC, 6, POWCTRL_RWDATA)
#define	POWCTRL_IOC_TLED_BLINK	_IOW(POWCTRL_IOC_MAGIC, 7, POWCTRL_RWDATA)
//#define	POWCTRL_IOC_WLED_COLOR	_IOW(POWCTRL_IOC_MAGIC, 6, POWCTRL_RWDATA)
//#define	POWCTRL_IOC_WLED_BLINK	_IOW(POWCTRL_IOC_MAGIC, 7, POWCTRL_RWDATA)
#define	POWCTRL_IOCPOWDBG	_IOW(POWCTRL_IOC_MAGIC, 8, int)

#define POWCTRL_IOC_MAXNR 8

// for POWCTRL_IOCTIMER
#define	POWCTRL_TIMER_DISABLE	(0)
#define	POWCTRL_TIMER_ENABLE	(1)


// for POWCTRL_IOC_TLED_COLOR
#define	POWCTRL_TLED_ALL	(0)
#define	POWCTRL_TLED_MIXED	(0)
#define	POWCTRL_TLED_YELLOW	(1)
#define	POWCTRL_TLED_GREEN	(1)
#define	POWCTRL_TLED_ORANGE	(2)
#define	POWCTRL_TLED_OFF	(3)

#define	POWCTRL_TLED_BLINK	(4)


// for POWCTRL_IOC_TLED_BLINK
#define	POWCTRL_TLED_MIXED_BLINK	(3)
#define	POWCTRL_TLED_GREEN_BLINK	(2)
#define	POWCTRL_TLED_YELLOW_BLINK	(2)
#define	POWCTRL_TLED_ORANGE_BLINK	(1)

#define	POWCTRL_TLED_BLINK_OFF	(0)


// for POWCTRL_IOC_WLED_COLOR (Workstation Mode for EE)
#if 0
#define	POWCTRL_WLED_GREEN	(1)
#define	POWCTRL_WLED_OFF	(0)
#define	POWCTRL_WLED_BLINK	(4)
#define	POWCTRL_WLED_BLINK_HIGH	(8)

// for POWCTRL_IOC_WLED_BLINK (Workstation Mode for EE)
#define	POWCTRL_WLED_BLINK_HIGH	(8)
#define	POWCTRL_WLED_BLINK_LOW	(4)
#define	POWCTRL_WLED_BLINK_OFF	(0)
#endif

// for POWCTRL_IOCPOWDBG
#define	POWCTRL_DBG_NONE	(0)
#define	POWCTRL_DBG_NORMAL	(1)
#define	POWCTRL_DBG_MIDIUM	(3)
#define	POWCTRL_DBG_ALL		(5)

int powctrl_init(void);
void powctrl_reset_pif(void);
void powctrl_poweroff_request(void);
void powctrl_system_poweroff(void);
#endif
