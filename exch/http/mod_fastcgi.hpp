#pragma once
#include <cstdint>
#include <ctime>
#include <gromox/clock.hpp>
#include <gromox/common_types.hpp>
#include <gromox/fileio.h>
#include <gromox/http.hpp>
#define RESPONSE_TIMEOUT				-1
#define RESPONSE_WAITING				0
#define RESPONSE_AVAILABLE				1

struct FASTCGI_NODE;
struct fastcgi_context;
struct http_context;
using HTTP_CONTEXT = http_context;
using FASTCGI_CONTEXT = fastcgi_context;

extern void mod_fastcgi_init(int context_num, gromox::time_duration exec_timeout);
extern int mod_fastcgi_run();
extern void mod_fastcgi_stop();
extern http_status mod_fastcgi_take_request(http_context *);
BOOL mod_fastcgi_check_responded(HTTP_CONTEXT *phttp);
BOOL mod_fastcgi_relay_content(HTTP_CONTEXT *phttp);
extern void mod_fastcgi_insert_ctx(http_context *);
int mod_fastcgi_check_response(HTTP_CONTEXT *phttp);
BOOL mod_fastcgi_read_response(HTTP_CONTEXT *phttp);
extern bool mod_fastcgi_is_in_charge(const http_context *);
