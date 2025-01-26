// SPDX-License-Identifier: GPL-2.0-only WITH linking exception
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <gromox/config_file.hpp>
#include <gromox/defs.h>
#include <gromox/flusher_common.h>
#include <gromox/paths.h>
#include <gromox/svc_loader.hpp>
#include <gromox/util.hpp>
#include <libHX/string.h>
#include <sys/types.h>
#include <utility>
#include "aux.hpp"
#include "parser.hpp"
#define FLUSHER_VERSION     0x00000001
#define MAX_CIRCLE_NUMBER   0x7FFFFFFF

using namespace gromox;

namespace {

struct SERVICE_NODE {
	void			*service_addr;
	std::string service_name;
};

struct FLH_PLUG_ENTITY {
	~FLH_PLUG_ENTITY();
	CANCEL_FUNCTION flush_cancel = nullptr;
	bool completed_init = false;
};

}

static BOOL flusher_load_plugin();
static int flusher_increase_max_ID();
	
static std::unique_ptr<FLH_PLUG_ENTITY> g_flusher_plug;
static bool g_can_register;
static size_t g_max_queue_len;
static std::atomic<int> g_current_ID;

void flusher_init(size_t queue_len) try
{
	g_flusher_plug = std::make_unique<FLH_PLUG_ENTITY>();
	g_flusher_plug->flush_cancel = NULL;
	g_max_queue_len = queue_len;
} catch (const std::bad_alloc &) {
}

int flusher_run()
{
	if (NULL == g_flusher_plug) {
		mlog(LV_ERR, "flusher: failed to allocate memory for FLUSHER");
		return -3;
	}
	if (!flusher_load_plugin())
		return -2;
	if (g_current_ID < 0) {
		mlog(LV_ERR, "flusher: flush ID error, should be larger than 0");
		return -4;
	}
	return 0;
}

/*
 *  put the context into the flusher's queue
 *  @param
 *      pcontext    indicate the context object
 *  @return
 *      TRUE    OK to put
 *      FALSE   fail to put
 */
BOOL flusher_put_to_queue(SMTP_CONTEXT *pcontext) try
{
	FLUSH_ENTITY e;
	if (0 == pcontext->flusher.flush_ID) {
		pcontext->flusher.flush_ID = flusher_increase_max_ID();
	}
	e.pconnection    = &pcontext->connection;
	e.penvelope      = &pcontext->menv;
	e.pflusher       = &pcontext->flusher;
	e.pstream        = &pcontext->stream;
	e.context_ID     = pcontext->context_id;
	e.pcontext       = pcontext;
	e.command_protocol = pcontext->command_protocol;
	message_enqueue_handle_workitem(e);
	return true;
} catch (const std::bad_alloc &) {
	return false;
}

/*
 *  cancel a flushed mail parts
 *  @param
 *      pcontext [in]   indicate the context object
 */
void flusher_cancel(SMTP_CONTEXT *pcontext)
{
	if (NULL == g_flusher_plug->flush_cancel) {
		return;
	}   
	FLUSH_ENTITY entity;
	entity.pconnection  = &pcontext->connection;
	entity.penvelope    = &pcontext->menv;
	entity.pflusher     = &pcontext->flusher;
	entity.pstream      = &pcontext->stream;

	g_flusher_plug->flush_cancel(&entity);
}

static BOOL flusher_load_plugin()
{
	g_can_register = true; /* so message_enqueue can set g_current_ID at start */
	auto main_result = FLH_LibMain(PLUGIN_INIT);
	g_can_register = false;
	if (!main_result) {
		mlog(LV_ERR, "flusher: failed to execute init in flusher plugin");
		return FALSE;
	}
	g_flusher_plug->completed_init = true;
	return TRUE;
}

void flusher_stop()
{
	g_flusher_plug.reset();
}

FLH_PLUG_ENTITY::~FLH_PLUG_ENTITY()
{
	if (completed_init && !FLH_LibMain(PLUGIN_FREE)) {
		mlog(LV_ERR, "flusher: error executing Flusher_LibMain with "
			   "FLUSHER_LIB_FREE");
		return;
	}
}

static int flusher_increase_max_ID()
{
	int current, next;
	do {
		current = g_current_ID.load();
		next = current >= INT32_MAX ? 1 : current + 1;
	} while (!g_current_ID.compare_exchange_strong(current, next));
	return next;
}

void flusher_set_flush_ID(int ID)
{
	/*
	 * FLH can dictate the starting value at PLUGIN_INIT;
	 * at other times, flusher.cpp is the one telling FLH what IDs to use
	 * (via flusher.cpp setting pcontext->flusher.flush_ID).
	 */
	if (g_can_register)
		g_current_ID = ID;
}

BOOL flusher_register_cancel(CANCEL_FUNCTION cancel_func)
{
	if (!g_can_register || g_flusher_plug->flush_cancel != nullptr)
		return FALSE;
	g_flusher_plug->flush_cancel = cancel_func;
	return TRUE;
}
