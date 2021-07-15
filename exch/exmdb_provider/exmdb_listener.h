#pragma once
#include <cstdint>
extern void exmdb_listener_init(const char *host, uint16_t port);
extern int exmdb_listener_run(const char *config_path);
extern int exmdb_listener_trigger_accept();
extern int exmdb_listener_stop();
