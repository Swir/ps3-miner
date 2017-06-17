#ifndef WORK_CPUMINER_H_
#define WORK_CPUMINER_H_

struct work {
	uchar	data[128];
	uchar	hash1[64];
	uchar	midstate[32];
	uchar	target[32];
	uchar	hash[32];
};

enum workio_commands {
	WC_GET_WORK,
	WC_SUBMIT_WORK,
};

struct workio_cmd {
	enum workio_commands	cmd;
	struct thr_info		*thr;
	union {
		struct work	*work;
	} u;
};


void hashmeter(int thr_id, const struct timeval *diff, ulong hashes_done);
bool submit_work(struct thr_info *thr, const struct work *work_in);

#endif // WORK_CPUMINER_H_
