/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "tagmath.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            (C->mon->viewmode == ViewTag ? C->tags & C->mon->curtags \
								 : C->class == C->mon->curcls)
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNormal, SchemeClass, SchemeTag,
	   SchemeClntLbl, SchemeUrgent, SchemeStatus,
	   SchemeLayout, SchemeNmaster, SchemeMfact }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
	   NetWMFullscreen, NetActiveWindow, NetWMWindowType,
	   NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkClass, ClkDesktop, ClkTag, ClkLayout, ClkLayoutParam,
	   ClkClientList, ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
	int i;
	tag_t t;
	float f;
	const void *v;
	struct {
		short s1, s2;
	} s;
} Arg;

typedef struct {
	uint click;
	uint mask;
	uint button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct LayoutParams LayoutParams;
struct LayoutParams {
	int nmaster;
	float mfact;
	uint lt_idx;
};

typedef struct Class Class;
struct Class {
	Class *next;

	char name[256];
	int w_name;

	LayoutParams params;
};

/* Matches a client's real WM_CLASS against `class` and, if `rename` is
 * non-empty, uses `rename` as the client's *grouping key* instead of its real
 * WM_CLASS (see _class_find_or_create): this is a merge/alias mechanism, not
 * just a display label. Two different `class` entries that share the same
 * `rename` land in the same Class bucket, one bar entry, one LayoutParams --
 * that's how e.g. "st-256color" and plain "st" are made to behave as one
 * class below. Giving two unrelated apps the same `rename` by accident will
 * merge their tag/layout state the same way. */
typedef struct {
	const char *class;
    const char *rename;
	LayoutParams params;
} ClassRule;

typedef struct Monitor Monitor;
typedef struct Desktop Desktop;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Client *snext;
	Window win;

	Monitor *mon;
	Class *class;
	tag_t tags;
};

typedef struct {
	uint mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	Monitor *next;
	Window barwin;

	Client *clients, *stack, *sel;
	Class *curcls, *prevcls;
	tag_t curtags, prevtags;
	ViewMode viewmode;

	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */

	char ltsymbol[16];
	int topbar;
	uint showbar;

	int x_class_ellipsis_l;
	int x_class_ellipsis_r;
	int x_tag_ellipsis_l;
	int x_tag_ellipsis_r;
	tag_t tag_ellipsis_l;
	tag_t tag_ellipsis_r;
	int x_layout;
	int x_layout_param;
	int x_status_text;
	int x_client_ellipsis_l;
	int x_client_ellipsis_r;
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	tag_t tags;
	int isfloating;
	int monitor;
} Rule;

/* function declarations */
static void applyrules(Client *c, const char *class, const char *instance);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void banish_pointer(const Arg *arg);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void class_adjacent(const Arg *arg);
static void class_select(const Arg *arg);
static void class_stack(const Arg *arg);
static void class_swap(const Arg *arg);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clear_status_text(const Arg *arg);
static void clientmessage(XEvent *e);
static void client_remove_tags(const Arg *arg);
static void client_select(const Arg *arg);
static void client_select_urg(const Arg *arg);
static void client_stack(const Arg *arg);
static void client_swap(const Arg *arg);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, uint size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void group_adjacent(const Arg *arg);
static void group_append(const Arg *arg);
static void group_insert(const Arg *arg);
static void group_select(const Arg *arg);
static void group_stack(const Arg *arg);
static void group_swap(const Arg *arg);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void reload(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static void tagmon(const Arg *arg);
static void tag_adjacent(const Arg *arg);
static void tag_remove(const Arg *arg);
static void tag_select(const Arg *arg);
static void tag_set(const Arg *arg);
static void tag_swap(const Arg *arg);
static void tag_stack(const Arg *arg);
static void tag_toggle_c(const Arg *arg);
static void tag_toggle_m(const Arg *arg);
static void tag_view(const Arg *arg);
static void tile_l(Monitor *m);
static void tile_r(Monitor *m);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int lrpad_2;
static int (*xerrorxlib)(Display *, XErrorEvent *);
static uint numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;
static Class *classes;
static LayoutParams tag_lt_params[TAG_BIT_COUNT];

/* configuration, allows nested code to access above variables */
#include "config.h"

static char stext[BAR_STATUS_LENGTH];
const char ellipsis_l[] = "<";
const char ellipsis_r[] = ">";
const char urgent_v[] = "Urg";
int w_ellipsis_l, w_ellipsis_r;
int w_urgent_v;
int w_clabels[LENGTH(clabels)];
char tlabels[TAG_BIT_COUNT][5];
int w_tlabels[TAG_BIT_COUNT];

struct {
	int x;
	Class *cls;
} classclick[BAR_CLASS_MAX];
struct {
	int x;
	tag_t tag;
} tagclick[BAR_TAG_MAX];
struct {
	int x;
	Client *c;
} urgentclick[BAR_URGENT_MAX];
struct {
	int x;
	Client *c;
} clientclick[BAR_CLIENT_MAX];

const char dwm_version[] = "dwm " VERSION;
int w_dwm_version;
int return_code = EXIT_SUCCESS;

LayoutParams *
_layout_params(Monitor *m)
{
	if (m->viewmode == ViewTag && m->curtags) {
		int lsb = TAG_LSB(m->curtags);
		return &tag_lt_params[lsb];
	} else {
		if (m->curcls)
			return &m->curcls->params;
	}
	return &default_lt_params;
}

const Layout *
_current_layout(Monitor *m)
{
	LayoutParams *p = _layout_params(m);
	return &layouts[p->lt_idx];
}

void
_class_detach(Class *cls)
{
	Class **cpp;
	for (cpp = &classes; *cpp && *cpp != cls; cpp = &(*cpp)->next);
	*cpp = cls->next;
}

void
_class_delete_if_empty(Client *c)
{
	Class *cls = c->class;
	for (Monitor *m = mons; m; m = m->next) {
		for (Client *c2 = m->clients; c2; c2 = c2->next) {
			if (c2 != c && c2->class == cls)
				return;
		}
	}

	for (Monitor *m = mons; m; m = m->next) {
		if (m->curcls == cls)
			m->curcls = m->prevcls;
		else if (m->prevcls == cls)
			m->prevcls = NULL;
	}

	_class_detach(cls);
	free(cls);
}

Class *
_class_find_or_create(const char *class)
{
	const char *name = class;
	const LayoutParams *params = NULL;
	for (int i = 0; i < LENGTH(crules); i++) {
		if (strcmp(class, crules[i].class) == 0) {
			name = strlen(crules[i].rename) ? crules[i].rename : class;
			params = &crules[i].params;
			break;
		}
	}

	Class *cls = classes;
	for (; cls; cls = cls->next) {
		if (strcmp(cls->name, name) == 0)
			return cls;
	}

	cls = ecalloc(1, sizeof(Class));
	Class **cpp = &classes;
	for (; *cpp; cpp = &(*cpp)->next);
	*cpp = cls;

	snprintf(cls->name, sizeof(cls->name), "%s", name);
	cls->w_name = TEXTW(cls->name);

	if (params)
		cls->params = *params;
	else
		cls->params = default_lt_params;

	return cls;
}

void
_class_select(Class *cls)
{
	if (!cls) {
		return;
	} else if (cls == selmon->curcls) {
		if (selmon->viewmode == ViewClass)
			return;
	} else {
		selmon->prevcls = selmon->curcls;
		selmon->curcls = cls;
	}

	selmon->prevtags = selmon->curtags;
	selmon->curtags = 0;
	SYNC_VIEWMODE(selmon);

	focus(NULL);
	arrange(selmon);
}

Class *
_class_tail()
{
	Class *cls = classes;
	for (; cls && cls->next; cls = cls->next);
	return cls;
}

void
_client_show(Client *c)
{
	if (!c)
		return;

	selmon->prevtags = selmon->curtags;
	selmon->curtags = 0;
	SYNC_VIEWMODE(selmon);
	if (selmon->curcls != c->class) {
		selmon->prevcls = selmon->curcls;
		selmon->curcls = c->class;
	}

	focus(c);
	arrange(selmon);
}

/* Creates a brand new tag slot at bit position `pos` (computed from `relative`/
 * `append`) by shifting every client's tags at/above `pos` up by one to make
 * room, then makes the new, still-empty slot the monitor's sole current tag. */
void
_tag_insert(Monitor *m, int relative, int append, int tag_sel)
{
	int pos = 0;
	if (relative) {
		pos = append ? TAG_MSB(m->curtags) + 1 : TAG_LSB(m->curtags);
	} else if (append) {
		tag_t occ = 0;
		for (Client *c = m->clients; c; c = c->next)
			occ |= c->tags;

		pos = occ == 0 ? 0 : TAG_MSB(occ) + 1;
	}

	if (pos >= TAG_BIT_COUNT - 1)
		return;

	if (pos) {
		for (Client *c = m->clients; c; c = c->next)
			c->tags = tag_bit_insert(c->tags, pos);

		m->curtags = tag_bit_insert(m->curtags, pos);
	} else {
		for (Client *c = m->clients; c; c = c->next)
			if (c->tags & TAG_UNIT) {
				for (Client *c = m->clients; c; c = c->next)
					c->tags <<= 1;
				break;
			}
		m->curtags <<= 1;
	}

	m->prevtags = m->curtags;
	m->curtags = TAG_UNIT << pos;
	SYNC_VIEWMODE(m);

	if (tag_sel && m->sel)
		m->sel->tags = m->curtags;

	focus(NULL);
	arrange(m);
}

/* Deletes every currently-selected tag slot (unless a client still occupies
 * one and `force` is false), from the top bit down, closing each gap in every
 * client's tags and in `prevtags` as it goes, then falls back to tag 1. */
void
_tag_remove(Monitor *m, int force)
{
	tag_t t = m->curtags;
	if (!t)
		return;

	if (!force) {
		for (Client *c = m->clients; c; c = c->next)
			if (t & c->tags)
				return;
	}

	int msb = TAG_MSB(t);
	while (1) {
		for (Client *c = m->clients; c; c = c->next)
			c->tags = tag_bit_remove(c->tags, msb);

		if (m->prevtags)
			m->prevtags = tag_bit_remove(m->prevtags, msb);

		t &= ~(TAG_UNIT << msb);
		if (!t)
			break;
		msb = TAG_MSB(t);
	}

	m->curtags = TAG_UNIT;
	SYNC_VIEWMODE(m);

	focus(NULL);
	arrange(m);
}

/* function implementations */
void
applyrules(Client *c, const char *class, const char *instance)
{
	uint i;
	const Rule *r;
	Monitor *m;

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
		}
	}
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;

	const Layout *lt = _current_layout(m);
	if (RESIZEHINTS || c->isfloating || !lt || !lt->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
	const Layout *lt = _current_layout(m);
	if (!lt)
		return;

	strncpy(m->ltsymbol, lt->symbol, sizeof m->ltsymbol);
	if (lt->arrange)
		lt->arrange(m);
}

void
attach(Client *c)
{
	Monitor *m = c->mon;
	c->next = m->clients;
	m->clients = c;
}

void
attachstack(Client *c)
{
	Monitor *m = c->mon;
	c->snext = m->stack;
	m->stack = c;
}

void
banish_pointer(const Arg *arg)
{
	Window w, w2;
	int x, y, x2, y2;
	unsigned int mask;

	if (!XQueryPointer(dpy, root, &w, &w2, &x, &y, &x2, &y2, &mask))
		return;

	if (x == 0 && (y == 0 || y == (selmon->mh - 1))) {
		x = selmon->mw;
	} else if (x == (selmon->mw - 1) && y == 0) {
		x = 0;
		y = selmon->mh;
	} else {
		x = y = 0;
	}

	XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
	XFlush(dpy);
}

void
buttonpress(XEvent *e)
{
	uint i, click = ClkRootWin;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		if (ev->x < selmon->x_class_ellipsis_l) {
			if (ev->button == Button1)
				_class_select(classes);
			return;
		}
		for (i = 0; i < LENGTH(classclick); i++) {
			if (!classclick[i].x)
				break;
			if (ev->x < classclick[i].x) {
				if (ev->button == Button1) {
					_class_select(classclick[i].cls);
					return;
				}

				click = ClkClass;
				goto process;
			}
		}
		if (ev->x < selmon->x_class_ellipsis_r) {
			if (ev->button == Button1)
				_class_select(_class_tail());
			return;
		}

		if (ev->x < selmon->x_tag_ellipsis_l) {
			if (ev->button == Button1) {
				arg.t = selmon->tag_ellipsis_l;
				tag_view(&arg);
			}
			return;
		}
		for (i = 0; i < LENGTH(tagclick); i++) {
			if (!tagclick[i].x)
				break;
			if (ev->x < tagclick[i].x) {
				click = ClkTag;
				arg.t = tagclick[i].tag;
				goto process;
			}
		}
		if (ev->x < selmon->x_tag_ellipsis_r) {
			if (ev->button == Button1) {
				arg.t = selmon->tag_ellipsis_r;
				tag_view(&arg);
			}
			return;
		}

		for (i = 0; i < LENGTH(urgentclick); i++) {
			if (!urgentclick[i].x)
				break;
			if (ev->x < urgentclick[i].x) {
				_client_show(urgentclick[i].c);
				return;
			}
		}

		if (ev->x < selmon->x_layout) {
			click = ClkLayout;
		} else if (ev->x < selmon->x_layout_param) {
			click = ClkLayoutParam;
		} else if (ev->x < selmon->x_status_text) {
			strcpy(stext, "");
			drawbar(selmon);
			return;
		} else {
			click = ClkClientList;

			if (ev->x < m->x_client_ellipsis_l) {
				arg.i = 1;
				goto process;
			}
			for (i = 0; i < LENGTH(clientclick); i++) {
				if (!clientclick[i].x)
					break;
				if (ev->x < clientclick[i].x) {
					switch (ev->button) {
					case Button1:
						focus(clientclick[i].c);
						restack(selmon);
						return;
					case Button3:
						focus(clientclick[i].c);
						zoom(NULL);
						return;
					}

					goto process;
				}
			}
			if (ev->x <= m->x_client_ellipsis_r) {
				arg.i = -1;
				goto process;
			}
		}
	} else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}

 process:
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
			&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
class_adjacent(const Arg *arg)
{
	Class *cls = selmon->curcls;
	if (!arg || !classes || !cls)
		return;

	if (arg->i > 0) {
		cls = selmon->curcls->next;
		if (!cls)
			cls = classes;
	} else {
		for (cls = classes; cls && cls->next != selmon->curcls; cls = cls->next);
		if (!cls)
			for (cls = selmon->curcls->next; cls && cls->next; cls = cls->next);
		if (!cls)
			return;
	}

	selmon->prevcls = selmon->curcls;
	selmon->curcls = cls;

	focus(NULL);
	arrange(selmon);
}

void
class_select(const Arg *arg) {
	if (!arg || !classes)
		return;

	Class *cls;
	if (arg->i > 0) {
		cls = classes;
		for (int i = 1; cls && i != arg->i; cls = cls->next, i++);
	} else if (arg->i < 0) {
		cls = _class_tail();
	} else {
		cls = selmon->prevcls;
	}

	_class_select(cls);
}

void
class_stack(const Arg *arg)
{
	Class *cls = selmon->curcls;
	if (!arg || !classes || !cls)
		return;

	if (arg->i > 0) {
		if (cls == classes)
			return;

		_class_detach(cls);
		cls->next = classes;
		classes = cls;
	} else {
		Class *tail = cls->next;
		for (; tail && tail->next; tail = tail->next);
		if (!tail)
			return;

		_class_detach(cls);
		tail->next = cls;
		cls->next = NULL;
	}

	drawbar(selmon);
}

void
class_swap(const Arg *arg)
{
	Class *cls = selmon->curcls;
	if (!arg || !classes || !cls)
		return;

	if (arg->i > 0) {
		Class *swap = cls->next;
		if (!swap)
			return;

		_class_detach(cls);
		cls->next = swap->next;
		swap->next = cls;
	} else {
		Class *swap = classes;
		if (cls == swap)
			return;
		for (; swap && swap->next != cls; swap = swap->next);
		if (!swap)
			return;

		_class_detach(swap);
		swap->next = cls->next;
		cls->next = swap;
	}

	drawbar(selmon);
}

void
cleanup(void)
{
	Monitor *m;
	size_t i;

	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		drw_scm_free(drw, scheme[i], 3);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon);
}

void
clear_status_text(const Arg *arg)
{
	if (!strlen(stext))
		return;
	strcpy(stext, "");
	drawbar(selmon);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
	}
}

void
client_remove_tags(const Arg *arg)
{
	if (!selmon->sel || !selmon->sel->tags)
		return;

	selmon->sel->tags = 0;
	if (selmon->viewmode == ViewTag) {
		focus(NULL);
		arrange(selmon);
	} else {
		drawbar(selmon);
	}
}

void
client_select(const Arg *arg)
{
	if (!arg)
		return;

	Client *c, *cand = NULL;

	if (arg->i > 0) {
		int i = 1;
		c = selmon->clients;
		for (; c; c = c->next) {
			if (ISVISIBLE(c)) {
				if (i == arg->i) {
					cand = c;
					break;
				}
				i++;
			}
		}
	} else if (arg->i < 0) {
		c = selmon->clients;
		for (; c; c = c->next)
			if (ISVISIBLE(c))
				cand = c;
	} else {
		if (!selmon->sel)
			return;

		for (c = selmon->stack; c && c != selmon->sel; c = c->snext)
			if (ISVISIBLE(c)) {
				cand = c;
				break;
			}
		if (!cand) {
			for (c = selmon->sel->snext; c && !ISVISIBLE(c); c = c->snext);
			cand = c;
		}
	}

	if (cand && cand != selmon->sel) {
		focus(cand);
		arrange(selmon);
	}
}

void
client_select_urg(const Arg *arg)
{
	Client *c;
	for (c = selmon->stack; c; c = c->snext)
		if (c->isurgent) {
			_client_show(c);
			return;
		}
}

void
client_stack(const Arg *arg)
{
	Client *sel = selmon->sel;
	if (!arg || !sel)
		return;

	if (arg->i > 0) {
		Client *tail = sel->next;
		for (; tail && tail->next; tail = tail->next);
		if (!tail)
			return;

		detach(sel);
		tail->next = sel;
		sel->next = NULL;
	} else {
		if (sel == selmon->clients)
			return;

		detach(sel);
		sel->next = selmon->clients;
		selmon->clients = sel;
	}

	arrange(selmon);
}

void
client_swap(const Arg *arg)
{
	if (!arg)
		return;

	Client *sel = selmon->sel;
	if (!sel)
		return;

	if (arg->i > 0) {
		Client *c = sel->next;
		for (; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			return;

		detach(sel);
		sel->next = c->next;
		c->next = sel;
	} else {
		Client **cand = NULL;
		Client **cpp = &selmon->clients;
		for (; *cpp && *cpp != sel; cpp = &(*cpp)->next)
			if (ISVISIBLE((*cpp)))
				cand = cpp;
		if (!cand)
			return;

		detach(sel);
		sel->next = *cand;
		*cand = sel;
	}

	arrange(selmon);
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		const Layout *lt = _current_layout(selmon);

		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !lt || !lt->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;

	m = ecalloc(1, sizeof(Monitor));
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	m->topbar = TOPBAR;
	m->showbar = SHOWBAR;

	return m;
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

void
detach(Client *c)
{
	Monitor *m = c->mon;
	Client **tc;

	for (tc = &m->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Monitor *m = c->mon;
	Client **tc, *t;

	for (tc = &m->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == m->sel) {
		for (t = m->stack; t && !ISVISIBLE(t); t = t->snext);
		m->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

void
drawbar(Monitor *m)
{
	if (!m || !m->showbar)
		return;

	LayoutParams *p = _layout_params(m);
	int x, w, i;
	int cls_idx = -1, c_idx = -1;
	uint boxs = drw->fonts->h / 9;
	uint boxw = drw->fonts->h / 6 + 2;
	Class *cls;
	Client *c;
	tag_t occ = 0;
	uint urg = 0;
	char buf[20];

	classclick[0].x = 0;
	tagclick[0].x = 0;
	urgentclick[0].x = 0;
	clientclick[0].x = 0;

	m->x_class_ellipsis_l = m->x_class_ellipsis_r = 0;
	m->x_tag_ellipsis_l = m->x_tag_ellipsis_r = 0;
	m->tag_ellipsis_l = m->tag_ellipsis_r = 0;
	m->x_client_ellipsis_l = m->x_client_ellipsis_r = 0;
	m->x_layout = m->x_layout_param = 0;

	for (i = 0, cls = classes; cls; cls = cls->next, i++) {
		if (cls == m->curcls)
			cls_idx = i;
	}
	const int cls_cnt = i;

	for (i = 0, c = m->clients; c; c = c->next) {
		if (ISVISIBLE(c)) {
			if (c == m->sel)
				c_idx = i;
			i++;
		}

		occ |= c->tags;
		if (urg < BAR_URGENT_MAX && c->isurgent) {
			urgentclick[urg].c = c;
			urg++;
		}
	}
	const int c_cnt = i;

	x = 0;

	if (cls_cnt > 0) {
		int start = 0;
		int end = cls_cnt;
		if (cls_cnt > BAR_CLASS_MAX) {
			start = cls_idx - BAR_CLASS_MAX / 2;
			end = cls_idx + (BAR_CLASS_MAX - BAR_CLASS_MAX / 2);
			if (start < 0) {
				end += abs(start);
				start = 0;
			} else if (end > cls_cnt) {
				start -= end - cls_cnt;
				end = cls_cnt;
			}
		}

		cls = classes;
		if (start) {
			for (i = 0; i < start; i++)
				cls = cls->next;

			drw_setscheme(drw, scheme[SchemeNormal]);
			drw_text(drw, x, 0, w_ellipsis_l, bh, lrpad_2, ellipsis_l, 0);

			x += w_ellipsis_l;
			m->x_class_ellipsis_l = x;
		}

		drw_setscheme(drw, scheme[SchemeClass]);
		int bar_i = 0;
		for (i = 0; cls && i < BAR_CLASS_MAX; cls = cls->next, i++, bar_i++) {
			int invert = m->viewmode == ViewClass && cls == m->curcls;
			drw_text(drw, x, 0, cls->w_name, bh, lrpad_2, cls->name, invert);
			if (m->sel && m->sel->class == cls)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, 1, invert);
			x += cls->w_name;

			classclick[bar_i].x = x;
			classclick[bar_i].cls = cls;
		}
		if (bar_i < BAR_CLASS_MAX) {
			classclick[bar_i].x = 0;
			classclick[bar_i].cls = NULL;
		}

		if (end < cls_cnt) {
			drw_setscheme(drw, scheme[SchemeNormal]);
			drw_text(drw, x, 0, w_ellipsis_r, bh, lrpad_2, ellipsis_r, 0);

			x += w_ellipsis_r;
			m->x_class_ellipsis_r = x;
		}
	} else {
		drw_setscheme(drw, scheme[SchemeNormal]);
		drw_text(drw, x, 0, w_dwm_version, bh, lrpad_2, dwm_version, 0);
		x += w_dwm_version;
	}
	if (x > m->mw) goto draw;

	tag_t t = m->curtags ? m->curtags | occ : occ;
	const int tag_cnt = t == 0 ? 0 : TAG_MSB(t) + 1;
	if (t) {
		drw_setscheme(drw, scheme[SchemeNormal]);
		drw_rect(drw, x, 0, 2, bh, 0, 1);
		x += 2;

		int tag_idx = m->curtags == 0 ? 0 : TAG_LSB(m->curtags);

		int start = 0;
		int end = tag_cnt;
		if (tag_cnt > BAR_TAG_MAX) {
			start = tag_idx - BAR_TAG_MAX / 2;
			end = tag_idx + (BAR_TAG_MAX - BAR_TAG_MAX / 2);
			if (start < 0) {
				end += abs(start);
				start = 0;
			} else if (end > tag_cnt) {
				start -= end - tag_cnt;
				end = tag_cnt;
			}
		}

		if (start) {
			drw_setscheme(drw, scheme[SchemeNormal]);
			drw_text(drw, x, 0, w_ellipsis_l, bh, lrpad_2, ellipsis_l, 0);
			x += w_ellipsis_l;

			m->x_tag_ellipsis_l = x;
			m->tag_ellipsis_l = TAG_UNIT;
		}

		tag_t t = TAG_UNIT << start;
		int bar_i = 0;
		for (i = start; i < end; i++, t <<= 1, bar_i++) {
			int is_selected = m->curtags && t & m->curtags;
			int s_idx = t & occ || is_selected ? SchemeTag : SchemeNormal;
			int invert = is_selected;
			int draw_box = m->sel && t & m->sel->tags;
			drw_setscheme(drw, scheme[s_idx]);

			int w2 = w_tlabels[i];
			drw_text(drw, x, 0, w2, bh, lrpad_2, tlabels[i], invert);

			if (draw_box)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, 1, invert);

			x += w2;
			tagclick[bar_i].x = x;
			tagclick[bar_i].tag = t;
		}
		if (bar_i < BAR_TAG_MAX) {
			tagclick[bar_i].x = 0;
			tagclick[bar_i].tag = 0;
		}

		if (end < tag_cnt) {
			drw_setscheme(drw, scheme[SchemeNormal]);
			drw_text(drw, x, 0, w_ellipsis_r, bh, lrpad_2, ellipsis_r, 0);

			x += w_ellipsis_r;

			m->x_tag_ellipsis_r = x;
			int msb = occ == 0 ? 0 : TAG_MSB(occ);
			m->tag_ellipsis_r = TAG_UNIT << msb;
		}
	}
	if (x > m->mw) goto draw;

	if (urg) {
		drw_setscheme(drw, scheme[SchemeUrgent]);
		drw_rect(drw, x, 0, 1, bh, 0, 1);
		x += 1;

		for (i = 0; i < urg; i++) {
			c = urgentclick[i].c;

			w = TEXTW(c->name);
			if (w > BAR_URGENT_WIDTH)
				w = BAR_URGENT_WIDTH;
			drw_text(drw, x, 0, w, bh, lrpad_2, c->name, 1);

			x += w;
			urgentclick[i].x = x;
		}
	}
	if (x > m->mw) goto draw;

	if (p) {
		w = TEXTW(m->ltsymbol);
		drw_setscheme(drw, scheme[SchemeLayout]);
		drw_text(drw, x, 0, w, bh, lrpad_2, m->ltsymbol, 0);
		x += w;
		m->x_layout = x;

		snprintf(buf, sizeof(buf), "%d", p->nmaster);
		w = TEXTW(buf);
		drw_setscheme(drw, scheme[SchemeNmaster]);
		drw_text(drw, x, 0, w, bh, lrpad_2, buf, 0);
		x += w;

		snprintf(buf, sizeof(buf), "%0.2f", p->mfact);
		w = TEXTW(buf);
		drw_setscheme(drw, scheme[SchemeMfact]);
		drw_text(drw, x, 0, w, bh, lrpad_2, buf, 0);
		x += w;
		m->x_layout_param = x;
	}
	if (x > m->mw) goto draw;

	if (strlen(stext)) {
		w = TEXTW(stext);
		if (w > BAR_STATUS_WIDTH)
			w = BAR_STATUS_WIDTH;

		drw_setscheme(drw, scheme[SchemeStatus]);
		drw_text(drw, x, 0, w, bh, lrpad_2, stext, 1);

		x += w;
		m->x_status_text = x;

		drw_rect(drw, x, 0, 1, bh, 0, 1);
		x += 1;
	}

	const int w_rest_area = m->mw - x;
	if (w_rest_area > 100 && c_cnt > 0) {
		const int s_idx = m->viewmode == ViewTag ? SchemeTag : SchemeClass;

		c = m->clients;
		i = 0;
		int start = 0, end = MIN(c_cnt, BAR_CLIENT_MAX);

		w = w_rest_area / end;
		if (w < BAR_CLIENT_WIDTH) {
			end = w_rest_area / BAR_CLIENT_WIDTH;
			w = w_rest_area / end;
		}

		int end_x = m->mw;
		if (end < c_cnt) {
			start = c_idx - end / 2;
			end = c_idx + (end - end / 2);
			if (start < 0) {
				end += abs(start);
				start = 0;
			} else if (end > c_cnt) {
				start -= end - c_cnt;
				end = c_cnt;
			}

			int w2 = w_rest_area;
			drw_setscheme(drw, scheme[SchemeNormal]);
			if (start) {
				for (; c && i < start; c = c->next)
					if (ISVISIBLE(c))
						i++;

				drw_text(drw, x, 0, w_ellipsis_l, bh, lrpad_2, ellipsis_l, 0);
				x += w_ellipsis_l;
				w2 -= w_ellipsis_l;

				m->x_client_ellipsis_l = x;
			}
			if (end < c_cnt) {
				drw_text(drw, m->mw - w_ellipsis_r, 0, w_ellipsis_r, bh, lrpad_2, ellipsis_r, 0);
				end_x -= w_ellipsis_r;
				w2 -= w_ellipsis_r;

				m->x_client_ellipsis_r = m->mw;
			}

			w = w2 / (end - start);
		}

		int bar_i = 0;
		for (; c && i < end; c = c->next) {
			if (!ISVISIBLE(c))
				continue;

			int is_sel = c == m->sel;
			int is_selmon = m == selmon && is_sel;

			int w2 = w;
			if (i < LENGTH(clabels)) {
				int w3 = w_clabels[i];
				drw_setscheme(drw, scheme[is_sel ? s_idx : SchemeClntLbl]);
				drw_text(drw, x, 0, w3, bh, lrpad_2, clabels[i],
						 is_selmon || c->isurgent);

				w2 -= w3;
				x += w3;
			}

			drw_setscheme(drw, scheme[is_sel ? s_idx : SchemeNormal]);
			drw_text(drw, x, 0, w2, bh, lrpad_2, c->name, 0);

			if (c->isfloating)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, c->isfixed, 0);

			x += w2;

			clientclick[bar_i].x = x;
			clientclick[bar_i].c = c;
			i++;
			bar_i++;
		}
		if (bar_i < BAR_CLIENT_MAX) {
			clientclick[bar_i].x = 0;
			clientclick[bar_i].c = NULL;
		}

		if (x < end_x) {
			drw_setscheme(drw, scheme[SchemeNormal]);
			drw_rect(drw, x, 0, end_x - x, bh, 1, 1);
		}
		x = m->mw;
	}

	if (x < m->mw) {
		drw_setscheme(drw, scheme[SchemeNormal]);
		drw_rect(drw, x, 0, m->mw - x, bh, 1, 1);
	}

 draw:
	drw_map(drw, m->barwin, 0, 0, m->ww, bh);
}

void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

void
group_adjacent(const Arg *arg)
{
	if (selmon->viewmode == ViewClass) {
		class_adjacent(arg);
	} else {
		tag_adjacent(arg);
	}
}

void
group_append(const Arg *arg)
{
	if (!arg)
		return;

	if (selmon->viewmode == ViewClass) {
		_tag_insert(selmon, 0, 1, arg->s.s2 > 0);
	} else {
		_tag_insert(selmon, arg->s.s1 > 0, 1, arg->s.s2 > 0);
	}
}

void
group_insert(const Arg *arg)
{
	if (!arg)
		return;

	if (selmon->viewmode == ViewClass) {
		_tag_insert(selmon, 0, 0, arg->s.s2 > 0);
	} else {
		_tag_insert(selmon, arg->s.s1 > 0, 0, arg->s.s2 > 0);
	}
}

void
group_select(const Arg *arg)
{
	if (!arg)
		return;

	if (arg->i) {
		if (selmon->viewmode == ViewClass) {
			class_select(arg);
		} else {
			tag_select(arg);
		}
		return;
	}

	if (selmon->viewmode == ViewClass) {
		if (selmon->prevtags) {
			selmon->curtags = selmon->prevtags;
			selmon->prevtags = 0;
			SYNC_VIEWMODE(selmon);
		} else if (selmon->prevcls) {
			Class *c = selmon->curcls;
			selmon->curcls = selmon->prevcls;
			selmon->prevcls = c;
		} else
			return;
	} else if (selmon->prevtags) {
		tag_t t = selmon->curtags;
		selmon->curtags = selmon->prevtags;
		selmon->prevtags = t;
		SYNC_VIEWMODE(selmon);
	} else if (selmon->curcls) {
		selmon->prevtags = selmon->curtags;
		selmon->curtags = 0;
		SYNC_VIEWMODE(selmon);
	} else
		return;

	focus(NULL);
	arrange(selmon);
}

void
group_stack(const Arg *arg)
{
	if (selmon->viewmode == ViewClass) {
		class_stack(arg);
	} else {
		tag_stack(arg);
	}
}

void
group_swap(const Arg *arg)
{
	if (selmon->viewmode == ViewClass) {
		class_swap(arg);
	} else {
		tag_swap(arg);
	}
}

void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c)) {
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	}

	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		Monitor *m = c->mon;
		if (m != selmon)
			selmon = m;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[m->viewmode == ViewTag ? SchemeTag : SchemeClass][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!arg || !mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
}

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!arg || !selmon->sel || (selmon->sel->isfullscreen && LOCKFULLSCREEN))
		return;
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (ISVISIBLE(i))
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i))
					c = i;
	}
	if (c) {
		focus(c);
		restack(selmon);
	}
}

Atom
getatomprop(Client *c, Atom prop)
{
	int format;
	unsigned long nitems, dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &format, &nitems, &dl, &p) == Success && p) {
		if (nitems > 0 && format == 32)
			atom = *(long *)p;
		XFree(p);
	}
	return atom;
}

int
getrootptr(int *x, int *y)
{
	int di;
	uint dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, &p) != Success)
		return -1;
	if (n != 0 && format == 32)
		result = *(long *)p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, uint size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		uint i, j;
		uint modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		uint i, j, k;
		uint modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

void
incnmaster(const Arg *arg)
{
	if (!arg)
		return;

	LayoutParams *p = _layout_params(selmon);
	p->nmaster = MAX(p->nmaster + arg->i, 0);
	arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	uint i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (!sendevent(selmon->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	{
		const char *class, *instance;
		XClassHint ch = { NULL, NULL };

		XGetClassHint(dpy, c->win, &ch);
		class    = ch.res_class ? ch.res_class : broken;
		instance = ch.res_name  ? ch.res_name  : broken;

		c->class = _class_find_or_create(class);

		if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
			c->mon = t->mon;
			c->tags = t->tags;
		} else {
			c->mon = selmon;
			applyrules(c, class, instance);

			if (c->mon->viewmode == ViewClass) {
				if (c->mon->curcls != c->class) {
					c->mon->prevcls = c->mon->curcls;
					c->mon->curcls = c->class;
				}
			} else {
				if (!c->tags)
					c->tags = c->mon->curtags;
			}
		}

		if (ch.res_class)
			XFree(ch.res_class);
		if (ch.res_name)
			XFree(ch.res_name);
	}

	Monitor *m = c->mon;
	if (c->x + WIDTH(c) > m->wx + m->ww)
		c->x = m->wx + m->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > m->wy + m->wh)
		c->y = m->wy + m->wh - HEIGHT(c);
	c->x = MAX(c->x, m->wx);
	c->y = MAX(c->y, m->wy);
	c->bw = BORDER_PX;

	wc.border_width = c->bw;
	updatetitle(c);
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNormal][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);

	if (m == selmon)
		unfocus(m->sel, 0);
	m->sel = c;

	arrange(m);
	XMapWindow(dpy, c->win);
	focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
monocle(Monitor *m)
{
	uint n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / REFRESH_RATE))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < SNAP_PX)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < SNAP_PX)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < SNAP_PX)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < SNAP_PX)
				ny = selmon->wy + selmon->wh - HEIGHT(c);

			const Layout *lt = _current_layout(selmon);
			if (!c->isfloating && lt && lt->arrange
			&& (abs(nx - c->x) > SNAP_PX || abs(ny - c->y) > SNAP_PX))
				togglefloating(NULL);
			if (!lt || !lt->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void
pop(Client *c)
{
	if (!c)
		return;

	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		Monitor *m = c->mon;

		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(m);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == m->sel)
				drawbar(m);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg)
{
	for (Monitor *m = mons; m; m = m->next) {
		if (m->clients)
			return;
	}

	return_code = EXIT_SUCCESS;
	running = 0;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
reload(const Arg *arg)
{
	return_code = EXIT_RELOAD;
	running = 0;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
	int ocx, ocy, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);

	m = c->mon;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / REFRESH_RATE))
				continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);

			const Layout *lt = _current_layout(selmon);

			if (m->wx + nw >= selmon->wx && m->wx + nw <= selmon->wx + selmon->ww
			&& m->wy + nh >= selmon->wy && m->wy + nh <= selmon->wy + selmon->wh)
			{
				if (!c->isfloating && lt && lt->arrange
				&& (abs(nw - c->w) > SNAP_PX || abs(nh - c->h) > SNAP_PX))
					togglefloating(NULL);
			}
			if (!lt || lt->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);

	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;

	const Layout *lt = _current_layout(m);
	if (m->sel->isfloating || !lt || !lt->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (lt && lt->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
scan(void)
{
	uint i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;

	unfocus(c, 1);
	detach(c);
	detachstack(c);
	attach(c);
	attachstack(c);
	if (c->isfullscreen)
		resizeclient(c, m->mx, m->my, m->mw, m->mh);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus)
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&c->win, 1);
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
	Monitor *m = c->mon;

	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, m->mx, m->my, m->mw, m->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(m);
	}
}

void
setlayout(const Arg *arg)
{
	if (!arg)
		return;

	const int i = arg->i;
	if (i < 0 || i >= LENGTH(layouts))
		return;

	LayoutParams *p = _layout_params(selmon);
	if (i == p->lt_idx)
		return;

	p->lt_idx = i;
	strncpy(selmon->ltsymbol, layouts[i].symbol, sizeof selmon->ltsymbol);

	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	if (!arg)
		return;

	LayoutParams *p = _layout_params(selmon);
	if (p) {
		float f = arg->f < 1.0 ? arg->f + p->mfact : arg->f - 1.0;
		if (f < 0.10 || f > 0.90)
			return;
		p->mfact = f;

		arrange(selmon);
	}
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");

	lrpad = drw->fonts->h;
	lrpad_2 = lrpad / 2;
	bh = drw->fonts->h + 2;

	w_dwm_version = TEXTW(dwm_version);
	w_ellipsis_l = TEXTW(ellipsis_l);
	w_ellipsis_r = TEXTW(ellipsis_r);
	w_urgent_v = TEXTW(urgent_v);

	for (int i = 0; i < LENGTH(clabels); i++) {
		w_clabels[i] = TEXTW(clabels[i]);
	}
	for (int i = 0; i < LENGTH(tlabels); i++) {
		if (i < LENGTH(tags))
			snprintf(tlabels[i], sizeof(tlabels[i]), "%s", tags[i]);
		else
			snprintf(tlabels[i], sizeof(tlabels[i]), "%d", i + 1);

		w_tlabels[i] = TEXTW(tlabels[i]);
	}

	for (int i = 0; i < TAG_BIT_COUNT; i++)
		tag_lt_params[i] = default_lt_params;

	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	/* init bars */
	updatebars();
	updatestatus();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;

	const Layout *lt = _current_layout(c->mon);
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!lt || !lt->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
spawn(const Arg *arg)
{
	struct sigaction sa;

	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
	}
}

void
tagmon(const Arg *arg)
{
	if (!arg || !selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

void
tag_adjacent(const Arg *arg)
{
	if (!arg || selmon->viewmode != ViewTag)
		return;

	selmon->prevtags = selmon->curtags;
	if (arg->i > 0) {
		selmon->curtags <<= 1;
	} else {
		selmon->curtags >>= 1;
	}
	SYNC_VIEWMODE(selmon);

	focus(NULL);
	arrange(selmon);
}

void
tag_remove(const Arg *arg)
{
	if (!arg)
		return;

	if (arg->i < 0)
		client_remove_tags(arg);
	else
		_tag_remove(selmon, arg->i > 0);
}

void
tag_select(const Arg *arg)
{
	if (!arg)
		return;

	tag_t t = 0;
	if (arg->i > 0)
		t = TAG_UNIT;
	else if (arg->i < 0) {
		for (Client *c = selmon->clients; c; c = c->next)
			t |= c->tags;
		if (!t)
			return;
		t = TAG_UNIT << TAG_MSB(t);
	} else if (!(t = selmon->prevtags))
		return;

	if (t == selmon->curtags)
		return;

	selmon->prevtags = selmon->curtags;
	selmon->curtags = t;
	SYNC_VIEWMODE(selmon);

	focus(NULL);
	arrange(selmon);
}

void
tag_set(const Arg *arg)
{
	if (!arg || !selmon->sel)
		return;

	selmon->sel->tags = arg->t;

	if (selmon->viewmode == ViewTag) {
		focus(NULL);
		arrange(selmon);
	} else {
		drawbar(selmon);
	}
}

/* Moves every currently-selected tag one slot toward the top (arg->i > 0) or
 * bottom (arg->i <= 0) of the occupied tag range, one step at a time: each
 * iteration relocates the outermost selected bit past its neighbor, for both
 * the monitor's own `curtags` and every client's `tags`. */
void
tag_stack(const Arg *arg)
{
	if (!arg || selmon->viewmode != ViewTag)
		return;

	tag_t t = selmon->curtags;
	if (!t)
		return;

	if (arg->i > 0) {
		while ((t & (t + 1))) {
			int msb = TAG_MSB(t);

			for (Client *c = selmon->clients; c; c = c->next) {
				tag_t bit = (c->tags >> msb) & TAG_UNIT;
				c->tags = bit | (tag_bit_remove(c->tags, msb) << 1);
			}

			t = TAG_UNIT | (tag_bit_remove(t, msb) << 1);
		}
	} else {
		tag_t occ = 0;
		for (Client *c = selmon->clients; c; c = c->next)
			occ |= c->tags;
		if (!occ)
			return;
		int msb = TAG_MSB(occ);

		while (1) {
			int lsb = TAG_LSB(t);
			tag_t mask1 = (TAG_UNIT << (msb + 1)) - 1;
			tag_t mask2 = (TAG_UNIT << lsb) - 1;
			if (t == mask1 - mask2)
				break;

			for (Client *c = selmon->clients; c; c = c->next) {
				tag_t bit = ((c->tags >> lsb) & TAG_UNIT) << msb;
				c->tags = bit | tag_bit_remove(c->tags, lsb);
			}

			t = (TAG_UNIT << msb) | tag_bit_remove(t, lsb);
		}
	}

	selmon->curtags = t;
	selmon->prevtags = 0;
	SYNC_VIEWMODE(selmon);
	drawbar(selmon);
}

/* Swaps the whole set of currently-selected tags with the adjacent slot one
 * position up (arg->i > 0) or down (arg->i <= 0): walks the selected bits
 * outward-in, and for every client whose membership differs between a
 * selected bit and its neighbor, flips both (an XOR swap of that bit pair). */
void
tag_swap(const Arg *arg)
{
	tag_t t = selmon->curtags;
	if (!arg || selmon->viewmode != ViewTag)
		return;

	if (arg->i > 0) {
		int msb = TAG_MSB(t);
		if (msb == TAG_BIT_COUNT - 1)
			return;

		while (1) {
			int mask = (TAG_UNIT << msb) | (TAG_UNIT << (msb + 1));
			for (Client *c = selmon->clients; c; c = c->next) {
				int b1 = (c->tags >> msb) & TAG_UNIT;
				int b2 = (c->tags >> (msb + 1)) & TAG_UNIT;

				if (b1 != b2)
					c->tags ^= mask;
			}

			t &= ~(TAG_UNIT << msb);
			if (!t)
				break;
			msb = TAG_MSB(t);
		}

		t = selmon->curtags << 1;
	} else {
		int lsb = TAG_LSB(t);
		if (lsb == 0)
			return;

		while (1) {
			int mask = (TAG_UNIT << lsb) | (TAG_UNIT << (lsb - 1));
			for (Client *c = selmon->clients; c; c = c->next) {
				int b1 = (c->tags >> lsb) & TAG_UNIT;
				int b2 = (c->tags >> (lsb - 1)) & TAG_UNIT;

				if (b1 != b2) {
					c->tags ^= mask;
				}
			}

			t &= ~(TAG_UNIT << lsb);
			if (!t)
				break;
			lsb = TAG_LSB(t);
		}

		t = selmon->curtags >> 1;
	}

	selmon->curtags = t;
	selmon->prevtags = 0;
	SYNC_VIEWMODE(selmon);
	drawbar(selmon);
}

void
tag_toggle_c(const Arg *arg)
{
	if (!arg || !selmon->sel)
		return;

	selmon->sel->tags ^= arg->t;

	if (selmon->viewmode == ViewTag && !(selmon->curtags & selmon->sel->tags)) {
		focus(NULL);
		arrange(selmon);
	} else {
		drawbar(selmon);
	}
}

void
tag_toggle_m(const Arg *arg)
{
	if (!arg)
		return;

	tag_t t;
	if (selmon->viewmode == ViewTag)
		t = selmon->curtags ^ arg->t;
	else
		t = selmon->curtags | arg->t;

	selmon->prevtags = selmon->curtags;
	selmon->curtags = t;
	SYNC_VIEWMODE(selmon);

	focus(NULL);
	arrange(selmon);
}

void
tag_view(const Arg *arg)
{
	if (!arg)
		return;

	tag_t t;
	if (arg->t > 0)
		t = arg->t;
	else if (!(t = selmon->prevtags))
		return;

	if (selmon->curtags && t == selmon->curtags)
		return;

	if (t != selmon->curtags) {
		selmon->prevtags = selmon->curtags;
		selmon->curtags = t;
		SYNC_VIEWMODE(selmon);
	}

	focus(NULL);
	arrange(selmon);
}

void
_tile(Monitor *m, int reverse)
{
	int i, n, h, mw, mx, sx, my, sy;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	LayoutParams *p = _layout_params(m);
	if (n > p->nmaster) {
		mw = p->nmaster ? m->ww * p->mfact : 0;
		mx = reverse ? m->ww - mw : 0;
		sx = reverse ? 0 : mw;
	} else {
		mw = m->ww;
		mx = sx = 0;
	}

	for (i = my = sy = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < p->nmaster) {
			h = (m->wh - my) / (MIN(n, p->nmaster) - i);
			resize(c, m->wx + mx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0);
			if (my + HEIGHT(c) < m->wh)
				my += HEIGHT(c);
		} else {
			h = (m->wh - sy) / (n - i);
			resize(c, m->wx + sx, m->wy + sy, m->ww - mw - (2*c->bw), h - (2*c->bw), 0);
			if (sy + HEIGHT(c) < m->wh)
				sy += HEIGHT(c);
		}
}

void
tile_l(Monitor *m)
{
	_tile(m, 0);
}

void
tile_r(Monitor *m)
{
	_tile(m, 1);
}

void
togglebar(const Arg *arg)
{
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
	arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
	Client *sel = selmon->sel;

	if (!sel)
		return;
	if (sel->isfullscreen) /* no support for fullscreen windows */
		return;
	sel->isfloating = !sel->isfloating || sel->isfixed;
	if (sel->isfloating)
		resize(sel, sel->x, sel->y, sel->w, sel->h, 0);
	arrange(selmon);
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNormal][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	XWindowChanges wc;

	_class_delete_if_empty(c);

	detach(c);
	detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);

	focus(NULL);
	updateclientlist();
	arrange(m);
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
}

void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;

	if (m->showbar) {
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	} else
		m->by = -bh;
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	uint i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "");
	drawbar(selmon);
}


void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;
	const Layout *lt = _current_layout(selmon);

	if (!lt || !lt->arrange || !c || c->isfloating)
		return;
	if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	checkotherwm();
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	run();
	cleanup();
	XCloseDisplay(dpy);

	return return_code;
}
