/* See LICENSE file for copyright and license details. */

/* constants */
#define EXIT_RELOAD       1
#define RESIZEHINTS       0     /* 1 means respect size hints in tiled resizals */
#define LOCKFULLSCREEN    1     /* 1 will force focus on the fullscreen window */
#define REFRESH_RATE      120    /* refresh rate (per second) for client move/resize */
#define BORDER_PX         1     /* border pixel of windows */
#define SNAP_PX           16    /* snap pixel */
#define TOPBAR            1     /* 0 means bottom bar */
#define SHOWBAR           1
#define BAR_CLASS_MAX     5
#define BAR_TAG_MAX       10
#define BAR_CLIENT_MAX    10
#define BAR_CLIENT_WIDTH  150
#define BAR_URGENT_MAX    3
#define BAR_URGENT_WIDTH  100
#define BAR_STATUS_LENGTH 100
#define BAR_STATUS_WIDTH  100

/* appearance */
static const char *fonts[]          = { "sans-serif:size=11" };
static const char dmenufont[]       = "sans-serif:size=15";

static const char col_white1[]      = "#ffffff";
static const char col_white2[]      = "#eeeeee";
static const char col_black1[]      = "#000000";
static const char col_black2[]      = "#191919";
static const char col_black3[]      = "#2a2a2a";
static const char col_bdr0[]        = "#242424";
static const char col_bdr1[]        = "#00c2ae";
static const char col_bdr2[]        = "#0090ff";
static const char col_red[]         = "#f32f7c";
static const char col_green[]       = "#afff00";
static const char col_blue[]        = "#00bbff";
static const char col_yellow[]      = "#ffff00";
static const char col_emerald[]     = "#00f2ae";
static const char col_dmenu_selbg[] = "#0077cc";

static const char *colors[][3]      = {
	/*                   fg            bg            border   */
	[SchemeNormal]   = { col_white2,   col_black2,   col_bdr0 },
	[SchemeClass]    = { col_emerald,  col_black2,   col_bdr1 },
	[SchemeTag]      = { col_blue,     col_black2,   col_bdr2 },
	[SchemeClntLbl]  = { col_white1,   col_black3,   col_bdr0 },
	[SchemeUrgent]   = { col_white2,   col_black2,   col_bdr0 },
	[SchemeStatus]   = { col_white2,   col_black2,   col_bdr0 },
	[SchemeLayout]   = { col_green,    col_black2,   col_bdr0 },
	[SchemeNmaster]  = { col_red,      col_black2,   col_bdr0 },
	[SchemeMfact]    = { col_yellow,   col_black2,   col_bdr0 },
};

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	0
	/*
	{ "Gimp",     NULL,       NULL,       0,            1,           -1 },
	{ "Firefox",  NULL,       NULL,       1 << 8,       0,           -1 },
	*/
};

/* `rename` merges `class` into that name's Class bucket (shared bar entry,
 * shared LayoutParams) -- it's not just a display label, see ClassRule. */
static const ClassRule crules[] = {
	/* class                        rename       nmaster  mfact  lt_idx */
	{ "st-256color",                "st",        {     1,   0.5,      0} },
	{ "firefox",                    "Firefox",   {     1,   0.5,      0} },
	{ "Brave-browser",              "Brave",     {     1,   0.5,      0} },
	{ "install4j-jclient-Launcher", "Java",      {     1,   0.5,      0} },
};

/* preallocated layout params           nmaster  mfact  lt_idx */
static LayoutParams default_lt_params = {     1,   0.5,      1 };

/* layouts */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[M]",      monocle },
	{ "[]=",      tile_l },
	{ "=[]",      tile_r },
	{ "><>",      NULL },
};

/* client labels */
static const char *clabels[] = {
	"Q", "W", "E", "R", "T",
	"A", "S", "D", "F", "G",
};

/* tag labels */
static const char *tags[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
};

/* key definitions */
#define MODKEY Mod4Mask
#define CLASSKEYS(KEY,IDX) \
	{ MODKEY,                     KEY,   class_select,         {.i =  IDX} },
#define TAGKEYS(KEY,IDX) \
	{ MODKEY,                     KEY,   tag_view,             {.t = TAG_UNIT << IDX} }, \
	{ MODKEY|ShiftMask,           KEY,   tag_set,              {.t = TAG_UNIT << IDX} }, \
	{ MODKEY|Mod1Mask,            KEY,   tag_toggle_c,         {.t = TAG_UNIT << IDX} }, \
	{ MODKEY|ControlMask,         KEY,   tag_toggle_m,         {.t = TAG_UNIT << IDX} },
#define CLIENTKEYS(KEY,IDX) \
	{ MODKEY,                     KEY,   client_select,        {.i =  IDX} },

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = {
	"mydmenu",
	"-m",   dmenumon,
	"-fn",  dmenufont,
	"-nb",  col_black2,
	"-nf",  col_white1,
	"-sb",  col_dmenu_selbg,
	"-sf",  col_white2,
	NULL,
};
static const char *termcmd[]  = { "st", NULL };
static const char *pavucmd[]  = { "pavucontrol", NULL };

static const Key keys[] = {
	/* modifier                     key        function              argument */
	{ MODKEY|ShiftMask,         XK_Delete,      reload,               {0} },
	{ MODKEY|ShiftMask,         XK_Escape,      quit,                 {0} },
	{ MODKEY|ShiftMask,      XK_BackSpace,      killclient,           {0} },
	{ MODKEY,                        XK_p,      spawn,                {.v = dmenucmd} },
	{ MODKEY|ShiftMask,              XK_p,      spawn,                {.v = pavucmd} },
	{ MODKEY|ShiftMask,         XK_Return,      spawn,                {.v = termcmd} },
	{ MODKEY,                   XK_Return,      client_select,        {.i =  0} },
	{ MODKEY,                      XK_Tab,      group_select,         {.i =  0} },
	{ MODKEY,                    XK_space,      togglefloating,       {0} },
	{ MODKEY,                    XK_slash,      togglebar,            {0} },
	{ MODKEY,                    XK_comma,      clear_status_text,    {0} },
	{ MODKEY,                   XK_period,      client_select_urg,    {0} },
	{ MODKEY,                    XK_grave,      banish_pointer,       {0} },
	{ MODKEY,               XK_apostrophe,      setlayout,            {.i =  0} },
	{ MODKEY,              XK_bracketleft,      setlayout,            {.i =  1} },
	{ MODKEY,             XK_bracketright,      setlayout,            {.i =  2} },
	{ MODKEY,                XK_semicolon,      setlayout,            {.i =  3} },
	{ MODKEY,                    XK_minus,      tag_remove,           {.i =  0} },
	{ MODKEY|ShiftMask,          XK_minus,      tag_remove,           {.i =  1} },
	{ MODKEY|ControlMask,        XK_minus,      tag_remove,           {.i = -1} },
	{ MODKEY,                        XK_y,      group_select,         {.i =  1} },
	{ MODKEY,                        XK_o,      group_select,         {.i = -1} },
	{ MODKEY|ShiftMask,              XK_y,      group_stack,          {.i =  1} },
	{ MODKEY|ShiftMask,              XK_o,      group_stack,          {.i = -1} },
	{ MODKEY,                        XK_u,      group_adjacent,       {.i = -1} },
	{ MODKEY,                        XK_i,      group_adjacent,       {.i =  1} },
	{ MODKEY|ShiftMask,              XK_u,      group_swap,           {.i = -1} },
	{ MODKEY|ShiftMask,              XK_i,      group_swap,           {.i =  1} },
	{ MODKEY|ControlMask,            XK_y,      group_insert,         {.s = {.s1 = 0, .s2 = 0}} },
	{ MODKEY|ControlMask,            XK_u,      group_insert,         {.s = {.s1 = 1, .s2 = 0}} },
	{ MODKEY|ControlMask,            XK_i,      group_append,         {.s = {.s1 = 1, .s2 = 0}} },
	{ MODKEY|ControlMask,            XK_o,      group_append,         {.s = {.s1 = 0, .s2 = 0}} },
	{ MODKEY|Mod1Mask,               XK_y,      group_insert,         {.s = {.s1 = 0, .s2 = 1}} },
	{ MODKEY|Mod1Mask,               XK_u,      group_insert,         {.s = {.s1 = 1, .s2 = 1}} },
	{ MODKEY|Mod1Mask,               XK_i,      group_append,         {.s = {.s1 = 1, .s2 = 1}} },
	{ MODKEY|Mod1Mask,               XK_o,      group_append,         {.s = {.s1 = 0, .s2 = 1}} },
	{ MODKEY,                        XK_j,      focusstack,           {.i =  1} },
	{ MODKEY,                        XK_k,      focusstack,           {.i = -1} },
	{ MODKEY|ShiftMask,              XK_j,      client_swap,          {.i =  1} },
	{ MODKEY|ShiftMask,              XK_k,      client_swap,          {.i = -1} },
	{ MODKEY|ControlMask,            XK_j,      incnmaster,           {.i = -1} },
	{ MODKEY|ControlMask,            XK_k,      incnmaster,           {.i =  1} },
	{ MODKEY,                        XK_h,      client_select,        {.i =  1} },
	{ MODKEY,                        XK_l,      client_select,        {.i = -1} },
	{ MODKEY|ShiftMask,              XK_h,      client_stack,         {.i = -1} },
	{ MODKEY|ShiftMask,              XK_l,      client_stack,         {.i =  1} },
	{ MODKEY|ControlMask,            XK_h,      setmfact,             {.f = -0.05} },
	{ MODKEY|ControlMask,            XK_l,      setmfact,             {.f =  0.05} },
	{ MODKEY|Mod1Mask,               XK_h,      setmfact,             {.f = -0.01} },
	{ MODKEY|Mod1Mask,               XK_l,      setmfact,             {.f =  0.01} },
	{ MODKEY,                     XK_Home,      focusmon,             {.i = -1} },
	{ MODKEY,                      XK_End,      focusmon,             {.i =  1} },
	{ MODKEY|ShiftMask,           XK_Home,      tagmon,               {.i =  1} },
	{ MODKEY|ShiftMask,            XK_End,      tagmon,               {.i = -1} },
	TAGKEYS(                         XK_1,      0)
	TAGKEYS(                         XK_2,      1)
	TAGKEYS(                         XK_3,      2)
	TAGKEYS(                         XK_4,      3)
	TAGKEYS(                         XK_5,      4)
	TAGKEYS(                         XK_6,      5)
	TAGKEYS(                         XK_7,      6)
	TAGKEYS(                         XK_8,      7)
	TAGKEYS(                         XK_9,      8)
	TAGKEYS(                         XK_0,      9)
	CLIENTKEYS(                      XK_q,      1)
	CLIENTKEYS(                      XK_w,      2)
	CLIENTKEYS(                      XK_e,      3)
	CLIENTKEYS(                      XK_r,      4)
	CLIENTKEYS(                      XK_t,      5)
	CLIENTKEYS(                      XK_a,      6)
	CLIENTKEYS(                      XK_s,      7)
	CLIENTKEYS(                      XK_d,      8)
	CLIENTKEYS(                      XK_f,      9)
	CLIENTKEYS(                      XK_g,     10)
	CLASSKEYS(                       XK_z,      1)
	CLASSKEYS(                       XK_x,      2)
	CLASSKEYS(                       XK_c,      3)
	CLASSKEYS(                       XK_v,      4)
	CLASSKEYS(                       XK_b,      5)
	CLASSKEYS(                       XK_n,      6)
	CLASSKEYS(                       XK_m,      7)
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click             event mask   button      function              argument */
	{ ClkClass,          0,           Button4,    class_adjacent,       {.i = -1} },
	{ ClkClass,          0,           Button5,    class_adjacent,       {.i =  1} },
	{ ClkTag,            0,           Button1,    tag_view,             {0} },
	{ ClkTag,            0,           Button2,    tag_toggle_c,         {0} },
	{ ClkTag,            0,           Button3,    tag_set,              {0} },
	{ ClkTag,            0,           Button4,    tag_adjacent,         {.i = -1} },
	{ ClkTag,            0,           Button5,    tag_adjacent,         {.i =  1} },
	{ ClkLayout,         0,           Button1,    setlayout,            {.i =  1} },
	{ ClkLayout,         0,           Button2,    setlayout,            {.i =  0} },
	{ ClkLayout,         0,           Button3,    setlayout,            {.i =  2} },
	{ ClkLayoutParam,    0,           Button1,    incnmaster,           {.i =  1} },
	{ ClkLayoutParam,    0,           Button3,    incnmaster,           {.i = -1 } },
	{ ClkLayoutParam,    0,           Button4,    setmfact,             {.f =  0.01} },
	{ ClkLayoutParam,    0,           Button5,    setmfact,             {.f = -0.01} },
	{ ClkClientList,     0,           Button1,    client_select,        {0} },
	{ ClkClientList,     0,           Button2,    spawn,                {.v = termcmd } },
	{ ClkClientList,     0,           Button4,    focusstack,           {.i = -1} },
	{ ClkClientList,     0,           Button5,    focusstack,           {.i =  1} },
	{ ClkClientWin,      MODKEY,      Button1,    movemouse,            {0} },
	{ ClkClientWin,      MODKEY,      Button2,    togglefloating,       {0} },
	{ ClkClientWin,      MODKEY,      Button3,    resizemouse,          {0} },
};
