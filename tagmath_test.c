/* See LICENSE file for copyright and license details.
 *
 * Standalone regression test for tagmath.h's tag-bitmask arithmetic. This is
 * the one part of dwm's logic that doesn't need an X11 display to exercise,
 * and it's exactly the part that has produced real bugs before (an
 * off-by-8x array size, and a shift-by-64 in tag_bit_remove caught while
 * writing this file) -- so it gets tested directly instead of only by hand.
 *
 * Build and run with `make test` (no X11/Xft required).
 */
#include <assert.h>
#include <stdio.h>

#include "tagmath.h"

typedef struct TestClient TestClient;
struct TestClient {
	tag_t tags;
	TestClient *next;
};

static void
test_lsb_msb(void)
{
	/* every single bit position, 0..63: this is the exact range that a
	 * `sizeof(tag_t)` (8) vs `sizeof(tag_t) * 8` (64) sized array bug
	 * would silently corrupt for positions 8..63. */
	for (int i = 0; i < TAG_BIT_COUNT; i++) {
		tag_t t = TAG_UNIT << i;
		assert(TAG_LSB(t) == i);
		assert(TAG_MSB(t) == i);
	}

	assert(TAG_LSB(0xF) == 0);
	assert(TAG_MSB(0xF) == 3);
	assert(TAG_LSB(0xC) == 2);          /* 0b1100 */
	assert(TAG_MSB(0xC) == 3);
	assert(TAG_LSB((TAG_UNIT << 63) | TAG_UNIT) == 0);
	assert(TAG_MSB((TAG_UNIT << 63) | TAG_UNIT) == 63);
}

static void
test_bit_insert(void)
{
	assert(tag_bit_insert(0, 0) == 0);
	assert(tag_bit_insert(0x1, 0) == 0x2);
	assert(tag_bit_insert(0x5, 1) == 0x9);        /* 0b101 -> 0b1001 */
	assert(tag_bit_insert(0x3, 2) == 0x3);        /* nothing at/above pos: no-op */
	assert(tag_bit_insert(TAG_UNIT << 5, 0) == TAG_UNIT << 6);
	/* pos == TAG_BIT_COUNT - 2 is the highest pos _tag_insert ever passes
	 * (it refuses pos >= TAG_BIT_COUNT - 1); confirm the boundary itself
	 * is correct rather than only the guard around it. */
	assert(tag_bit_insert(TAG_UNIT << 62, 62) == TAG_UNIT << 63);
}

static void
test_bit_remove(void)
{
	assert(tag_bit_remove(0x1, 0) == 0);
	assert(tag_bit_remove(0x2, 0) == 0x1);
	assert(tag_bit_remove(0x9, 1) == 0x5);        /* inverse of the insert above */
	/* removing the top bit (pos == TAG_BIT_COUNT - 1) is a real, reachable
	 * case (tag_adjacent can shift a selection up to bit 63) and used to
	 * compute `t >> 64`, undefined behavior that happened to read back as
	 * "unchanged" on x86-64/gcc instead of the intended zero. */
	assert(tag_bit_remove(TAG_UNIT << 63, 63) == 0);
	assert(tag_bit_remove((TAG_UNIT << 63) | (TAG_UNIT << 2), 63) == TAG_UNIT << 2);
	assert(tag_bit_remove((TAG_UNIT << 63) | TAG_UNIT, 0) == TAG_UNIT << 62);
}

static void
test_insert_remove_roundtrip(void)
{
	/* tag_bit_remove(tag_bit_insert(t, pos), pos) must recover t exactly,
	 * for any pos that doesn't already have a bit set in t (the precondition
	 * every real caller maintains: you insert a slot before it holds a bit). */
	static const tag_t samples[] = { 0, 0x1, 0x2, 0x5, 0xFF, 0xAAAA, TAG_UNIT << 40 };

	for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); i++) {
		for (int pos = 0; pos < TAG_BIT_COUNT - 1; pos++) {
			tag_t t = samples[i] & ~(TAG_UNIT << pos);
			tag_t inserted = tag_bit_insert(t, pos);
			assert((inserted & (TAG_UNIT << pos)) == 0);
			assert(tag_bit_remove(inserted, pos) == t);
		}
	}
}

static void
test_sync_viewmode(void)
{
	struct { tag_t curtags; ViewMode viewmode; } mon;

	mon.curtags = 0;
	SYNC_VIEWMODE(&mon);
	assert(mon.viewmode == ViewClass);

	mon.curtags = TAG_UNIT;
	SYNC_VIEWMODE(&mon);
	assert(mon.viewmode == ViewTag);

	mon.curtags = TAG_UNIT << 63;
	SYNC_VIEWMODE(&mon);
	assert(mon.viewmode == ViewTag);

	mon.curtags = 0;
	SYNC_VIEWMODE(&mon);
	assert(mon.viewmode == ViewClass);
}

/* Mirrors _tag_insert's/_tag_remove's actual usage pattern: apply the same
 * primitive across every client in a list, not just a single tag_t value. */
static void
test_client_list_roundtrip(void)
{
	TestClient c3 = { 0x4, NULL };        /* tag 2 */
	TestClient c2 = { 0x1, &c3 };         /* tag 0 */
	TestClient c1 = { 0x3, &c2 };         /* tags 0 and 1 */
	int pos = 1;

	tag_t orig[3] = { c1.tags, c2.tags, c3.tags };

	for (TestClient *c = &c1; c; c = c->next)
		c->tags = tag_bit_insert(c->tags, pos);

	assert(c1.tags == 0x5);               /* 0b011 -> 0b101: bit 1 moves to bit 2 */
	assert(c2.tags == 0x1);               /* 0b001 -> 0b001, unaffected (below pos) */
	assert(c3.tags == 0x8);               /* 0b100 -> 0b1000 */

	for (TestClient *c = &c1; c; c = c->next)
		c->tags = tag_bit_remove(c->tags, pos);

	assert(c1.tags == orig[0]);
	assert(c2.tags == orig[1]);
	assert(c3.tags == orig[2]);
}

int
main(void)
{
	test_lsb_msb();
	test_bit_insert();
	test_bit_remove();
	test_insert_remove_roundtrip();
	test_sync_viewmode();
	test_client_list_roundtrip();

	printf("tagmath_test: all tests passed\n");
	return 0;
}
