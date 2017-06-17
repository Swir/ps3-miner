/*
 * Author of hack for PS3
 *
 * (c) 2014 N-Engine
 *
 * The licence are the same as above.
 *
 * Based on version from :
 *
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 *
 */
#include "include.h"

#define PROGRAM_NAME		"minerd"
#define DEF_RPC_URL			"http://IP:PORT/"
#define DEF_RPC_USERNAME	"rpcuser"
#define DEF_RPC_PASSWORD	"rpcpass"
#define DEF_RPC_USERPASS	DEF_RPC_USERNAME ":" DEF_RPC_PASSWORD

static inline void drop_policy(void) { }
static inline void affine_to_cpu(int id, int cpu) { }

enum sha256_algos {
	ALGO_SCRYPT,		/* scrypt(1024,1,1) */
};

static const char *algo_names[] = { [ALGO_SCRYPT] = "scrypt", };

bool opt_debug = false;
bool opt_protocol = false;
bool want_longpoll = false;
bool have_longpoll = false;
bool use_syslog = false;
static bool opt_quiet = false;
static int opt_retries = 10;
static int opt_fail_pause = 30;
int opt_scantime = 5;
static json_t *opt_config;
static const bool opt_time = true;
static enum sha256_algos opt_algo = ALGO_SCRYPT;
static int opt_n_threads=1;
static int num_processors=2;
static int num_cell_spu=6; /* the number of SPU cores for Cell/BE (normally 6) */
static char *rpc_url;
static char *rpc_userpass;
static char *rpc_user, *rpc_pass;
struct thr_info *thr_info;
static int work_thr_id;
int longpoll_thr_id;
struct work_restart *work_restart = NULL;
pthread_mutex_t time_lock;


struct option_help {
	const char	*name;
	const char	*helptext;
};

static struct option_help options_help[] = {
	{ "help",
	  "(-h) Display this help text" },

	{ "config FILE",
	  "(-c FILE) JSON-format configuration file (default: none)\n"
	  "See example-cfg.json for an example configuration." },

	{ "algo XXX",
	  "(-a XXX) USE *ONLY* scrypt (e.g. --algo scrypt) WITH TENEBRIX\n" 
	  "\tscrypt is the default now" },

	{ "quiet",
	  "(-q) Disable per-thread hashmeter output (default: off)" },

	{ "debug",
	  "(-D) Enable debug output (default: off)" },

	{ "no-longpoll",
	  "Disable X-Long-Polling support (default: enabled)" },

	{ "protocol-dump",
	  "(-P) Verbose dump of protocol-level activities (default: off)" },

	{ "retries N",
	  "(-r N) Number of times to retry, if JSON-RPC call fails\n"
	  "\t(default: 10; use -1 for \"never\")" },

	{ "retry-pause N",
	  "(-R N) Number of seconds to pause, between retries\n"
	  "\t(default: 30)" },

	{ "scantime N",
	  "(-s N) Upper bound on time spent scanning current work,\n"
	  "\tin seconds. (default: 5)" },

#ifdef HAVE_SYSLOG_H
	{ "syslog",
	  "Use system log for output messages (default: standard error)" },
#endif

	{ "threads N",
	  "(-t N) Number of miner threads (default: 1)" },

	{ "url URL",
	  "URL for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_URL ")" },

	{ "userpass USERNAME:PASSWORD",
	  "Username:Password pair for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_USERPASS ")" },

	{ "user USERNAME",
	  "(-u USERNAME) Username for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_USERNAME ")" },

	{ "pass PASSWORD",
	  "(-p PASSWORD) Password for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_PASSWORD ")" },
};

static struct option options[] = {
	{ "algo", 1, NULL, 'a' },
	{ "config", 1, NULL, 'c' },
	{ "debug", 0, NULL, 'D' },
	{ "help", 0, NULL, 'h' },
	{ "no-longpoll", 0, NULL, 1003 },
	{ "pass", 1, NULL, 'p' },
	{ "protocol-dump", 0, NULL, 'P' },
	{ "quiet", 0, NULL, 'q' },
	{ "threads", 1, NULL, 't' },
	{ "retries", 1, NULL, 'r' },
	{ "retry-pause", 1, NULL, 'R' },
	{ "scantime", 1, NULL, 's' },
#ifdef HAVE_SYSLOG_H
	{ "syslog", 0, NULL, 1004 },
#endif
	{ "url", 1, NULL, 1001 },
	{ "user", 1, NULL, 'u' },
	{ "userpass", 1, NULL, 1002 },

	{ }
};

#include "scrypt-cell-spu.h"

/* Each SPU core is processing 8 hashes as once and needs 8x memory */
#define SCRATCHBUF_SIZE (131583 * 8)

static void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	int thr_id = mythr->id;
	uint32_t max_nonce = 0xffffff;
	unsigned char *scratchbuf = NULL;
	int restart = 0;

	TRACE_DEBUG( "miner_thread(%d) malloc\n", thr_id );

	scratchbuf = malloc(SCRATCHBUF_SIZE);
	max_nonce = 0xffff;

	while (1)
	{
		struct work work __attribute__((aligned(128)));
		unsigned long hashes_done;
		struct timeval tv_start, tv_end, diff;
		int diffms;
		uint64_t max64;
		bool rc;

		TRACE_DEBUG( "miner_thread(%d) enter loop\n", thr_id );

		/* obtain new work from internal workio thread */
		if ( unlikely(!get_work(mythr, &work)) )
		{
			TRACE_ERROR( "work retrieval failed, exiting "
				"mining thread %d", mythr->id);
			goto out;
		}

		hashes_done = 0;
		gettimeofday(&tv_start, NULL);

		/* scan nonces for a proof-of-work hash */
		TRACE_DEBUG( "miner_thread(%d) using algo scrypt\n", thr_id );

		// -- calc hash to spu

		scanhash_spu_args *argp = (scanhash_spu_args *)
			(((uintptr_t)scratchbuf + 127) & ~(uintptr_t)127);

		spe_stop_info_t stop_info;
		unsigned int entry = 0; // SPE_DEFAULT_ENTRY;

		TRACE_DEBUG( "miner_thread(%d) memcpy argp\n", thr_id );

		memcpy( argp->data, work.data, sizeof(work.data) );
		memcpy( argp->target, work.target, sizeof(work.target) );

		TRACE_DEBUG( "miner_thread(%d) memcpy argp done\n", thr_id );

		argp->max_nonce = max_nonce;
		argp->hashes_done = 0;

		//work_restart[thr_id].
		restart = 0;

		TRACE_DEBUG( "miner_thread(%d) spe_context_run\n", thr_id );

		spe_context_run(mythr->spe_context, &entry, 0, argp,
				(void *)&/*work_restart[thr_id].*/ restart, &stop_info);

		hashes_done = argp->hashes_done;
		memcpy(work.data, argp->data, sizeof(work.data));
		rc = stop_info.result.spe_exit_code;
		// -- calc result from spu

		TRACE_DEBUG("scanhash_scrypt()\n");
		rc = scanhash_scrypt( thr_id, work.data, scratchbuf, work.target, max_nonce, &hashes_done );

		/* record scanhash elapsed time */
		gettimeofday(&tv_end, NULL);
		timeval_subtract(&diff, &tv_end, &tv_start);

		hashmeter(thr_id, &diff, hashes_done);

		/* adjust max_nonce to meet target scan time */
		diffms = diff.tv_sec * 1000 + diff.tv_usec / 1000;
		if (diffms > 0)
		{
			max64 =
			   ((uint64_t)hashes_done * opt_scantime * 1000) / diffms;
			if (max64 > 0xfffffffaULL)
				max64 = 0xfffffffaULL;
			max_nonce = max64;
		}

		/* if nonce found, submit work */
		if (rc && !submit_work(mythr, &work))
			break;
	}

	TRACE_DEBUG( "miner_thread(%d) loop done\n", thr_id );

out:

	TRACE_DEBUG( "miner_thread(%d) done\n", thr_id );

	return NULL;
}

static void restart_threads(void)
{
	int i;
	for (i = 0; i < opt_n_threads; i++)
		work_restart[i].restart = 1;
}

static void atexit_run() {
	TRACE_DEBUG("Program exiting\n");
	stop_debug();
	netDeinitialize();
};

int main (int argc, char *argv[])
{
	struct thr_info *thr;
	int i;
	atexit(atexit_run);

	num_cell_spu		= 6;
	num_processors		= 2;
	opt_n_threads		= num_processors + num_cell_spu;
	opt_algo			= ALGO_SCRYPT;
	rpc_url				= strdup( DEF_RPC_URL );
	rpc_userpass		= strdup( DEF_RPC_USERPASS );
	rpc_user			= strdup( DEF_RPC_USERNAME );
	rpc_pass			= strdup( DEF_RPC_PASSWORD );

	netInitialize();
	start_debug("IP",PORT);

	if (!ppu_init(1)) { 
		TRACE_ERROR("Can't init spe\n");
		return 0;
	}

	TRACE_DEBUG("CpuMiner started\n");

	TRACE_DEBUG("Step 2\n");

	thr_info = calloc(opt_n_threads + 2, sizeof(*thr));
	if (!thr_info)
		return 1;

	TRACE_DEBUG("Step 6\n");

	/* init workio thread info */
	work_thr_id = 0;
	thr = &thr_info[work_thr_id];
	thr->id = work_thr_id;

	TRACE_DEBUG("Step 7\n");
	longpoll_thr_id = -1;

	/* start mining threads */
	i = 0;
	thr = &thr_info[i];
	thr->id = i;

	TRACE_DEBUG("Step 10-%d\n",i);

	miner_thread(thr);

	return 0;
}
