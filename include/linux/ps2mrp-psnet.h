#ifndef _LINUX_PS2MRP_PSNET_H
#define _LINUX_PS2MRP_PSNET_H
#include <linux/fs.h>
#include <linux/proc_fs.h>

#define MRP_PSNET_BUILDDATE "Mar 10 1999"
#define MRP_PSNET_BUILDTIME "21:15:11"

#define MRP_UNIT_MAX (4)
#define MRP_MAX_PKT_SIZE (0x4000)

#define mrp_printk(level, fmt, arg...)	\
	if (mrp_debug >= level) {	\
		printk(fmt, ##arg);	\
	}

#define MRP_IOCTL_GT	(0xa14c4754)
#define MRP_IOCTL_RF	(0xa14c5246)
#define MRP_IOCTL_SF	(0xa14c5346)
#define MRP_IOCTL_SY	(0xa14c5359)
#define MRP_IOCTL_DECI	(0xa14c4126)

extern int mrp_debug;

struct base2 {
	volatile int fifoport;
};

struct mrpregs {
	volatile unsigned short bid;	/* board id */
	volatile unsigned short _bid2;
	volatile unsigned short rst;	/* reset */
	volatile unsigned short _rst2;
	volatile unsigned short cps;	/* ??? */
	volatile unsigned short _cps2;
	volatile unsigned short cpr;	/* ??? */
	volatile unsigned short _cpr2;
	volatile unsigned short fst;	/* fifo status */
	volatile unsigned short _fst2;
	volatile unsigned short txc;	/* ??? */
	volatile unsigned short _txc2;
	volatile unsigned short rxc;	/* ??? */
	volatile unsigned short _rxc2;
	volatile unsigned short f1c;	/* field 0x1c ??? */
	volatile unsigned short _f1c2;
	volatile unsigned short ist;	/* interrupt status */
	volatile unsigned short _ist2;
	volatile unsigned short isp;	/* ??? */
	volatile unsigned short _isp2;
	volatile unsigned short ier;	/* interrupt enable */
	volatile unsigned short _ier2;
	volatile unsigned short csi;	/* ??? */
	volatile unsigned short _csi2;
	volatile unsigned short fsi;	/* ??? */
	volatile unsigned short _fsi2;
	volatile unsigned short aeo;	/* ??? */
	volatile unsigned short _aeo2;
	volatile unsigned short afo;	/* ??? */
	volatile unsigned short _afo2;
};

struct base0 {
	char _pad0[0x4c];
	unsigned int idk4c;
	unsigned int idk50;
};

#define RINGBUF_SIZE (0x1000)
#define RINGBUF_INDEX_MASK (0xfff)

struct ringbuf_s {
	unsigned char buf[RINGBUF_SIZE];
	unsigned int write_index;
	unsigned int read_index;
	unsigned int count;
};

static inline void ringbuf_put(struct ringbuf_s *ring, unsigned char x)
{
	size_t index;
	index = ring->write_index & RINGBUF_INDEX_MASK;

	ring->buf[index] = x;
	ring->write_index ++;
	ring->count ++;
}

static inline unsigned char ringbuf_get(struct ringbuf_s *ring)
{
	unsigned char rc;
	size_t index;
	index = ring->read_index & RINGBUF_INDEX_MASK;

	rc = ring->buf[index];
	ring->read_index ++;
	ring->count --;
	return rc;
}

struct mrp_unit {
	int flags;
	int irq;
	struct base0 *base0;
	struct base2 *base2;
	struct mrpregs *regs;
	void *sendbuf;
	void *recvbuf;
	void *deci_out;
	void *deci_in;
	int slen;
	int rlen;
	struct wait_queue *wake_queue4;
	struct wait_queue *wake_queue3;
	int nintr;
	void *buf38;
	int nbytes38;
	void *buf40;
	int nbytes40;
	int int48;
	struct ringbuf_s sendring;
	struct ringbuf_s recvring;
	struct wait_queue *wake_queue1;
	struct wait_queue *wake_queue2;
};

struct decihdr_s {
	unsigned short magic;
	unsigned short size;
	unsigned int category;
	unsigned short priority;
	unsigned short reply;
	unsigned char tag;
	unsigned char acktag;
	unsigned char ackcode;
	unsigned char pad;
	unsigned int crsv;
	unsigned short cid;
	unsigned short seq;
	unsigned int req;
	unsigned int cksum;
} __attribute__((packed));

#define DECI_MAGIC (0xa14c)

static inline unsigned int
deci_cksum(unsigned int *hdr)
{
	unsigned int rc;
	rc = hdr[0];
	rc += hdr[1];
	rc += hdr[2];
	rc += hdr[3];
	rc += hdr[4];
	rc += hdr[5];
	rc += hdr[6];
	return rc;
}

enum mrp_flags {
	MRPF_DETECT = 0x0001,
	MRPF_VALID  = 0x0002,
	MRPF_RESET  = 0x0004,
	MRPF_OPENED = 0x0008,
	MRPF_SBUSY  = 0x0010,
	MRPF_RDONE  = 0x0020,
	MRPF_REQTAG = 0x0040,
	MRPF_CPOPEN = 0x0080,
	MRPF_CPBUSY = 0x0100
};

enum mrp_fst_e {
	MRP_FSTF_0001 = 0x0001,
	MRP_FSTF_0002 = 0x0002,
	MRP_FSTF_0004 = 0x0004,
	MRP_FSTF_0008 = 0x0008,
	MRP_FSTF_0010 = 0x0010,
	MRP_FSTF_0020 = 0x0020,
	MRP_FSTF_0040 = 0x0040,
	MRP_FSTF_0080 = 0x0080,
	MRP_FSTF_0100 = 0x0100,
	MRP_FSTF_0200 = 0x0200,
	MRP_FSTF_0400 = 0x0400,
	MRP_FSTF_0800 = 0x0800,
	MRP_FSTF_1000 = 0x1000,
	MRP_FSTF_2000 = 0x2000,
	MRP_FSTF_4000 = 0x4000,
	MRP_FSTF_8000 = 0x8000
};

enum idk4c_flags {
	IDK4CF_0001 = 0x0001,
	IDK4CF_0002 = 0x0002,
	IDK4CF_0004 = 0x0004,
	IDK4CF_0008 = 0x0008,
	IDK4CF_0010 = 0x0010,
	IDK4CF_0020 = 0x0020,
	IDK4CF_0040 = 0x0040,
	IDK4CF_0080 = 0x0080,
	IDK4CF_0100 = 0x0100,
	IDK4CF_0200 = 0x0200,
	IDK4CF_0400 = 0x0400,
	IDK4CF_0800 = 0x0800,
	IDK4CF_1000 = 0x1000,
	IDK4CF_2000 = 0x2000,
	IDK4CF_4000 = 0x4000,
	IDK4CF_8000 = 0x8000
};

enum idk50_flags {
	IDK50F_00000020 = 0x00000020,
	IDK50F_40000000 = 0x40000000
};

enum mrp_stat_flags {
	MRP_STATF_CPR	= 0x01,
	MRP_STATF_CPS	= 0x02,
	MRP_STATF_BOOTP	= 0x04,
	MRP_STATF_RESET	= 0x08,
	MRP_STATF_RECV	= 0x10,
	MRP_STATF_WAKE	= 0x20,
	MRP_STATF_40	= 0x40,
	MRP_STATF_80	= 0x80
};

enum mrp_reset_flags {
	MRP_RESETF_0001 = 0x0001,
	MRP_RESETF_0002 = 0x0002,
	MRP_RESETF_0004 = 0x0004,
	MRP_RESETF_0008 = 0x0008,
	MRP_RESETF_0010 = 0x0010,
	MRP_RESETF_0020 = 0x0020,
	MRP_RESETF_0040 = 0x0040,
	MRP_RESETF_0080 = 0x0080,
	MRP_RESETF_0100 = 0x0100,
	MRP_RESETF_0200 = 0x0200,
	MRP_RESETF_0400 = 0x0400,
	MRP_RESETF_0800 = 0x0800,
	MRP_RESETF_1000 = 0x1000,
	MRP_RESETF_2000 = 0x2000,
	MRP_RESETF_4000 = 0x4000,
	MRP_RESETF_8000 = 0x8000
};

enum mrp_cpr_flags {
	MRP_CPRF_0001 = 0x0001,
	MRP_CPRF_0002 = 0x0002,
	MRP_CPRF_0004 = 0x0004,
	MRP_CPRF_0008 = 0x0008,
	MRP_CPRF_0010 = 0x0010,
	MRP_CPRF_0020 = 0x0020,
	MRP_CPRF_0040 = 0x0040,
	MRP_CPRF_0080 = 0x0080,
	MRP_CPRF_0100 = 0x0100,
	MRP_CPRF_0200 = 0x0200,
	MRP_CPRF_0400 = 0x0400,
	MRP_CPRF_0800 = 0x0800,
	MRP_CPRF_1000 = 0x1000,
	MRP_CPRF_2000 = 0x2000,
	MRP_CPRF_4000 = 0x4000,
	MRP_CPRF_8000 = 0x8000
};

static inline unsigned
mrp_put_fifo(struct mrp_unit *mrp, unsigned int *buf, unsigned nbytes)
{
	volatile struct base2 *base2 = mrp->base2;
	unsigned loops = 0;
	unsigned nw = (nbytes + 3) >> 2;
	
	mrp_printk(3, "mrp_put_fifo: nw=%d\n", nw);
	
	switch (nw & 7) {
	case 7:	base2->fifoport = *buf++;
	case 6:	base2->fifoport = *buf++;
	case 5:	base2->fifoport = *buf++;
	case 4:	base2->fifoport = *buf++;
	case 3:	base2->fifoport = *buf++;
	case 2:	base2->fifoport = *buf++;
	case 1:	base2->fifoport = *buf++;
	default:
	case 0: loops = nw >> 3;
	}
	while (loops) {
		base2->fifoport = buf[0];
		base2->fifoport = buf[1];
		base2->fifoport = buf[2];
		base2->fifoport = buf[3];
		base2->fifoport = buf[4];
		base2->fifoport = buf[5];
		base2->fifoport = buf[6];
		base2->fifoport = buf[7];
		buf += 8;
		loops--;
	}
	mrp->regs->txc = 1;
	mrp->regs->ier |= MRP_STATF_WAKE;
	mrp->flags |= MRPF_SBUSY;

	return nw;
}

static inline void
mrp_get_fifo(struct mrp_unit *mrp, unsigned int *buf, unsigned nbytes)
{
	volatile struct base2 *base2 = mrp->base2;
	unsigned loops = 0;
	unsigned nw = (nbytes + 3) >> 2;
	
	mrp_printk(3, "mrp_get_fifo: nw=%d\n", nw);

	switch (nw & 7) {
	case 7: *buf++ = base2->fifoport;
	case 6: *buf++ = base2->fifoport;
	case 5: *buf++ = base2->fifoport;
	case 4: *buf++ = base2->fifoport;
	case 3: *buf++ = base2->fifoport;
	case 2: *buf++ = base2->fifoport;
	case 1: *buf++ = base2->fifoport;
	default:
	case 0: loops = nw >> 3;
	}
	while (loops) {
		buf[0] = base2->fifoport;
		buf[1] = base2->fifoport;
		buf[2] = base2->fifoport;
		buf[3] = base2->fifoport;
		buf[4] = base2->fifoport;
		buf[5] = base2->fifoport;
		buf[6] = base2->fifoport;
		buf[7] = base2->fifoport;
		buf += 8;
		loops--;
	}
	mrp->regs->rxc = 1;
}

extern int mrp_get_info(
	char *buffer,
	char **start,
	off_t offset,
	int length,
	int dummy);
extern int mrp_read(
	struct inode *inode,
	struct file *file,
	char *buf,
	int len);
extern int mrp_write(
	struct inode *inode,
	struct file *file,
	const char *buf,
	int len);
extern int mrp_select(
	struct inode *inode,
	struct file *file,
	int sel_type,
	select_table *wait);
extern int mrp_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg);
extern int mrp_open(
	struct inode *inode,
	struct file *file);
extern void mrp_release(
	struct inode *inode,
	struct file *file);
#endif
