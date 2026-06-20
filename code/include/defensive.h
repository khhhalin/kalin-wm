/*
 * defensive.h - Defensive programming assertions and checks
 */

#ifndef DEFENSIVE_H
#define DEFENSIVE_H

#include <wlr/util/log.h>

/* Always evaluate condition even in release, log if false */
#define VERIFY(cond, msg) do { \
	if (!(cond)) { \
		wlr_log(WLR_ERROR, "VERIFY failed: %s at %s:%d", msg, __FILE__, __LINE__); \
	} \
} while (0)

/* Abort if condition fails - use for truly fatal conditions */
#define REQUIRE(cond, msg) do { \
	if (!(cond)) { \
		wlr_log(WLR_ERROR, "REQUIRE failed: %s at %s:%d", msg, __FILE__, __LINE__); \
		abort(); \
	} \
} while (0)

/* Null-check pointer before dereference */
#define NULL_CHECK(ptr, fallback) ((ptr) ? (ptr) : (fallback))

/* Safe division with divisor check */
#define SAFE_DIV(num, denom, fallback) ((denom) != 0 ? ((num) / (denom)) : (fallback))

/* Safe modulo with divisor check */
#define SAFE_MOD(num, denom, fallback) ((denom) != 0 ? ((num) % (denom)) : (fallback))

#endif /* DEFENSIVE_H */
