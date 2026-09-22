/* See LICENSE file for copyright and license details. */

/* Pure tag-bitmask arithmetic, kept free of X11/Xlib so it can be linked into
 * both dwm and tagmath_test without pulling in a display connection. If you
 * change one of these definitions, keep tagmath_test.c in sync and re-run
 * `make test`.
 */

typedef unsigned long long tag_t;

#define TAG_UNIT                1ULL
#define TAG_BIT_COUNT           (sizeof(tag_t) * 8)
#define TAG_LSB(T)              __builtin_ctzll(T)          /* T must be nonzero */
#define TAG_MSB(T)              (TAG_BIT_COUNT - 1 - __builtin_clzll(T)) /* T must be nonzero */

/* ViewClass = 0 so a freshly ecalloc'd Monitor starts consistent with curtags == 0 */
typedef enum { ViewClass, ViewTag } ViewMode;

/* re-derives viewmode from curtags; call after every assignment to M->curtags */
#define SYNC_VIEWMODE(M)        ((M)->viewmode = (M)->curtags ? ViewTag : ViewClass)

/* Opens a zero bit at position pos in t, shifting bit pos and everything
 * above it up by one. Used to make room for a newly created tag slot.
 * pos == TAG_BIT_COUNT - 1 would shift bit 63 out past the top of a 64-bit
 * tag_t (t >> pos << (pos+1) is a shift-by-64, undefined behavior in C); no
 * caller currently reaches that pos, but it's guarded here too so this
 * primitive is correct on its own, independent of callers' guards. */
static inline tag_t
tag_bit_insert(tag_t t, int pos)
{
	tag_t above = pos + 1 >= TAG_BIT_COUNT ? 0 : (t >> pos) << (pos + 1);
	tag_t rightmask = t & ((TAG_UNIT << pos) - 1);
	return above | rightmask;
}

/* Removes bit pos from t, shifting everything above it down by one to close
 * the gap. Used when a tag slot is deleted or reordered.
 * pos == TAG_BIT_COUNT - 1 (removing the top tag) is a real, reachable case
 * (e.g. after enough tag_adjacent shifts land the selection on bit 63) and
 * needs the same shift-by-64 guard as tag_bit_insert above. */
static inline tag_t
tag_bit_remove(tag_t t, int pos)
{
	tag_t above = pos + 1 >= TAG_BIT_COUNT ? 0 : (t >> (pos + 1)) << pos;
	tag_t rightmask = t & ((TAG_UNIT << pos) - 1);
	return above | rightmask;
}
