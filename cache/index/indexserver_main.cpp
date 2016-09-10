/*
 * indexserver_main.cpp
 *
 *  Created on: 26.05.2015
 *      Author: mika
 */

#include "cache/index/indexserver.h"
#include "cache/index/reorg_strategy.h"
#include "cache/common.h"
#include "util/configuration.h"
#include "util/log.h"
#include <signal.h>
#include <iostream>

IndexServer *instance = nullptr;

void termination_handler(int signum) {
	if (signum == SIGSEGV) {
		printf("Segmentation fault. Stacktrace:\n%s", CacheCommon::get_stacktrace().c_str());
		exit(1);
	}
	else {
		instance->stop();
	}
}

void set_signal_handler() {
	struct sigaction new_action, old_action, ignore;

	/* Set up the structure to specify the new action. */
	new_action.sa_handler = termination_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	ignore.sa_handler = SIG_IGN;
	sigemptyset(&ignore.sa_mask);
	ignore.sa_flags = 0;

	sigaction(SIGINT, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction(SIGINT, &new_action, NULL);
	sigaction(SIGHUP, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction(SIGHUP, &new_action, NULL);
	sigaction(SIGTERM, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction(SIGTERM, &new_action, NULL);
	sigaction(SIGSEGV, NULL, &old_action);
		sigaction(SIGSEGV, &new_action, NULL);

	sigaction(SIGPIPE, NULL, &old_action);
		sigaction(SIGPIPE,&ignore,NULL);
}

int main(void) {
	CacheCommon::set_uncaught_exception_handler();
	set_signal_handler();
	Configuration::loadFromDefaultPaths();

	Log::logToStream(Configuration::get("log.level","info"), &std::cerr);

	auto cfg = IndexConfig::fromConfiguration();

	instance = new IndexServer(cfg);
	instance->run();
	return 0;
}

