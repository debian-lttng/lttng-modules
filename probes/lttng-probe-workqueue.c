/* SPDX-License-Identifier: (GPL-2.0 or LGPL-2.1)
 *
 * probes/lttng-probe-workqueue.c
 *
 * LTTng workqueue probes.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * Copyright (C) 2012 Mentor Graphics Corp.
 */

#include <linux/module.h>
#include <linux/idr.h>
#include <lttng-tracer.h>

struct cpu_workqueue_struct;
struct pool_workqueue;

/*
 * Create the tracepoint static inlines from the kernel to validate that our
 * trace event macros match the kernel we run on.
 */
#include <trace/events/workqueue.h>

#include <wrapper/tracepoint.h>

/*
 * Create LTTng tracepoint probes.
 */
#define LTTNG_PACKAGE_BUILD
#define CREATE_TRACE_POINTS
#define TRACE_INCLUDE_PATH instrumentation/events/lttng-module

#include <instrumentation/events/lttng-module/workqueue.h>

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Andrew Gabbasov <andrew_gabbasov@mentor.com>");
MODULE_DESCRIPTION("LTTng workqueue probes");
MODULE_VERSION(__stringify(LTTNG_MODULES_MAJOR_VERSION) "."
	__stringify(LTTNG_MODULES_MINOR_VERSION) "."
	__stringify(LTTNG_MODULES_PATCHLEVEL_VERSION)
	LTTNG_MODULES_EXTRAVERSION);
