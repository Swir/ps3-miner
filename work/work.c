#include "../include.h"


void hashmeter(int thr_id, const struct timeval *diff,
		      unsigned long hashes_done)
{
	double khashes, secs;

	khashes = hashes_done / 1000.0;
	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);

	if (!opt_quiet)
		TRACE_DEBUG( "thread %d: %lu hashes, %.2f khash/sec",
		       thr_id, hashes_done,
		       khashes / secs);
}

static bool jobj_binary(const json_t *obj, const char *key,
			void *buf, size_t buflen)
{
	const char *hexstr;
	json_t *tmp;

	tmp = json_object_get(obj, key);
	if (unlikely(!tmp)) {
		TRACE_ERROR( "JSON key '%s' not found", key);
		return false;
	}
	hexstr = json_string_value(tmp);
	if (unlikely(!hexstr)) {
		TRACE_ERROR( "JSON key '%s' is not a string", key);
		return false;
	}
	if (!hex2bin(buf, hexstr, buflen))
		return false;

	return true;
}

static bool work_decode(const json_t *val, struct work *work)
{
	if (unlikely(!jobj_binary(val, "midstate",
			 work->midstate, sizeof(work->midstate)))) {
		TRACE_ERROR( "JSON inval midstate");
		goto err_out;
	}

	if (unlikely(!jobj_binary(val, "data", work->data, sizeof(work->data)))) {
		TRACE_ERROR( "JSON inval data");
		goto err_out;
	}

	if (unlikely(!jobj_binary(val, "hash1", work->hash1, sizeof(work->hash1)))) {
		TRACE_ERROR( "JSON inval hash1");
		goto err_out;
	}

	if (unlikely(!jobj_binary(val, "target", work->target, sizeof(work->target)))) {
		TRACE_ERROR( "JSON inval target");
		goto err_out;
	}

	memset(work->hash, 0, sizeof(work->hash));

	return true;

err_out:
	return false;
}

static bool submit_upstream_work(CURL *curl, const struct work *work)
{
	char *hexstr = NULL;
	json_t *val, *res;
	char s[345];
	bool rc = false;

	/* build hex string */
	hexstr = bin2hex(work->data, sizeof(work->data));
	if (unlikely(!hexstr)) {
		TRACE_ERROR( "submit_upstream_work OOM");
		goto out;
	}

	/* build JSON-RPC request */
	sprintf(s,
	      "{\"method\": \"getwork\", \"params\": [ \"%s\" ], \"id\":1}\r\n",
		hexstr);

	if (opt_debug)
		TRACE_DEBUG( "DBG: sending RPC call: %s", s);

	/* issue JSON-RPC request */
	val = json_rpc_call(curl, rpc_url, rpc_userpass, s, false, false);
	if (unlikely(!val)) {
		TRACE_ERROR( "submit_upstream_work json_rpc_call failed");
		goto out;
	}

	res = json_object_get(val, "result");

	TRACE_DEBUG( "PROOF OF WORK RESULT: %s",
	       json_is_true(res) ? "true (yay!!!)" : "false (booooo)");

	json_decref(val);

	rc = true;

out:
	free(hexstr);
	return rc;
}

static const char *rpc_req =
	"{\"method\": \"getwork\", \"params\": [], \"id\":0}\r\n";

static bool get_upstream_work(CURL *curl, struct work *work)
{
	json_t *val;
	bool rc;

	TRACE_DEBUG("get_upstream_work() want_longpoll: %d\n", (int)want_longpoll);

	val = json_rpc_call(curl, rpc_url, rpc_userpass, rpc_req,
			    want_longpoll, false);

	if (!val) {
		TRACE_ERROR("json_rpc_call() empty return\n");
		return false;
	}

	rc = work_decode(json_object_get(val, "result"), work);

	json_decref(val);

	return rc;
}

static void workio_cmd_free(struct workio_cmd *wc)
{
	if (!wc)
		return;

	switch (wc->cmd) {
	case WC_SUBMIT_WORK:
		free(wc->u.work);
		break;
	default: /* do nothing */
		break;
	}

	memset(wc, 0, sizeof(*wc));	/* poison */
	free(wc);
}

static struct work* workio_get_work(struct workio_cmd *wc, CURL *curl)
{
	struct work *ret_work;
	int failures = 0;

	ret_work = calloc(1, sizeof(*ret_work));
	if (!ret_work)
		return 0;

	/* obtain new work from bitcoin via JSON-RPC */
	while (!get_upstream_work(curl, ret_work))
	{
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries)))
		{
			TRACE_ERROR( "json_rpc_call failed, terminating workio thread");
			free(ret_work);
			return 0;
		}

		/* pause, then restart work-request loop */
		TRACE_ERROR( "json_rpc_call failed, retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}

	return ret_work;
}

static bool workio_submit_work(struct workio_cmd *wc, CURL *curl)
{
	int failures = 0;

	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(curl, wc->u.work)) {
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
			TRACE_ERROR( "...terminating workio thread");
			return false;
		}

		/* pause, then restart work-request loop */
		TRACE_ERROR( "...retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}

	return true;
}

static struct work* workio_thread(struct workio_cmd* wc)
{
	//struct thr_info *mythr = userdata;
	static CURL *curl = 0;
	struct work* work=0;

	TRACE_DEBUG("workio_thread()\n");

	if (!curl) {
		curl = curl_easy_init();
	}

	if (unlikely(!curl)) {
		TRACE_ERROR( "CURL initialization failed");
		return NULL;
	}

	/* process workio_cmd */
	switch (wc->cmd) {
		case WC_GET_WORK:
			work = workio_get_work(&wc, curl);
		break;
		case WC_SUBMIT_WORK:
			workio_submit_work(&wc, curl);
		break;
		default:		/* should never happen */
			work = false;
		break;
	}

	return work;
}

static bool get_work(struct thr_info *thr, struct work *work)
{
	struct workio_cmd *wc;
	struct work *work_heap;

	TRACE_DEBUG("get_work()\n");

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));

	if (!wc) {
		TRACE_DEBUG("get_work() - calloc failed\n");
		return false;
	}

	wc->cmd = WC_GET_WORK;
	wc->thr = thr;

	TRACE_DEBUG("get_work() - workio_thread()\n");

	/* send work request to workio thread */
	if( !(work_heap=workio_thread( wc )) ) {
		TRACE_ERROR("get_work() - workio_thread() failed\n");
		workio_cmd_free(wc);
		return false;
	}

	if (!work_heap) {
		TRACE_ERROR("get_work() - work_heap failed\n");
		return false;
	}

	TRACE_DEBUG("get_work() - memcpy work\n");

	/* copy returned work into storage provided by caller */
	memcpy( work, work_heap, sizeof(*work) );
	free( work_heap );

	return true;
}

static bool submit_work(struct thr_info *thr, const struct work *work_in)
{
	struct workio_cmd *wc;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (!wc)
		return false;

	wc->u.work = malloc(sizeof(*work_in));
	if (!wc->u.work)
		goto err_out;

	wc->cmd = WC_SUBMIT_WORK;
	wc->thr = thr;
	memcpy(wc->u.work, work_in, sizeof(*work_in));

	/* send solution to workio thread */
	//if (!tq_push(thr_info[work_thr_id].q, wc))
	//	goto err_out;
	workio_thread(wc);

	return true;

err_out:
	workio_cmd_free(wc);
	return false;
}
