#pragma once
#include <gromox/authmgr.hpp>
#include <gromox/common_types.hpp>
#include <gromox/defs.h>
#include <gromox/mysql_adaptor.hpp>
#include "../mysql_adaptor/sql2.hpp"

extern BOOL (*system_services_lang_to_charset)(const char*, char*);
extern authmgr_login_t system_services_auth_login;
extern authmgr_login_t2 system_services_auth_login_token;
#define E(s) extern decltype(mysql_adaptor_ ## s) *system_services_ ## s;
E(check_same_org)
E(get_domain_groups)
E(get_domain_ids)
E(get_domain_info)
E(get_domain_users)
E(get_group_users)
E(get_homedir)
E(get_maildir)
E(get_mlist_ids)
E(get_mlist_memb)
E(get_org_domains)
E(get_user_displayname)
E(get_user_ids)
E(get_user_privilege_bits)
E(get_username_from_id)
E(set_timezone)
E(set_user_lang)
E(setpasswd)
E(scndstore_hints)
E(meta)
#undef E
extern int (*system_services_add_timer)(const char *, int);
