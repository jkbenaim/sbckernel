#ifndef _LINUX_PS2_MRP_H
#define _LINUX_PS2_MRP_H

struct base2 {
	int fifoport;
};

struct mrpregs {
	short bid;
	short _pad02;
	short rst;
	short _pad06;
	short cps;
	short _pad0a;
	short cpr;
	short _pad0e;
	short fst;
	short _pad12;
	short txc;
	short _pad16;
	short rxc;
	short _pad1a;
	short f1c;
	short _pad1e;
	short ist;
	short _pad22;
	short isp;
	short _pad26;
	short ier;
	short _pad2a;
	short csi;
	short _pad2e;
	short fsi;
	short _pad32;
	short aeo;
	short _pad36;
	short afo;
	short _pad3a;
};

struct base0 {
	char _pad0[0x4c];
	int idk4c;
	int idk50;
};

enum mrp_flags {
	MRPF_DETECT = 1,
	MRPF_VALID = 2,
	MRPF_RESET = 4,
	MRPF_OPENED = 8,
	MRPF_SBUSY = 16,
	MRPF_RDONE = 32,
	MRPF_REQTAG = 64,
	MRPF_CPOPEN = 128,
	MRPF_CPBUSY = 0x100
};

struct mrp_unit {
	int flags;
	int irq;
	struct base0 *base0;
	struct base2 *base2;
	struct mrpregs *mrpregs;
	void *sendbuf;
	void *recvbuf;
	int *buf1c;
	void *buf20;
	int slen;
	int rlen;
	void *wake_queue_4;
	void *wake_queue_3;
	int nintr;
	void *buf38;
	int nbytes38;
	void *buf40;
	int nbytes40;
	int int48;
	unsigned char ringbuf2[0x1000];
	int unused_104c;
	unsigned int ringbuf2_ptr_2;
	unsigned int ringbuf2_ptr_1;
	unsigned int ringbuf[0x1000];
	unsigned int ringbuf_idx_1;
	int unused_205c;
	unsigned int ringbuf_idx_2;
	void *wake_queue;
	void *wake_queue_2;
};

#define DECI_MAGIC	(0xa14c)
struct decihdr {
	unsigned short magic;
	unsigned short size;
	unsigned int category;
	unsigned short priority;
	unsigned short rep;
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

#endif
