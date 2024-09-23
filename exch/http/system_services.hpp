#pragma once
#include <string>
#include <gromox/authmgr.hpp>
#include <gromox/common_types.hpp>
#include <gromox/mysql_adaptor.hpp>

extern int system_services_run();
extern void system_services_stop();

extern bool (*system_services_judge_ip)(const char *host, std::string &reason);
extern bool (*system_services_judge_user)(const char *);
extern void (*system_services_ban_user)(const char *, int);
extern authmgr_login_t system_services_auth_login;
extern decltype(mysql_adaptor_meta) *system_services_auth_meta;
extern bool (*ss_dnsbl_check)(const char *host);
