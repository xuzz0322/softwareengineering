#pragma once
#include "db.hpp"
#include "httplib.h"
#include <atomic>
void configure_routes(httplib::Server& server);
void billing_loop(std::atomic<bool>& stop);
void bootstrap_passwords();
