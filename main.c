/*
Copyright 2017 jun7@hush.mail

This file is part of wyeb.

wyeb is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

wyeb is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with wyeb.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <webkit2/webkit2.h>
#include <JavaScriptCore/JSStringRef.h>
#include <gdk/gdkx.h>

#include "general.c"

#define MIMEOPEN "mimeopen -n %s"

#define DSET "set;"

#define LASTWIN (wins ? (Win *)*wins->pdata : NULL)
#define URI(win) (webkit_web_view_get_uri(win->kit) ?: "")
#define GFA(p, v) {g_free(p); p = v;}

typedef enum {
	Mnormal    = 0,
	Minsert    = 1,

	Mhint      = 2, //click
	Mhintopen  = 2 + 4,
	Mhintnew   = 2 + 8,
	Mhintback  = 2 + 16,
	Mhintdl    = 2 + 32,
	Mhintbkmrk = 2 + 64,
	Mhintspawn = 2 + 128,
	Mhintrange = 2 + 256,

	Mfind      = 512,
	Mopen      = 1024,
	Mopennew   = 2048,

	Mlist      = 4096,
	Mpointer   = 8192,
} Modes;

typedef struct {
	union {
		GtkWindow *win;
		GtkWidget *winw;
		GObject   *wino;
	};
	union {
		GtkProgressBar *prog;
		GtkWidget      *progw;
	};
	union {
		WebKitWebView *kit;
		GtkWidget     *kitw;
		GObject       *kito;
	};
	union {
		WebKitSettings *set;
		GObject        *seto;
	};
//	union {
//		GtkLabel  *lbl;
//		GtkWidget *lblw;
//	};
	union {
		GtkEntry  *ent;
		GtkWidget *entw;
		GObject   *ento;
	};
	gchar  *pageid;
	WebKitFindController *findct;

	//mode
	Modes   lastmode;
	Modes   mode;
	bool    crashed;

	//conf
	bool    userreq;
	gchar  *lasturiconf;
	gchar  *lastreset;
	gchar  *overset;

	//draw
	gdouble lastx;
	gdouble lasty;
	gchar  *msg;
	bool    smallmsg;

	//hittestresult
	gchar  *link;
	gchar  *focusuri;
	bool    usefocus;
	gchar  *linklabel;
	gchar  *image;
	gchar  *media;
	bool    oneditable;

	//pointer
	gdouble lastdelta;
	guint   lastkey;
	gdouble px;
	gdouble py;
	guint   pbtn;

	//entry
	GSList *undo;
	GSList *redo;
	gchar  *lastfind;
	GtkStyleProvider *sp;

	//misc
	gint    cursorx;
	gint    cursory;
	gchar  *spawn;
	gchar  *spawndir;
	bool    scheme;
	GTlsCertificateFlags tlserr;

	bool cancelcontext;
	bool cancelbtn1r;
	bool cancelmdlr;
} Win;

typedef struct {
	union {
		GtkWindow *win;
		GtkWidget *winw;
		GObject   *wino;
	};
	union {
		GtkProgressBar *prog;
		GtkWidget      *progw;
	};
	union {
		GtkBox    *box;
		GtkWidget *boxw;
	};
	union {
		GtkEntry  *ent;
		GtkWidget *entw;
	};
	WebKitDownload *dl;
	gchar  *name;
	gchar  *dldir;
	const gchar *dispname;
	guint64 len;
	bool    res;
	bool    finished;
} DLWin;


//@global
static gchar     *suffix = "";
static GPtrArray *wins = NULL;
static GPtrArray *dlwins = NULL;
static GQueue    *histimgs = NULL;
typedef struct {
	gchar *buf;
	gsize  size;
} Img;

static GKeyFile *conf = NULL;
static gchar    *confpath = NULL;
static gchar    *mdpath = NULL;
static gchar    *accelp = NULL;
static __time_t  conftime = 0;
static __time_t  mdtime = 0;
static __time_t  accelt = 0;
static GSList   *csslist = NULL;
static GSList   *csstimes = NULL;

static gchar *logs[] = {"h1", "h2", "h3", "h4", NULL};
static gint logfnum = sizeof(logs) / sizeof(*logs) - 1;
static gchar *logdir = NULL;

static GtkAccelGroup *accelg = NULL;
static WebKitWebContext *ctx = NULL;
static bool ephemeral = false;

typedef struct {
	gchar *group;
	gchar *key;
	gchar *val;
	gchar *desc;
} Conf;
Conf dconf[] = {
	{"all"   , "editor"       , MIMEOPEN,
		"editor=xterm -e nano %s\n"
		"editor=gvim --servername "APP" --remote-silent \"%s\""
	},
	{"all"   , "mdeditor"     , ""},
	{"all"   , "diropener"    , MIMEOPEN},
	{"all"   , "generator"    , "markdown -f -style %s"},

	{"all"   , "hintkeys"     , HINTKEYS},
	{"all"   , "keybindswaps" , "",
		"keybindswaps=Xx;ZZ;zZ ->if typed x: x to X, if Z: Z to Z"},

	{"all"   , "winwidth"     , "1000"},
	{"all"   , "winheight"    , "1000"},
	{"all"   , "zoom"         , "1.000"},

	{"all"   , "dlwinback"    , "false"},
	{"all"   , "dlwinclosemsec","3000"},
	{"all"   , "msgmsec"      , "600"},
	{"all"   , "ignoretlserr" , "false"},
	{"all"   , "histimgs"     , "66"},
	{"all"   , "histimgsize"  , "222"},
//	{"all"   , "configreload" , "true",
//			"reload last window when whiteblack.conf or reldomain are changed"},

	{"boot"  , "enablefavicon", "true"},
	{"boot"  , "extensionargs", "adblock:true;"},
	{"boot"  , "multiwebprocs", "false"},
	{"boot"  , "ephemeral"    , "fales"},

	{"search", "d"            , "https://duckduckgo.com/?q=%s"},
	{"search", "g"            , "https://www.google.com/search?q=%s"},
	{"search", "f"            , "https://www.google.com/search?q=%s&btnI=I"},
	{"search", "u"            , "http://www.urbandictionary.com/define.php?term=%s"},

	{"set:v"     , "enable-caret-browsing", "true"},
	{"set:script", "enable-javascript"    , "false"},
	{"set:image" , "auto-load-images"     , "true"},
	{"set:image" , "linkformat"   , "[![](%s) %.40s](%s)"},
	{"set:image" , "linkdata"     , "ftu"},

	{DSET    , "search"           , "https://www.google.com/search?q=%s", "search=g"},
	{DSET    , "usercss"          , "user.css"},
//	{DSET    , "loadsightedimages", "false"},
	{DSET    , "reldomaindataonly", "false"},
	{DSET    , "reldomaincutheads", "www.;wiki.;bbs.;developer."},
	{DSET    , "showblocked"      , "false"},

	{DSET    , "mdlbtnlinkaction" , "openback"},
	{DSET    , "mdlbtnleft"       , "prevwin", "mdlbtnleft=winlist"},
	{DSET    , "mdlbtnright"      , "nextwin"},
	{DSET    , "mdlbtnup"         , "top"},
	{DSET    , "mdlbtndown"       , "bottom"},
	{DSET    , "pressscrollup"    , "top"},
	{DSET    , "pressscrolldown"  , "bottom"},
	{DSET    , "rockerleft"       , "back"},
	{DSET    , "rockerright"      , "forward"},
	{DSET    , "rockerup"         , "quitprev"},
	{DSET    , "rockerdown"       , "quitnext"},

	{DSET    , "multiplescroll"   , "0"},

	{DSET    , "newwinhandle"     , "normal",
		"newwinhandle=notnew | ignore | back | normal"},
	{DSET    , "hjkl2arrowkeys"   , "false",
		"hjkl's default are scrolls, not arrow keys"},
	{DSET    , "linkformat"       , "[%.40s](%s)"},
	{DSET    , "linkdata"         , "tu", "t: title, u: uri, f: favicon\n"},
	{DSET    , "scriptdialog"     , "true"},
	{DSET    , "hackedhint4js"    , "true"},
	{DSET    , "hintrangemax"     , "9"},
	{DSET    , "dlmimetypes"      , "",
		"dlmimetypes=text/plain;video/;audio/;application/\n"
		"dlmimetypes=*"},
	{DSET    , "dlsubdir"         , ""},
	{DSET    , "entrybgcolor"     , "true"},
	{DSET    , "onstartmenu"      , "",
		"onstartmenu spawns a shell in the menu dir when load started before redirect"},
	{DSET    , "onloadmenu"       , "", "when load commited"},
	{DSET    , "onloadedmenu"     , "", "when load finished"},
	{DSET    , "spawnmsg"         , "false"},
	{DSET    , "hintstyle"        , "",
		"hintstyle=font-size: medium !important; -webkit-transform: rotate(-9deg);"},

	//changes
	//{DSET      , "auto-load-images" , "false"},
	//{DSET      , "enable-plugins"   , "false"},
	//{DSET      , "enable-java"      , "false"},
	//{DSET      , "enable-fullscreen", "false"},
};
static bool confbool(gchar *key)
{ return g_key_file_get_boolean(conf, "all", key, NULL); }
static gint confint(gchar *key)
{ return g_key_file_get_integer(conf, "all", key, NULL); }
static gdouble confdouble(gchar *key)
{ return g_key_file_get_double(conf, "all", key, NULL); }
static gchar *confcstr(gchar *key)
{//return is static string
	static gchar *str = NULL;
	GFA(str, g_key_file_get_string(conf, "all", key, NULL))
	return str;
}
static gchar *getset(Win *win, gchar *key)
{//return is static string
	static gchar *ret = NULL;
	if (!win)
	{
		GFA(ret, g_key_file_get_string(conf, DSET, key, NULL))
		return ret;
	}
	return g_object_get_data(win->seto, key);
}
static bool getsetbool(Win *win, gchar *key)
{
	return !g_strcmp0(getset(win, key), "true");
}

static gchar *usage =
	"usage: "APP" [[[suffix] action|\"\"] uri|arg|\"\"]\n"
	"  suffix: Process ID.\n"
	"    It is added to all directories conf, cache and etc.\n"
	"    '/' is default. '//' means $SUFFIX.\n"
	"  action: Such as new(default), open, opennew ...\n"
	"    Except 'new' and some, without a set of $SUFFIX and $WINID,\n"
	"    actions are sent to a window last focused\n"
	;

static gchar *mainmdstr =
"<!-- this is text/markdown -->\n"
"<meta charset=utf8>\n"
"Keys:\n"
"**e**: Edit this page;\n"
"**E**: Edit main config file;\n"
"**c**: Open config directory;\n"
"**m**: Show this page;\n"
"**M**: Show [history]("APP":history);\n"
"**b**: Add title and URI of a page opened to this page;\n"
"\n"
"If **e**,**E**,**c** don't work, open 'main.conf' in\n"
"config directory/'"APPNAME"' and edit '"MIMEOPEN"' values.\n"
"If you haven't any gui editor or filer, set them like 'xterm -e nano %s'.\n"
"\n"
"For other keys, see [help]("APP":help) assigned '**:**'.\n"
"Since this application is inspired by dwb and luakit,\n"
"usage is similar to them,\n"
"\n"
"<style>\n"
" .links a{\n"
"  background: linear-gradient(to right top, #ddf, white, white);\n"
"  color: #109; padding: 0 .3em; border-radius: .2em;\n"
"  text-decoration: none;\n"
"  display: inline-block;\n"
" }\n"
" .links img{height: 1em; width: 1em; margin-bottom: -.1em;}\n"
"</style>\n"
"<div class=links style=line-height:1.4;>\n"
"\n"
"["APPNAME"](https://github.com/jun7/"APP")\n"
"["APP"adblock](https://github.com/jun7/"APP"adblock)\n"
"[Arch Linux](https://www.archlinux.org/)\n"
"[dwb - ArchWiki](https://wiki.archlinux.org/index.php/dwb)\n"
;


//@misc
static bool isin(GPtrArray *ary, void *v)
{
	if (ary && v) for (int i = 0; i < ary->len; i++)
		if (v == ary->pdata[i]) return true;
	return false;
}
static gint threshold(Win *win)
{
	gint ret = 8;
	g_object_get(gtk_widget_get_settings(win->winw),
			"gtk-dnd-drag-threshold", &ret, NULL);
	return ret;
}
static const gchar *dldir(Win *win)
{//return is static string
	static gchar *ret = NULL;
	g_free(ret);
	ret = g_build_filename(
		g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD) ?:
		g_get_home_dir(),
		getset(win, "dlsubdir"),
		NULL
	);

	return ret;
}

static void quitif(bool force)
{
	if (!force && (wins->len != 0 || dlwins->len != 0)) return;

	gtk_main_quit();
}

static void alert(gchar *msg)
{
	GtkWidget *dialog = gtk_message_dialog_new(
			LASTWIN ? LASTWIN->win : NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", msg);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void append(gchar *path, const gchar *str)
{
	FILE *f = fopen(path, "a");
	if (!f)
	{
		alert(g_strdup_printf("fopen %s failed", path));
		return;
	}
	if (str)
		fputs(str, f);
	fputs("\n", f);
	fclose(f);
}
static void freeimg(Img *img)
{
	g_free(img ? img->buf : NULL);
	g_free(img);
}
static gboolean historycb(Win *win)
{
	if (ephemeral ||
		!isin(wins, win) ||
		!webkit_web_view_get_uri(win->kit) ||
		g_str_has_prefix(URI(win), APP":") ||
		webkit_web_view_is_loading(win->kit)
	) return false;

#define MAXSIZE 22222
	static gchar *current = NULL;
	static gint currenti = -1;
	static gint logsize = 0;
	if (!current || !g_file_test(logdir, G_FILE_TEST_EXISTS))
	{
		_mkdirif(logdir, false);

		currenti = -1;
		logsize = 0;
		for (gchar **file = logs; *file; file++)
		{
			currenti++;
			GFA(current, g_build_filename(logdir, *file, NULL))

			if (!g_file_test(current, G_FILE_TEST_EXISTS))
				break;

			struct stat info;
			stat(current, &info);
			logsize = info.st_size;
			if (logsize < MAXSIZE)
				break;
		}
	}

	gchar tstr[99];
	time_t t = time(NULL);
	strftime(tstr, sizeof(tstr), "%T/%d/%b/%y", localtime(&t));
	gchar *str = g_strdup_printf("%s %s %s", tstr, URI(win),
			webkit_web_view_get_title(win->kit) ?: "");

	static gchar *last = NULL;
	if (last && !strcmp(str + 18, last + 18))
	{
		GFA(str, NULL);
		freeimg(g_queue_pop_head(histimgs));
	}

	gint maxi = confint("histimgs");
	while (histimgs->length > 0 && histimgs->length >= maxi)
		freeimg(g_queue_pop_tail(histimgs));

	if (maxi)
	{
		gdouble ww = gtk_widget_get_allocated_width(win->kitw);
		gdouble wh = gtk_widget_get_allocated_height(win->kitw);
		gdouble scale = confint("histimgsize") / MAX(1, MAX(ww, wh));

		if (
			gtk_widget_get_visible(win->kitw) &&
			gtk_widget_is_drawable(win->kitw) &&
			scale > 0.0 && ww > 0.0 && wh > 0.0
		) {
			GdkPixbuf *pix = gdk_pixbuf_get_from_window(
				gtk_widget_get_window(win->kitw), 0, 0, ww, wh);

			GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
					pix, ww * scale, wh * scale, GDK_INTERP_BILINEAR);

			Img *img = g_new(Img, 1);
			gdk_pixbuf_save_to_buffer(scaled,
					&img->buf, &img->size,
					"jpeg", NULL, "quality", "77", NULL);

			g_queue_push_head(histimgs, img);

			g_object_unref(pix);
			g_object_unref(scaled);
		}
		else
			g_queue_push_head(histimgs, NULL);
	}

	if (!str) return false;

	append(current, str);
	GFA(last, str)

	logsize += strlen(str) + 1;
	if (logsize > MAXSIZE)
	{
		currenti++;
		if (currenti >= logfnum)
			currenti = 0;

		GFA(current, g_build_filename(logdir, logs[currenti], NULL))
		FILE *f = fopen(current, "w");
		fclose(f);
		logsize = 0;
	}
	return false;
}
static void addhistory(Win *win)
{
	const gchar *uri = URI(win);
	if (!*uri ||
			g_str_has_prefix(uri, APP":") ||
			g_str_has_prefix(uri, "about:")
			) return;

	g_timeout_add(100, (GSourceFunc)historycb, win);
}
static void removehistory()
{
	for (gchar **file = logs; *file; file++)
	{
		gchar *tmp = g_build_filename(logdir, *file, NULL);
		remove(tmp);
		g_free(tmp);
	}
	GFA(logdir, NULL)
}

static guint msgfunc = 0;
static gboolean clearmsgcb(Win *win)
{
	if (isin(wins, win))
	{
		GFA(win->msg, NULL)
		gtk_widget_queue_draw(win->winw);
	}

	msgfunc = 0;
	return false;
}
static void _showmsg(Win *win, gchar *msg, bool small)
{
	if (msgfunc) g_source_remove(msgfunc);
	GFA(win->msg, msg)
	win->smallmsg = small;
	msgfunc = g_timeout_add(confint("msgmsec"), (GSourceFunc)clearmsgcb, win);
	gtk_widget_queue_draw(win->winw);
}
static void showmsg(Win *win, const gchar *msg)
{ _showmsg(win, g_strdup(msg), false); }

static void send(Win *win, Coms type, gchar *args)
{
	gchar *arg = g_strdup_printf("%s:%c:%s", win->pageid, type, args ?: "");

	static bool alerted = false;
	if(!ipcsend(shared ? "ext" : win->pageid, arg) &&
			!win->crashed && !alerted && type == Cstart)
	{
		alerted = true;
		alert("Failed to communicate with the Web Extension.\n"
				"Make sure ext.so is in "EXTENSION_DIR".");
	}

	g_free(arg);
}
typedef struct {
	Win   *win;
	Coms   type;
	gchar *args;
} Send;
static gboolean senddelaycb(Send *s)
{
	if (isin(wins, s->win))
		send(s->win, s->type, s->args);
	else
		g_free(s->args);
	g_free(s);
	return false;
}
static void senddelay(Win *win, Coms type, gchar *args)
{
	Send *s = g_new0(Send, 1);
	s->win  = win;
	s->type = type;
	s->args = args;
	g_timeout_add(40, (GSourceFunc)senddelaycb, s);
}

static Win *winbyid(const gchar *pageid)
{
	for (int i = 0; i < wins->len; i++)
		if (!strcmp(pageid, ((Win *)wins->pdata[i])->pageid))
			return wins->pdata[i];
	return NULL;
}

static guint reloadfunc = 0;
static gboolean reloadlastcb()
{
	reloadfunc = 0;
	return false;
}
static void reloadlast()
{
//	if (!confbool("configreload")) return;
	if (reloadfunc) return;
	if (LASTWIN) webkit_web_view_reload(LASTWIN->kit);
	reloadfunc = g_timeout_add(300, (GSourceFunc)reloadlastcb, NULL);
}

static void putbtne(Win* win, GdkEventType type, guint btn)
{
	GdkEvent *e = gdk_event_new(type);
	GdkEventButton *eb = (GdkEventButton *)e;

	eb->window = gtk_widget_get_window(win->kitw);
	g_object_ref(eb->window);
	eb->send_event = false; //true destroys the mdl btn hack

	GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
	gdk_event_set_device(e, gdk_seat_get_pointer(seat));

	eb->x = win->px;
	eb->y = win->py;
	eb->button = btn;
	eb->type = type;
	gdk_event_put(e);
	gdk_event_free(e);
}

static void addhash(gchar *str, guint *hash)
{
	if (*hash == 0) *hash = 5381;
	while (*str++)
		*hash = *hash * 33 + *str;
}

static void setresult(Win *win, WebKitHitTestResult *htr)
{
	g_free(win->image);
	g_free(win->media);
	g_free(win->link);
	g_free(win->linklabel);

	win->image = win->media = win->link = win->linklabel = NULL;
	win->usefocus = false;

	if (!htr) return;

	win->image = webkit_hit_test_result_context_is_image(htr) ?
		g_strdup(webkit_hit_test_result_get_image_uri(htr)) : NULL;
	win->media = webkit_hit_test_result_context_is_media(htr) ?
		g_strdup(webkit_hit_test_result_get_media_uri(htr)) : NULL;
	win->link = webkit_hit_test_result_context_is_link(htr) ?
		g_strdup(webkit_hit_test_result_get_link_uri(htr)) : NULL;

	const gchar *label = webkit_hit_test_result_get_link_label(htr);
	if (!label)
		label = webkit_hit_test_result_get_link_title(htr);
	win->linklabel = label ? g_strdup(label): NULL;

	win->oneditable = webkit_hit_test_result_context_is_editable(htr);
}

static void undo(Win *win, GSList **undo, GSList **redo)
{
	if (!*undo && redo != undo) return;
	if (!*redo ||
			strcmp((*redo)->data, gtk_entry_get_text(win->ent)))
		*redo = g_slist_prepend(*redo,
				g_strdup(gtk_entry_get_text(win->ent)));

	if (redo == undo) return;
	if (!strcmp((*undo)->data, gtk_entry_get_text(win->ent)))
	{
		g_free((*undo)->data);
		*undo = g_slist_delete_link(*undo, *undo);
		if (!*undo) return;
	}
	gtk_entry_set_text(win->ent, (*undo)->data);
	gtk_editable_set_position((void *)win->ent, -1);
}


static bool run(Win *win, gchar* action, const gchar *arg); //declaration

//@textlink
static gchar   *tlpath = NULL;
static Win     *tlwin = NULL;
static __time_t tltime = 0;
static void textlinkcheck(bool monitor)
{
	if (!isin(wins, tlwin)) return;
	if (!getctime(tlpath, &tltime)) return;
	send(tlwin, Ctlset, tlpath);
}
static void textlinkon(Win *win)
{
	if (tltime == 0)
		monitor(tlpath, textlinkcheck);

	getctime(tlpath, &tltime);
	run(win, "openeditor", tlpath);
	tlwin = win;
}
static void textlinktry(Win *win)
{
	if (!tlpath)
		tlpath = g_build_filename(
			g_get_user_data_dir(), fullname, "textlink.txt", NULL);

	tlwin = NULL;
	send(win, Ctlget, tlpath);
}


//@conf
static void _kitprops(bool set, GObject *obj, GKeyFile *kf, gchar *group)
{
	//properties
	guint len;
	GParamSpec **list = g_object_class_list_properties(
			G_OBJECT_GET_CLASS(obj), &len);

	for (int i = 0; i < len; i++) {
		GParamSpec *s = list[i];
		const gchar *key = s->name;
		if (!(s->flags & G_PARAM_WRITABLE)) continue;
		if (set != g_key_file_has_key(kf, group, key, NULL)) continue;

		GValue gv = {0};
		g_value_init(&gv, s->value_type);

		g_object_get_property(obj, key, &gv);

		switch (s->value_type) {
		case G_TYPE_BOOLEAN:
			if (set) {
				bool v = g_key_file_get_boolean(kf, group, key, NULL);
				if (g_value_get_boolean(&gv) == v) continue;
				g_value_set_boolean(&gv, v);
			}
			else
				g_key_file_set_boolean(kf, group, key, g_value_get_boolean(&gv));
			break;
		case G_TYPE_UINT:
			if (set) {
				gint v = g_key_file_get_integer(kf, group, key, NULL);
				if (g_value_get_uint(&gv) == v) continue;
				g_value_set_uint(&gv, v);
			}
			else
				g_key_file_set_integer(kf, group, key, g_value_get_uint(&gv));
			break;
		case G_TYPE_STRING:
			if (set) {
				gchar *v = g_key_file_get_string(kf, group, key, NULL);
				if (!strcmp(g_value_get_string(&gv), v)) {
					g_free(v);
					continue;;
				}
				g_value_set_string(&gv, v);
				g_free(v);
			} else
				g_key_file_set_string(kf, group, key, g_value_get_string(&gv));
			break;
		default:
			if (!strcmp(key, "hardware-acceleration-policy")) {
				if (set) {
					gchar *str = g_key_file_get_string(kf, group, key, NULL);

					WebKitHardwareAccelerationPolicy v;
					if (!strcmp(str, "ALWAYS"))
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS;
					else if (!strcmp(str, "NEVER"))
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER;
					else //ON_DEMAND
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND;

					g_free(str);
					if (v == g_value_get_enum(&gv)) continue;

					g_value_set_enum(&gv, v);
				} else {
					switch (g_value_get_enum(&gv)) {
					case WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND:
						g_key_file_set_string(kf, group, key, "ON_DEMAND");
						break;
					case WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS:
						g_key_file_set_string(kf, group, key, "ALWAYS");
						break;
					case WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER:
						g_key_file_set_string(kf, group, key, "NEVER");
						break;
					}
				}
			}
			else
				continue;
		}
		if (set)
		{
			//D(change value %s, key)
			g_object_set_property(obj, key, &gv);
		}
		g_value_unset(&gv);
	}
}
static void checkconf(bool monitor); //declaration
static gchar *addcss(const gchar *name)
{
	gchar *path = path2conf(name);
	bool exists = g_file_test(path, G_FILE_TEST_EXISTS);
	bool already = false;

	for(GSList *next = csslist; next; next = next->next)
		if (!strcmp(next->data, name))
		{
			already = true;
			break;
		}

	if (!already)
	{
		__time_t *time = g_new0(__time_t, 1);
		if (exists)
			getctime(path, time);
		csslist  = g_slist_prepend(csslist,  g_strdup(name));
		csstimes = g_slist_prepend(csstimes, time);

		monitor(path, checkconf);
	}
	if (!exists)
		GFA(path, NULL)

	return path;
}
static void setcss(Win *win, gchar *namesstr)
{
	gchar **names = g_strsplit(namesstr, ";", -1);

	WebKitUserContentManager *cmgr =
		webkit_web_view_get_user_content_manager(win->kit);
	webkit_user_content_manager_remove_all_style_sheets(cmgr);

	for (gchar **name = names; *name; name++)
	{
		gchar *path = addcss(*name);
		if (!path) return;

		gchar *str;
		if (!g_file_get_contents (path, &str, NULL, NULL)) return;

		WebKitUserStyleSheet *css =
			webkit_user_style_sheet_new(str,
					WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
					WEBKIT_USER_STYLE_LEVEL_USER,
					NULL, NULL);

		webkit_user_content_manager_add_style_sheet(cmgr, css);

		g_free(str);
		g_free(path);
	}
	g_strfreev(names);
}
static void setprops(Win *win, GKeyFile *kf, gchar *group)
{
	//sets
	static int deps = 0;
	if (deps > 99) return;
	gchar **sets = g_key_file_get_string_list(kf, group, "sets", NULL, NULL);
	for (gchar **set = sets; set && *set; set++) {
		gchar *setstr = g_strdup_printf("set:%s", *set);
		deps++;
		setprops(win, kf, setstr);
		deps--;
		g_free(setstr);
	}
	g_strfreev(sets);

	//D(set props group: %s, group)
	_kitprops(true, win->seto, kf, group);

	//non webkit settings
	int len = sizeof(dconf) / sizeof(*dconf);
	for (int i = 0; i < len; i++) {
		if (strcmp(dconf[i].group, DSET)) continue;
		gchar *key = dconf[i].key;
		if (!g_key_file_has_key(kf, group, key, NULL)) continue;

		gchar *val = g_key_file_get_string(kf, group, key, NULL);

		if (!strcmp(key, "usercss") &&
			g_strcmp0(g_object_get_data(win->seto, key), val))
		{
			setcss(win, val);
		}
		g_object_set_data_full(win->seto, key, *val ? val : NULL, g_free);
	}
}
static void getkitprops(GObject *obj, GKeyFile *kf, gchar *group)
{
	_kitprops(false, obj, kf, group);
}
static bool _seturiconf(Win *win, const gchar* uri)
{
	bool ret = false;
	GFA(win->lastreset, g_strdup(uri))

	gchar **groups = g_key_file_get_groups(conf, NULL);
	for (gchar **gl = groups; *gl; gl++)
	{
		gchar *g = *gl;
		if (!g_str_has_prefix(g, "uri:")) continue;

		gchar *tofree = NULL;
		if (g_key_file_has_key(conf, g, "reg", NULL))
		{
			g = tofree = g_key_file_get_string(conf, g, "reg", NULL);
		} else {
			g += 4;
		}

		regex_t reg;
		if (regcomp(&reg, g, REG_EXTENDED | REG_NOSUB))
		{
			g_free(tofree);
			continue;
		}

		if (regexec(&reg, uri, 0, NULL, 0) == 0) {
			setprops(win, conf, *gl);
			GFA(win->lasturiconf, g_strdup(uri))
			ret = true;
		}

		regfree(&reg);
		g_free(tofree);
	}

	g_strfreev(groups);
	return ret;
}
static void resetconf(Win *win, bool force)
{
//	gchar *checks[] = {"reldomaindataonly", "reldomaincutheads", NULL};
	gchar *checks[] = {"reldomaincutheads", NULL};
	guint hash = 0;
	if (force)
		for (gchar **check = checks; *check; check++)
			addhash(getset(win, *check), &hash);

	if (win->lasturiconf || force)
	{
		GFA(win->lasturiconf, NULL)
		setprops(win, conf, DSET);
	}

	_seturiconf(win, URI(win));

	if (win->overset) {
		gchar *setstr = g_strdup_printf("set:%s", win->overset);
		setprops(win, conf, setstr);
		g_free(setstr);
	}

	if (force)
	{
		guint last = hash;
		hash = 0;
		for (gchar **check = checks; *check; check++)
			addhash(getset(win, *check), &hash);
		if (last != hash)
			reloadlast();
	}
}
static void getdconf(GKeyFile *kf, bool isnew)
{
	gint len = sizeof(dconf) / sizeof(*dconf);
	for (int i = 0; i < len; i++)
	{
		Conf c = dconf[i];

		if (!isnew)
		{
			if (!strcmp(c.group, "search")) continue;
			if (g_str_has_prefix(c.group, "set:")) continue;
		}

		if (!g_key_file_has_key(kf, c.group, c.key, NULL))
			g_key_file_set_value(kf, c.group, c.key, c.val);

		if (isnew && c.desc)
			g_key_file_set_comment(conf, c.group, c.key, c.desc, NULL);
	}

	if (!isnew) return;

	//sample and comment
	g_key_file_set_comment(conf, DSET, NULL, "Default of 'set's.", NULL);

	const gchar *sample = "uri:^https?://(www\\.)?foo\\.bar/.*";

	g_key_file_set_boolean(conf, sample, "enable-javascript", true);
	g_key_file_set_comment(conf, sample, NULL,
			"After 'uri:' is regular expressions for 'set'.\n"
			"preferential order of sections: Last > First > '"DSET"'"
			, NULL);

	sample = "uri:^foo|a-zA-Z0-9|*";

	g_key_file_set_string(conf, sample, "reg", "^foo[^a-zA-Z0-9]*$");
	g_key_file_set_comment(conf, sample, "reg",
			"Use reg if a regular expression has []."
			, NULL);

	g_key_file_set_string(conf, sample, "sets", "image;script");
	g_key_file_set_comment(conf, sample, "sets",
			"include other sets." , NULL);

	//fill vals not set
	if (LASTWIN)
		getkitprops(LASTWIN->seto, kf, DSET);
	else {
		WebKitSettings *set = webkit_settings_new();
		getkitprops((GObject *)set, kf, DSET);
		g_object_unref(set);
	}
}

void checkconf(bool frommonitor)
{
	//mainmd
	if (mdpath && wins->len && g_file_test(mdpath, G_FILE_TEST_EXISTS))
	{
		if (getctime(mdpath, &mdtime))
			for (int i = 0; i < wins->len; i++)
			{
				Win *win = wins->pdata[i];
				if (g_str_has_prefix(URI(win), APP":main"))
					webkit_web_view_reload(win->kit);
			}
	}

	//accels
	if (
		accelp &&
		g_file_test(accelp, G_FILE_TEST_EXISTS) &&
		getctime(accelp, &accelt)
	)
		gtk_accel_map_load(accelp);

	//conf
	if (!confpath)
	{
		confpath = path2conf("main.conf");
		monitor(confpath, checkconf);
	}

	bool newfile = false;
	if (!g_file_test(confpath, G_FILE_TEST_EXISTS))
	{
		if (frommonitor) return;
		if (!conf)
		{
			conf = g_key_file_new();
			getdconf(conf, true);
		}
		mkdirif(confpath);
		g_key_file_save_to_file(conf, confpath, NULL);
		newfile = true;
	}

	bool changed = getctime(confpath, &conftime);

	if (newfile) return;

	if (changed)
	{
		GKeyFile *new = g_key_file_new();

		GError *err = NULL;
		g_key_file_load_from_file(
				new, confpath, G_KEY_FILE_KEEP_COMMENTS, &err);
		if (err)
		{
			alert(err->message);
			g_error_free(err);

			if (!conf)
			{
				conf = g_key_file_new();
				getdconf(conf, true);
			}
		}
		else
		{
			getdconf(new, false);

			if (conf) g_key_file_free(conf);
			conf = new;

			if (ctx)
				webkit_web_context_set_tls_errors_policy(ctx,
						confbool("ignoretlserr") ?
						WEBKIT_TLS_ERRORS_POLICY_IGNORE :
						WEBKIT_TLS_ERRORS_POLICY_FAIL);

			if (wins)
				for (int i = 0; i < wins->len; i++)
					resetconf(wins->pdata[i], true);
		}
	}

	//css
	if (!changed && wins)
	{
		gchar *path = NULL;

		    GSList *nt   = csstimes;
		for(GSList *next = csslist ; next; next = next->next, nt = nt->next)
		{
			__time_t *time = nt->data;
			gchar *name = next->data;

			GFA(path, path2conf(name))

			bool exists = g_file_test(path, G_FILE_TEST_EXISTS);
			if (!exists && *time == 0) continue;
			*time = 0;

			if (exists && !getctime(path, time)) continue;

			for (int i = 0; i < wins->len; i++)
			{
				Win *lw = wins->pdata[i];
				gchar *us = getset(lw, "usercss");
				if (!us) continue;

				if (!exists || g_strrstr(us, name))
					setcss(lw, us);
			}
		}
		g_free(path);
	}
}

static void preparemd()
{
	prepareif(&mdpath, &mdtime, "mainpage.md", mainmdstr, checkconf);
}


//@context
static void settitle(Win *win, const gchar *pstr)
{
	if (pstr)
	{
		gtk_window_set_title(win->win, pstr);
		return;
	}
	if (win->crashed)
	{
		settitle(win, "!! Web Process Crashed !!");
		return;
	}

	const gchar *uri = URI(win);
	const gchar *wtitle = webkit_web_view_get_title(win->kit) ?: "";

	gchar *title = g_strdup_printf("%s%s%s%s%s%s - %s",
		win->tlserr ? "!TLS has errors! " : "",
		suffix            , *suffix      ? "| " : "",
		win->overset ?: "", win->overset ? "| " : "",
		wtitle, uri);

	gtk_window_set_title(win->win, title);
	g_free(title);
}
static void setbg(Win *win, int color); //declaration
static void pmove(Win *win, guint key); //declaration
static bool winlist(Win *win, guint type, cairo_t *cr); //declaration
static void _modechanged(Win *win)
{
	Modes last = win->lastmode;
	win->lastmode = win->mode;

	switch (last) {
	case Mnormal:
	case Minsert:
		break;

	case Mfind:
		GFA(win->lastfind, g_strdup(gtk_entry_get_text(win->ent)))
	case Mopen:
	case Mopennew:
		setbg(win, 0);

		gtk_widget_hide(win->entw);
		gtk_widget_grab_focus(win->kitw);
		break;

	case Mlist:
		gtk_widget_queue_draw(win->winw);
		gdk_window_set_cursor(gtk_widget_get_window(win->winw), NULL);
//		gtk_widget_set_sensitive(win->kitw, true);
		break;

	case Mpointer:
		win->pbtn = 1;
		gtk_widget_queue_draw(win->winw);
		break;

	case Mhint:
	case Mhintopen:
	case Mhintnew:
	case Mhintback:
	case Mhintdl:
	case Mhintbkmrk:
	case Mhintspawn:
	case Mhintrange:
		send(win, Crm, NULL);
		break;
	}

	//into
	Coms com = 0;
	switch (win->mode) {
	case Mnormal:
	case Minsert:
		break;

	case Mfind:
		if (win->crashed)
		{
			win->mode = Mnormal;
			break;
		}
		gtk_entry_set_text(win->ent, win->lastfind ?: "");
		GFA(win->lastfind, NULL)
	case Mopen:
	case Mopennew:
		if (win->mode != Mfind)
		{
			gchar *setstr = g_key_file_get_string(conf, DSET, "search", NULL);
			if (g_strcmp0(setstr, getset(win, "search")))
				setbg(win, 2);
			g_free(setstr);
		}

		gtk_widget_show(win->entw);
		gtk_widget_grab_focus(win->entw);
		undo(win, &win->undo, &win->undo);
		break;

	case Mlist:
//		gtk_widget_set_sensitive(win->kitw, false);
		winlist(win, 2, NULL);
		gtk_widget_queue_draw(win->winw);
		break;

	case Mpointer:
		pmove(win, 0);
		break;

	case Mhint:
		com = Cclick;
	case Mhintopen:
	case Mhintnew:
	case Mhintback:
		if (!com) com = Clink;
	case Mhintdl:
	case Mhintbkmrk:
		if (!com) com = Curi;
	case Mhintspawn:
		if (!com) com = Cspawn;
	case Mhintrange:
		if (!com) com = Crange;

		if (win->crashed)
			win->mode = Mnormal;
		else
		{
			send(win, Cstyle, getset(win, "hintstyle") ?: "");

			gboolean script = false; //dont use bool
			if (getsetbool(win, "hackedhint4js"))
				g_object_get(win->seto, "enable-javascript", &script, NULL);


			gchar *arg = g_strdup_printf("%09d%c%s",
					atoi(getset(win, "hintrangemax") ?: "0"),
					script ? 'y' : 'n', confcstr("hintkeys"));

			send(win, com, arg);
			g_free(arg);
		}

		break;
	}
}
static void update(Win *win)
{
	if (win->lastmode != win->mode) _modechanged(win);

	switch (win->mode) {
	case Mnormal: break;
	case Minsert:
		settitle(win, "-- INSERT MODE --");
		break;

	case Mlist:
		settitle(win, "-- LIST MODE --");
		break;

	case Mpointer:
		settitle(win, "-- POINTER MODE --");
		break;

	case Mhintrange:
		settitle(win, "-- RANGE MODE --");
		break;

	case Mhint:
	case Mhintopen:
	case Mhintnew:
	case Mhintback:
	case Mhintdl:
	case Mhintbkmrk:
	case Mhintspawn:

	case Mopen:
	case Mopennew:
	case Mfind:
		settitle(win, NULL);
		break;
	}

	//normal mode
	if (win->mode != Mnormal) return;

	if (win->focusuri || win->link)
	{
		bool f = (win->usefocus && win->focusuri) || !win->link;
		gchar *str = g_strconcat(f ? "Focus" : "Link",
				": ", f ? win->focusuri : win->link, NULL);
		settitle(win, str);
		g_free(str);
	}
	else
		settitle(win, NULL);
}
static void tonormal(Win *win)
{
	win->mode = Mnormal;
	update(win);
}


//@funcs for actions
static gchar *getsearch(gchar *pkey)
{
	gchar *ret = NULL;
	gchar **kv = g_key_file_get_keys(conf, "search", NULL, NULL);
	for (gchar **key = kv; *key; key++)
		if (!strcmp(pkey, *key))
		{
			ret = g_key_file_get_string(conf, "search", *key, NULL);
			break;
		}

	g_strfreev(kv);
	return ret;
}
static void _openuri(Win *win, const gchar *str, Win *caller)
{
	win->userreq = true;
	if (!str || !*str) str = APP":main";

	if (
		g_str_has_prefix(str, "http:") ||
		g_str_has_prefix(str, "https:") ||
		g_str_has_prefix(str, APP":") ||
		g_str_has_prefix(str, "file:") ||
		g_str_has_prefix(str, "about:")
	) {
		webkit_web_view_load_uri(win->kit, str);
		return;
	}

	if (str != gtk_entry_get_text(win->ent))
		gtk_entry_set_text(win->ent, str ?: "");

	if (g_str_has_prefix(str, "javascript:")) {
		webkit_web_view_run_javascript(win->kit, str + 11, NULL, NULL, NULL);
		return;
	}

	gchar *uri = NULL;

	gchar **stra = g_strsplit(str, " ", 2);

	if (*stra && stra[1])
	{
		gchar *search = getsearch(stra[0]);
		if (search) {
			char *esc = g_uri_escape_string(stra[1], NULL, true);
			uri = g_strdup_printf(search, esc);
			g_free(esc);

			GFA(win->lastfind, g_strdup(stra[1]))

			goto out;
		}
	}

	static regex_t *url = NULL;
	if (!url) {
		url = g_new(regex_t, 1);
		regcomp(url,
				"^([a-zA-Z0-9-]{2,256}\\.)+[a-z]{2,6}(/.*)?$",
				REG_EXTENDED | REG_NOSUB);
	}

	gchar *dsearch;
	if (regexec(url, str, 0, NULL, 0) == 0) {
		uri = g_strdup_printf("http://%s", str);
	} else if (dsearch = getset(caller ?: win, "search")) {
		char *esc = g_uri_escape_string(str, NULL, true);
		uri = g_strdup_printf(getsearch(dsearch) ?: dsearch, esc);
		g_free(esc);

		GFA(win->lastfind, g_strdup(str))
	}

	if (!uri) uri = g_strdup(str);
out:
	g_strfreev(stra);

	webkit_web_view_load_uri(win->kit, uri);
	g_free(uri);
}
static void openuri(Win *win, const gchar *str)
{ _openuri(win, str, NULL); }

static void spawnwithenv(Win *win, const gchar *shell, gchar* path,
		bool iscallback, gchar *jsresult,
		gchar *piped, gsize plen)
{
	gchar **argv;
	if (shell)
	{
		GError *err = NULL;
		if (!g_shell_parse_argv(shell, NULL, &argv, &err))
		{
			showmsg(win, err->message);
			g_error_free(err);
			return;
		}
	} else {
		argv = g_new0(gchar*, 2);
		argv[0] = g_strdup(path);
	}

	if (getsetbool(win, "spawnmsg"))
		_showmsg(win, g_strdup_printf("spawn: %s", shell ?: path), false);

	gchar *dir = shell ?
		(path ? g_strdup(path) : path2conf("menu")) : g_path_get_dirname(path);

	gchar **envp = g_get_environ();
	envp = g_environ_setenv(envp, "SUFFIX" , *suffix ? suffix : "/", true);
	envp = g_environ_setenv(envp, "ISCALLBACK",
			iscallback ? "1" : "0", true);
	envp = g_environ_setenv(envp, "JSRESULT", jsresult ?: "", true);

	gchar buf[9];
	snprintf(buf, 9, "%d", wins->len);
	envp = g_environ_setenv(envp, "WINSLEN", buf, true);
	envp = g_environ_setenv(envp, "WINID"  , win->pageid, true);
	envp = g_environ_setenv(envp, "CURRENTSET", win->overset ?: "", true);
	envp = g_environ_setenv(envp, "URI"    , URI(win), true);
	envp = g_environ_setenv(envp, "LINK_OR_URI", URI(win), true);
	envp = g_environ_setenv(envp, "DLDIR"  , dldir(win), true);

	const gchar *title = webkit_web_view_get_title(win->kit);
	if (!title) title = URI(win);
	envp = g_environ_setenv(envp, "TITLE" , title, true);
	envp = g_environ_setenv(envp, "LABEL_OR_TITLE" , title, true);

	envp = g_environ_setenv(envp, "FOCUSURI", win->focusuri ?: "", true);

	envp = g_environ_setenv(envp, "LINK", win->link ?: "", true);
	if (win->link)
	{
		envp = g_environ_setenv(envp, "LINK_OR_URI", win->link, true);
		envp = g_environ_setenv(envp, "LABEL_OR_TITLE" , win->link, true);
	}
	envp = g_environ_setenv(envp, "LINKLABEL", win->linklabel ?: "", true);
	if (win->linklabel)
	{
		envp = g_environ_setenv(envp, "LABEL_OR_TITLE" , win->linklabel, true);
	}

	envp = g_environ_setenv(envp, "MEDIA", win->media ?: "", true);
	envp = g_environ_setenv(envp, "IMAGE", win->image ?: "", true);

	envp = g_environ_setenv(envp, "MEDIA_IMAGE_LINK",
			win->media ?: win->image ?: win->link ?: "", true);

	gchar *cbtext;
	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_PRIMARY));
	envp = g_environ_setenv(envp, "PRIMARY"  , cbtext ?: "", true);
	envp = g_environ_setenv(envp, "SELECTION", cbtext ?: "", true);

	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_SECONDARY));
	envp = g_environ_setenv(envp, "SECONDARY", cbtext ?: "", true);

	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
	envp = g_environ_setenv(envp, "CLIPBOARD", cbtext ?: "", true);

	gint input;
	GPid child_pid;
	GError *err = NULL;
	if (piped ?
			!g_spawn_async_with_pipes(
				dir, argv, envp,
				G_SPAWN_SEARCH_PATH,
				NULL,
				NULL,
				&child_pid,
				&input,
				NULL,
				NULL,
				&err)
			:
			!g_spawn_async(
				dir, argv, envp,
				shell ? G_SPAWN_SEARCH_PATH : G_SPAWN_DEFAULT,
				NULL, NULL, &child_pid, &err))
	{
		showmsg(win, err->message);
		g_error_free(err);
	}
	else if (piped)
	{
		GIOChannel *io = g_io_channel_unix_new(input);
		g_io_channel_set_encoding(io, NULL, NULL);

		if (G_IO_STATUS_NORMAL !=
				g_io_channel_write_chars(
					io, piped, plen, NULL, &err))
		{
			showmsg(win, err->message);
			g_error_free(err);
		}
		g_io_channel_unref(io);
	}

	g_spawn_close_pid(child_pid);

	g_strfreev(envp);
	g_strfreev(argv);
	g_free(dir);
}

static void scroll(Win *win, gint x, gint y)
{
	GdkEvent *e = gdk_event_new(GDK_SCROLL);
	GdkEventScroll *es = (void *)e;

	es->window = gtk_widget_get_window(win->kitw);
	g_object_ref(es->window);
	es->send_event = false; //for multiplescroll
	//es->time   = GDK_CURRENT_TIME;
	es->direction =
		x < 0 ? GDK_SCROLL_LEFT :
		x > 0 ? GDK_SCROLL_RIGHT :
		y < 0 ? GDK_SCROLL_UP :
		        GDK_SCROLL_DOWN;

	es->delta_x = x;
	es->delta_y = y;

	GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
	gdk_event_set_device(e, gdk_seat_get_keyboard(seat));
	//gdk_seat_get_pointer() //witch is good?

	gdk_event_put(e);
	gdk_event_free(e);
}
void pmove(Win *win, guint key)
{
	//GDK_KEY_Down
	gdouble ww = gtk_widget_get_allocated_width(win->kitw);
	gdouble wh = gtk_widget_get_allocated_height(win->kitw);
	if (!win->px && !win->py)
	{
		win->px = ww * 3 / 7;
		win->py = wh * 1 / 3;
	}
	if (key == 0)
		win->lastdelta = MIN(ww, wh) / 7;

	guint lkey = win->lastkey;
	if (
		(key  == GDK_KEY_Up   && lkey == GDK_KEY_Down) ||
		(lkey == GDK_KEY_Up   && key  == GDK_KEY_Down) ||
		(key  == GDK_KEY_Left && lkey == GDK_KEY_Right) ||
		(lkey == GDK_KEY_Left && key  == GDK_KEY_Right) )
		win->lastdelta /= 2;

	if (win->lastdelta < 2) win->lastdelta = 2;
	gdouble d = win->lastdelta;
	if (key == GDK_KEY_Up   ) win->py -= d;
	if (key == GDK_KEY_Down ) win->py += d;
	if (key == GDK_KEY_Left ) win->px -= d;
	if (key == GDK_KEY_Right) win->px += d;

	win->px = CLAMP(win->px, 0, ww);
	win->py = CLAMP(win->py, 0, wh);

	win->lastdelta *= .9;
	win->lastkey = key;
	gtk_widget_queue_draw(win->winw);
}
static void sendkey(Win *win, guint key)
{
	GdkEvent *e = gdk_event_new(GDK_KEY_PRESS);
	GdkEventKey *ek = (GdkEventKey *)e;

	ek->window = gtk_widget_get_window(win->kitw);
	g_object_ref(ek->window);
	ek->send_event = true;
//	ek->time   = GDK_CURRENT_TIME;
	ek->keyval = key;
//	ek->state  = ek->state & ~GDK_MODIFIER_MASK;

	GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
	gdk_event_set_device(e, gdk_seat_get_keyboard(seat));

	gdk_event_put(e);
	gdk_event_free(e);
}

static void command(Win *win, const gchar *cmd, const gchar *arg)
{
	_showmsg(win, g_strdup_printf("Run '%s' with '%s'", cmd, arg), false);

	gchar *str = g_strdup_printf(cmd, arg);
	GError *err = NULL;
	if (!g_spawn_command_line_async(str, &err))
	{
		alert(err->message);
		g_error_free(err);
	}
	g_free(str);
}

static void openeditor(Win *win, const gchar *path, gchar *editor)
{
	if (!editor || !*editor)
		editor = confcstr("editor");
	if (!*editor)
		editor = MIMEOPEN;

	command(win, editor, path);
}
static void openconf(Win *win, bool shift)
{
	gchar *path;
	gchar *editor = NULL;

	const gchar *uri = URI(win);
	if (g_str_has_prefix(uri, APP":main"))
	{
		if (shift)
			path = confpath;
		else {
			path = mdpath;
			editor = confcstr("mdeditor");
		}
	} else if (!shift && g_str_has_prefix(uri, APP":"))
	{
		showmsg(win, "No config");
		return;
	} else {
		path = confpath;
		if (!shift)
		{

			gchar *esc = escape(uri);
			gchar *name = g_strdup_printf("uri:^%s", esc);
			if (!g_key_file_has_group(conf, name))
			{
				gchar *str = g_strdup_printf("\n[%s]", name);
				append(path, str);
				g_free(str);
			}
			g_free(name);
			g_free(esc);
		}
	}

	openeditor(win, path, editor);
}

static void nextwin(Win *win, bool next)
{
	GPtrArray *dwins = g_ptr_array_new();

	GdkWindow  *dw = gtk_widget_get_window(win->winw);
	GdkDisplay *dd = gdk_window_get_display(dw);
	for (int i = 0; i < wins->len; i++)
	{
		Win *lw = wins->pdata[i];
		GdkWindow *ldw = gtk_widget_get_window(lw->winw);

		if (gdk_window_get_state(ldw) & GDK_WINDOW_STATE_ICONIFIED)
			continue;

#ifdef GDK_WINDOWING_X11
		if (GDK_IS_X11_DISPLAY(dd) &&
			(gdk_x11_window_get_desktop(dw) !=
					gdk_x11_window_get_desktop(ldw))
		) continue;
#endif

		g_ptr_array_add(dwins, lw);
	}

	if (dwins->len < 2)
	{
		showmsg(win, "No other window");
		goto out;
	}

	if (next)
	{
		g_ptr_array_remove(wins, win);
		g_ptr_array_add(wins, win);
		//present first to keep focus on xfce
		gtk_window_present(((Win *)dwins->pdata[1])->win);
		gdk_window_lower(gtk_widget_get_window(win->winw));
	}
	else
	{
		gtk_window_present(
				((Win *)dwins->pdata[dwins->len - 1])->win);
	}

out:
	g_ptr_array_free(dwins, TRUE);
}
static gint inwins(Win *win, GSList **list, bool onlylen)
{
	guint len = wins->len - 1;

	GdkWindow  *dw = gtk_widget_get_window(win->winw);
	GdkDisplay *dd = gdk_window_get_display(dw);
	for (int i = 1; i < wins->len; i++)
	{
		Win *lw = wins->pdata[i];
		GdkWindow *ldw = gtk_widget_get_window(lw->winw);

#ifdef GDK_WINDOWING_X11
		if (GDK_IS_X11_DISPLAY(dd) &&
			(gdk_x11_window_get_desktop(dw) !=
					gdk_x11_window_get_desktop(ldw)))
		{
			len--;
			continue;
		}
#endif

		if (gdk_window_get_state(ldw) & GDK_WINDOW_STATE_ICONIFIED)
			len--;
		else if (!onlylen)
			*list = g_slist_append(*list, lw);
	}
	return len;
}
static bool quitnext(Win *win, bool next)
{
	if (inwins(win, NULL, true) < 1)
	{
		if (!strcmp(APP":main", URI(win)))
			return run(win, "quit", NULL);

		run(win, "showmainpage", NULL);
		showmsg(win, "Last Window");
		return false;
	}
	if (next)
		run(win, "nextwin", NULL);
	else
		run(win, "prevwin", NULL);
	return run(win, "quit", NULL);
}
bool winlist(Win *win, guint type, cairo_t *cr)
//type: 0: none 1:present 2:setcursor 3:close, and GDK_KEY_Down ... GDK_KEY_Right
{
	//for window select mode
	if (win->mode != Mlist) return false;
	GSList *actvs = NULL;
	guint len = inwins(win, &actvs, false);
	if (len < 1)
	{
		g_slist_free(actvs);
		return false;
	}

	gdouble w = gtk_widget_get_allocated_width(win->winw);
	gdouble h = gtk_widget_get_allocated_height(win->winw);

	gdouble yrate = h / w;

	gdouble yunitd = sqrt(len * yrate);
	gdouble xunitd = yunitd / yrate;
	gint yunit = yunitd;
	gint xunit = xunitd;

	if (yunit * xunit >= len)
		; //do nothing
	else if ((yunit + 1) * xunit >= len && (xunit + 1) * yunit >= len)
	{
		if (yunit > xunit)
			xunit++;
		else
			yunit++;
	}
	else if ((yunit + 1) * xunit >= len)
		yunit++;
	else if ((xunit + 1) * yunit >= len)
		xunit++;
	else {
		xunit++;
		yunit++;
	}

	switch (type) {
	case GDK_KEY_Up:
	case GDK_KEY_Down:
	case GDK_KEY_Left:
	case GDK_KEY_Right:
		if (win->cursorx > 0 && win->cursory > 0)
			break;
	case 2:
		win->cursorx = xunit / 2.0 - .5;
		win->cursory = yunit / 2.0 - .5;
		win->cursorx++;
		win->cursory++;
		if (type == 2)
			return true;
	}
	switch (type) {
	case GDK_KEY_Up:
		if (--win->cursory < 1) win->cursory = yunit;
		return true;
	case GDK_KEY_Down:
		if (++win->cursory > yunit) win->cursory = 1;
		return true;
	case GDK_KEY_Left:
		if (--win->cursorx < 1) win->cursorx = xunit;
		return true;
	case GDK_KEY_Right:
		if (++win->cursorx > xunit) win->cursorx = 1;
		return true;
	}

	gdouble uw = w / xunit;
	gdouble uh = h / yunit;

	if (cr)
	{
		cairo_set_source_rgba(cr, .4, .4, .4, .6);
		cairo_paint(cr);
	}

	gdouble px, py;
	gdk_window_get_device_position_double(
			gtk_widget_get_window(win->kitw),
			gdk_seat_get_pointer(gdk_display_get_default_seat(
					gdk_display_get_default())),
			&px, &py, NULL);

	bool ret = false;
	GSList *crnt = actvs;
	for (int yi = 0; yi < yunit; yi++) for (int xi = 0; xi < xunit; xi++)
	{
		if (!crnt) break;
		Win *lw = crnt->data;

		bool issuf =
			gtk_widget_get_visible(lw->kitw) &&
			gtk_widget_is_drawable(lw->kitw) ;

		gdouble lww = gtk_widget_get_allocated_width(lw->kitw);
		gdouble lwh = gtk_widget_get_allocated_height(lw->kitw);

		if (lww == 0 || lwh == 0) lww = lwh = 9;

		gdouble scale = MIN(uw / lww, uh / lwh) * (1.0 - 1.0/(yunit * xunit + 1));
		gdouble tw = lww * scale;
		gdouble th = lwh * scale;
		gdouble tx = xi * uw + (uw - tw) / 2;
		gdouble ty = yi * uh + (uh - th) / 2;
		gdouble tr = tx + tw;
		gdouble tb = ty + th;

		bool pin = win->cursorx + win->cursory == 0 ?
			px > tx && px < tr && py > ty && py < tb :
			xi + 1 == win->cursorx && yi + 1 == win->cursory;
		ret = ret || pin;

		if (pin)
		{
			gchar *title = g_strdup_printf("LIST| %s",
					webkit_web_view_get_title(lw->kit));
			settitle(win, title);
			g_free(title);

			win->cursorx = xi + 1;
			win->cursory = yi + 1;
		}

		if (!cr)
		{
			if (pin)
			{
				if (type == 1) //present
					gtk_window_present(lw->win);
				else if (type == 3) //close
				{
					run(lw, "quit", NULL);
					if (len > 1)
						gtk_widget_queue_draw(win->winw);
					else
						tonormal(win);
				}
				crnt = NULL;
				break;
			}

			crnt = crnt->next;
			continue;
		}

		cairo_reset_clip(cr);
		gdouble r   = 4 + th / 66.0;
		gdouble deg = M_PI / 180.0;
		cairo_new_sub_path (cr);
		cairo_arc (cr, tr - r, ty + r, r, -90 * deg,   0 * deg);
		cairo_arc (cr, tr - r, tb - r, r,   0 * deg,  90 * deg);
		cairo_arc (cr, tx + r, tb - r, r,  90 * deg, 180 * deg);
		cairo_arc (cr, tx + r, ty + r, r, 180 * deg, 270 * deg);
		cairo_close_path (cr);
		if (pin)
		{
			cairo_set_source_rgba(cr, .9, .0, .4, .7);
			cairo_set_line_width(cr, 6.0);
			cairo_stroke_preserve(cr);
		}
		cairo_clip(cr);

		if (issuf)
		{
			cairo_scale(cr, scale, scale);

			GdkPixbuf *pix = gdk_pixbuf_get_from_window(
				gtk_widget_get_window(lw->kitw), 0, 0, lww, lwh);
			cairo_surface_t *suf =
				gdk_cairo_surface_create_from_pixbuf(pix, 0, NULL);
			cairo_set_source_surface(cr, suf, tx / scale, ty / scale);

			cairo_paint(cr);

			cairo_surface_destroy(suf);
			g_object_unref(pix);

			cairo_scale(cr, 1 / scale, 1 / scale);
		}
		else
		{
			cairo_set_source_rgba(cr, .1, .0, .2, .4);
			cairo_paint(cr);
		}

		crnt = crnt->next;
	}

	g_slist_free(actvs);

	if (!ret)
		update(win);

	return ret;
}

static void addlink(Win *win, const gchar *title, const gchar *uri)
{
	preparemd();
	if (uri)
	{
		gchar *escttl = title ? g_markup_escape_text(title, -1) : NULL;
		if (!escttl || !*escttl) escttl = g_strdup(uri);
		gchar *fav = g_strdup_printf(APP":f/%s", uri);

		gchar *str;

		gchar *items = getset(win, "linkdata") ?: "tu";
		gint len = strlen(items);
		const gchar *as[9];
		for (int i = 0; i < 9; i++)
		{
			gchar d = i < len ? items[i] : '\0';
			as[i] =
				d == 't' ? escttl:
				d == 'u' ? uri:
				d == 'f' ? fav:
				"";
		}
		str = g_strdup_printf(getset(win, "linkformat"),
				as[0], as[1], as[2], as[3], as[4], as[5], as[6], as[7], as[8]);

		append(mdpath, str);

		g_free(str);
		g_free(fav);
		g_free(escttl);
	}
	else
		append(mdpath, NULL);

	showmsg(win, "Added");
	checkconf(false);
}

static void resourcecb(GObject *srco, GAsyncResult *res, gpointer p)
{
	if (!LASTWIN) return;
	Win *win = LASTWIN;
	GSList *sp = p;

	gsize len;
	guchar *data = webkit_web_resource_get_data_finish(
			(WebKitWebResource *)srco, res, &len, NULL);

	spawnwithenv(win, sp->data, sp->next->data, true, NULL, (gchar *)data, len);

	g_free(data);
	g_slist_free_full(sp, g_free);
}

static void jscb(GObject *po, GAsyncResult *pres, gpointer p)
{
	if (!p) return;
	GSList *sp = p;
	if (!sp->data) goto out;

	GError *err = NULL;
	WebKitJavascriptResult *res =
		webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(po), pres, &err);

	gchar *resstr = NULL;
	if (res)
	{
		JSValueRef jv = webkit_javascript_result_get_value(res);
		JSGlobalContextRef jctx =
			webkit_javascript_result_get_global_context(res);

		if (JSValueIsString(jctx, jv))
		{
			JSStringRef jstr = JSValueToStringCopy(jctx, jv, NULL);
			gsize len = JSStringGetMaximumUTF8CStringSize(jstr);
			resstr = g_malloc(len);
			JSStringGetUTF8CString(jstr, resstr, len);
			JSStringRelease(jstr);
		}
		else
			resstr = g_strdup("unsupported return value");

		webkit_javascript_result_unref(res);
	}
	else
	{
		resstr = g_strdup(err->message);
		g_error_free(err);
	}

	Win *win = g_object_get_data(po, "win");
	if (isin(wins, win))
		spawnwithenv(win, sp->data, sp->next->data, true, resstr, NULL, 0);

	g_free(resstr);
out:
	g_slist_free_full(sp, g_free);
}

//@actions
typedef struct {
	gchar *name;
	guint key;
	guint mask;
	gchar *desc;
} Keybind;
static Keybind dkeys[]= {
//every mode
	{"tonormal"      , GDK_KEY_Escape, 0, "To Normal Mode"},
	{"tonormal"      , '[', GDK_CONTROL_MASK},

//normal
	{"toinsert"      , 'i', 0},
	{"toinsertinput" , 'I', 0, "To Insert Mode with focus of first input"},
	{"topointer"     , 'p', 0, "pp resets damping"},
	{"tomdlpointer"  , 'P', 0, "makes middle click"},
	{"torightpointer", 'p', GDK_CONTROL_MASK, "right click"},

	{"tohint"        , 'f', 0},
	{"tohintnew"     , 'F', 0},
	{"tohintback"    , 't', 0},
	{"tohintdl"      , 'd', 0, "dl is Download"},
	{"tohintbookmark", 'T', 0},

	{"showdldir"     , 'D', 0},

	{"yankuri"       , 'y', 0, "Clipboard"},
	{"yanktitle"     , 'Y', 0, "Clipboard"},
	{"bookmark"      , 'b', 0, "arg: \"\" or \"uri + ' ' + label\""},
	{"bookmarkbreak" , 'B', 0, "Add line break to the main page"},

	{"quit"          , 'q', 0},
	{"quitall"       , 'Q', 0},
//	{"quit"          , 'Z', 0},

	{"scrolldown"    , 'j', 0},
	{"scrollup"      , 'k', 0},
	{"scrollleft"    , 'h', 0},
	{"scrollright"   , 'l', 0},

	{"arrowdown"     , 'j', GDK_CONTROL_MASK},
	{"arrowup"       , 'k', GDK_CONTROL_MASK},
	{"arrowleft"     , 'h', GDK_CONTROL_MASK},
	{"arrowright"    , 'l', GDK_CONTROL_MASK},

	{"pagedown"      , 'f', GDK_CONTROL_MASK},
	{"pageup"        , 'b', GDK_CONTROL_MASK},

	{"top"           , 'g', 0},
	{"bottom"        , 'G', 0},
	{"zoomin"        , '+', 0},
	{"zoomout"       , '-', 0},
	{"zoomreset"     , '=', 0},

	//tab
	{"nextwin"       , 'J', 0},
	{"prevwin"       , 'K', 0},
	{"quitnext"      , 'x', 0},
	{"quitprev"      , 'X', 0},
	{"winlist"       , 'z', 0},

	{"back"          , 'H', 0},
	{"forward"       , 'L', 0},
	{"stop"          , 's', 0},
	{"reload"        , 'r', 0},
	{"reloadbypass"  , 'R', 0, "Reload bypass cache"},

	{"find"          , '/', 0},
	{"findnext"      , 'n', 0},
	{"findprev"      , 'N', 0},
	{"findselection" , '*', 0},

	{"open"          , 'o', 0},
	{"opennew"       , 'w', 0, "New window"},
	{"edituri"       , 'O', 0, "arg or focused link or Current"},
	{"editurinew"    , 'W', 0, "arg or focused link or Current"},

//	{"showsource"    , 'S', 0}, //not good
	{"showhelp"      , ':', 0},
	{"showhistory"   , 'M', 0},
	{"showmainpage"  , 'm', 0},

	{"clearallwebsitedata", 'C', GDK_CONTROL_MASK},
	{"edit"          , 'e', 0},//normaly conf, if in main edit mainpage.
	{"editconf"      , 'E', 0},
	{"openconfigdir" , 'c', 0},

	{"setv"          , 'v', 0, "Use the 'set:v' section"},
	{"setscript"     , 's', GDK_CONTROL_MASK, "Use the 'set:script' section"},
	{"setimage"      , 'i', GDK_CONTROL_MASK, "set:image"},
	{"unset"         , 'u', 0},

	{"addwhitelist"  , 'a', 0,
		"URIs blocked by reldomain limitation and black list are added to whiteblack.conf"},
	{"addblacklist"  , 'A', 0, "URIs loaded"},

//insert
	{"textlink"      , 'e', GDK_CONTROL_MASK, "For textarea in insert mode"},

//nokey
	{"set"           , 0, 0, "Use 'set:' + arg section of main.conf. This toggles"},
	{"set2"          , 0, 0, "Not toggle"},
	{"new"           , 0, 0},
	{"newclipboard"  , 0, 0, "Open [arg + ' ' +] clipboard text in a new window."},
	{"newselection"  , 0, 0, "Open [arg + ' ' +] selection ..."},
	{"newsecondary"  , 0, 0, "Open [arg + ' ' +] secondaly ..."},
	{"findclipboard" , 0, 0},
	{"findsecondary" , 0, 0},

	{"tohintopen"    , 0, 0, "not click but open uri as opennew/back"},

	{"openback"      , 0, 0},
	{"openwithref"   , 0, 0, "current uri is sent as Referer"},
	{"download"      , 0, 0},
	{"showmsg"       , 0, 0},
	{"click"         , 0, 0, "x:y"},
	{"spawn"         , 0, 0, "arg is called with environment variables"},
	{"jscallback"    , 0, 0, "run script of arg1 and arg2 is called with $JSRESULT"},
	{"tohintcallback", 0, 0,
		"arg is called with env selected by hint."},
	{"tohintrange"   , 0, 0, "Same as tohintcallback but range."},
	{"sourcecallback", 0, 0, "The web resource is sent via pipe"},

//todo pagelist
//	{"windowimage"   , 0, 0}, //pageid
//	{"windowlist"    , 0, 0}, //=>pageid uri title
//	{"present"       , 0, 0}, //pageid
};
static gchar *ke2name(GdkEventKey *ke)
{
	guint key = ke->keyval;

	gchar **swaps = g_key_file_get_string_list(
			conf, "all", "keybindswaps", NULL, NULL);

	for (gchar **swap = swaps; *swap; swap++) {
		if (!**swap || !*(*swap + 1)) continue;
		if (key == **swap)
			key =  *(*swap + 1);
		else
		if (key == *(*swap + 1))
			key =  **swap;
		else
			continue;
		break;
	}
	g_strfreev(swaps);

	gint len = sizeof(dkeys) / sizeof(*dkeys);
	guint mask = ((ke->state & GDK_MODIFIER_MASK) & ~GDK_SHIFT_MASK);
	for (int i = 0; i < len; i++) {
		Keybind b = dkeys[i];
		if (key == b.key && b.mask == mask)
			return b.name;
	}
	return NULL;
}
//declaration
static Win *newwin(const gchar *uri, Win *cbwin, Win *caller, bool back);
static bool _run(Win *win, gchar* action, const gchar *arg, gchar *cdir, gchar *exarg)
{
	if (action == NULL) return false;
	gchar **retv = NULL; //hintret

#define Z(str, func) if (!strcmp(action, str)) {func; goto out;}
	//nokey nowin
	Z("new"         , win = newwin(arg, NULL, NULL, false))

#define CLIP(clip) \
		gchar *uri = g_strdup_printf(arg ? "%s %s" : "%s%s", arg ?: "", \
			gtk_clipboard_wait_for_text(gtk_clipboard_get(clip))); \
		win = newwin(uri, NULL, NULL, false); \
		g_free(uri)
	Z("newclipboard", CLIP(GDK_SELECTION_CLIPBOARD))
	Z("newselection", CLIP(GDK_SELECTION_PRIMARY))
	Z("newsecondary", CLIP(GDK_SELECTION_SECONDARY))
#undef CLIP

	if (win == NULL) return false;

	//internal
	Z("textlinkon" , textlinkon(win))
	Z("blocked"    ,
			_showmsg(win, g_strdup_printf("Blocked %s", arg), true);
			return true;)
	Z("openeditor" , openeditor(win, arg, NULL))
	Z("reloadlast" , reloadlast())
	Z("focusuri"   , win->usefocus = true; GFA(win->focusuri, g_strdup(arg)))

	if (!strcmp(action, "hintret"))
	{
		const gchar *orgarg = arg;
		retv = g_strsplit(arg, " ", 2);
		arg = *retv + 1;

		switch (win->mode) {
		case Mhintopen:
			action = "open"    ; break;
		case Mhintnew:
			action = "opennew" ; break;
		case Mhintback:
			action = "openback"; break;
		case Mhintdl:
			action = "download"; break;
		case Mhintbkmrk:
			arg = orgarg + 1;
			action = "bookmark"; break;

		case Mhintrange:
		case Mhintspawn:
			setresult(win, NULL);
			win->linklabel = g_strdup(retv[1]);

			switch (*orgarg) {
			case 'l':
				win->link  = g_strdup(arg); break;
			case 'i':
				win->image = g_strdup(arg); break;
			case 'm':
				win->media = g_strdup(arg); break;
			}
			action = "spawn";
			arg = win->spawn;
			cdir = win->spawndir;
			break;

		case Mhint:
		default:
			break;
		}
	}

	if (arg != NULL) {
		Z("find"  ,
				GFA(win->lastfind, g_strdup(arg))
				webkit_find_controller_search(win->findct, arg,
					WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
					WEBKIT_FIND_OPTIONS_WRAP_AROUND, G_MAXUINT))

		Z("open"   , openuri(win, arg))
		Z("opennew", newwin(arg, NULL, win, false))

		Z("bookmark",
			gchar **args = g_strsplit(arg, " ", 2);
			addlink(win, args[1], args[0]);
			g_strfreev(args);
		)

		//nokey
		Z("openback", showmsg(win, "Opened"); newwin(arg, NULL, win, true))
		Z("openwithref",
			WebKitURIRequest *req = webkit_uri_request_new(arg);
			SoupMessageHeaders *hdrs = webkit_uri_request_get_http_headers(req);
			if (hdrs) //scheme wyeb: returns NULL
				soup_message_headers_append(hdrs, "Referer", URI(win));
			webkit_web_view_load_request(win->kit, req);
			g_object_unref(req);
		)
		Z("download", webkit_web_view_download_uri(win->kit, arg))
		Z("showmsg" , showmsg(win, arg))
		Z("click",
			gchar **xy = g_strsplit(arg ?: "100:100", ":", 2);
			gdouble z = webkit_web_view_get_zoom_level(win->kit);
			win->px = atof(*xy) * z;
			win->py = atof(*(xy + 1)) * z;
			putbtne(win, GDK_BUTTON_PRESS, 1);
			putbtne(win, GDK_BUTTON_RELEASE, 1);
			g_strfreev(xy);
		)
		Z("spawn"   , spawnwithenv(win, arg, cdir, true, NULL, NULL, 0))
		Z("jscallback"    ,
			webkit_web_view_run_javascript(win->kit, arg, NULL, jscb,
			g_slist_prepend(g_slist_prepend(NULL, g_strdup(cdir)), g_strdup(exarg))))
		Z("tohintcallback", win->mode = Mhintspawn;
				GFA(win->spawn, g_strdup(arg))
				GFA(win->spawndir, g_strdup(cdir)))
		Z("tohintrange", win->mode = Mhintrange;
				GFA(win->spawn, g_strdup(arg))
				GFA(win->spawndir, g_strdup(cdir)))
		Z("sourcecallback",
			WebKitWebResource *res = webkit_web_view_get_main_resource(win->kit);
			webkit_web_resource_get_data(res, NULL, resourcecb,
				g_slist_prepend(g_slist_prepend(NULL, g_strdup(cdir)), g_strdup(arg)))
		)
	}

	Z("tonormal"    , win->mode = Mnormal)

	Z("toinsert"    , win->mode = Minsert)
	Z("toinsertinput", win->mode = Minsert; send(win, Ctext, NULL))

	if (!strcmp(action, "torightpointer"))
	{
		action = "topointer";
		win->pbtn = 3;
	}
	if (!strcmp(action, "tomdlpointer"))
	{
		action = "topointer";
		win->pbtn = 2;
	}
	Z("topointer"   , win->mode = win->mode == Mpointer ? Mnormal : Mpointer)

	Z("tohint"      , win->mode = Mhint)
	Z("tohintopen"  , win->mode = Mhintopen)
	Z("tohintnew"   , win->mode = Mhintnew)
	Z("tohintback"  , win->mode = Mhintback)
	Z("tohintdl"    , win->mode = Mhintdl)
	Z("tohintbookmark", win->mode = Mhintbkmrk)
	Z("tohintrange" , win->mode = Mhintrange)

	Z("showdldir"   ,
		command(win, confcstr("diropener"), dldir(win));
	)

	Z("yankuri"     ,
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), URI(win), -1);
		showmsg(win, "URI is yanked to clipboard")
	)
	Z("yanktitle"   ,
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
			webkit_web_view_get_title(win->kit) ?: "", -1);
		showmsg(win, "Title is yanked to clipboard")
	)
	Z("bookmark"    , addlink(win, webkit_web_view_get_title(win->kit), URI(win)))
	Z("bookmarkbreak", addlink(win, NULL, NULL))

	Z("quit"        , gtk_widget_destroy(win->winw); return true)
	Z("quitall"     , quitif(true))

	if (win->mode == Mpointer)
	{
		Z("scrolldown" , pmove(win, GDK_KEY_Down))
		Z("scrollup"   , pmove(win, GDK_KEY_Up))
		Z("scrollleft" , pmove(win, GDK_KEY_Left))
		Z("scrollright", pmove(win, GDK_KEY_Right))
	}
	bool arrow = getsetbool(win, "hjkl2arrowkeys");
	Z(arrow ? "scrolldown"  : "arrowdown" , sendkey(win, GDK_KEY_Down))
	Z(arrow ? "scrollup"    : "arrowup"   , sendkey(win, GDK_KEY_Up))
	Z(arrow ? "scrollleft"  : "arrowleft" , sendkey(win, GDK_KEY_Left))
	Z(arrow ? "scrollright" : "arrowright", sendkey(win, GDK_KEY_Right))
	Z(arrow ? "arrowdown"  : "scrolldown" , scroll(win, 0, 1))
	Z(arrow ? "arrowup"    : "scrollup"   , scroll(win, 0, -1))
	Z(arrow ? "arrowleft"  : "scrollleft" , scroll(win, -1, 0))
	Z(arrow ? "arrowright" : "scrollright", scroll(win, 1, 0))

	Z("pagedown"    , sendkey(win, GDK_KEY_Page_Down))
	Z("pageup"      , sendkey(win, GDK_KEY_Page_Up))

	Z("top"         , sendkey(win, GDK_KEY_Home))
	Z("bottom"      , sendkey(win, GDK_KEY_End))
	Z("zoomin"      ,
			gdouble z = webkit_web_view_get_zoom_level(win->kit);
			webkit_web_view_set_zoom_level(win->kit, z * 1.06))
	Z("zoomout"     ,
			gdouble z = webkit_web_view_get_zoom_level(win->kit);
			webkit_web_view_set_zoom_level(win->kit, z / 1.06))
	Z("zoomreset"   ,
			webkit_web_view_set_zoom_level(win->kit, 1.0))

	Z("nextwin"     , nextwin(win, true))
	Z("prevwin"     , nextwin(win, false))
	Z("winlist"     ,
			if (inwins(win, NULL, true) > 0)
				win->mode = Mlist;
			else
				showmsg(win, "No other window");
	)

	Z("quitnext"    , return quitnext(win, true))
	Z("quitprev"    , return quitnext(win, false))

	Z("back"        ,
			if (webkit_web_view_can_go_back(win->kit))
			webkit_web_view_go_back(win->kit);
			else
			showmsg(win, "No Previous Page")
	)
	Z("forward"     ,
			if (webkit_web_view_can_go_forward(win->kit))
			webkit_web_view_go_forward(win->kit);
			else
			showmsg(win, "No Next Page")
	 )
	Z("stop"        , webkit_web_view_stop_loading(win->kit))
	Z("reload"      , webkit_web_view_reload(win->kit))
	Z("reloadbypass", webkit_web_view_reload_bypass_cache(win->kit))

	Z("find"        , win->mode = Mfind)
	Z("findnext"    ,
			webkit_find_controller_search_next(win->findct);
			senddelay(win, Cfocus, NULL);
			)
	Z("findprev"    ,
			webkit_find_controller_search_previous(win->findct);
			senddelay(win, Cfocus, NULL);
			)
#define CLIP(clip) \
	run(win, "find", gtk_clipboard_wait_for_text(gtk_clipboard_get(clip))); \
	senddelay(win, Cfocus, NULL);

	Z("findselection", CLIP(GDK_SELECTION_PRIMARY))
	Z("findclipboard", CLIP(GDK_SELECTION_CLIPBOARD))
	Z("findsecondary", CLIP(GDK_SELECTION_SECONDARY))
#undef CLIP

	Z("open"        , win->mode = Mopen)
	Z("edituri"     ,
			win->mode = Mopen;
			gtk_entry_set_text(win->ent, arg ?: win->focusuri ?: URI(win)))
	Z("opennew"     , win->mode = Mopennew)
	Z("editurinew"  ,
			win->mode = Mopennew;
			gtk_entry_set_text(win->ent, arg ?: win->focusuri ?: URI(win)))

//	Z("showsource"  , )
	Z("showhelp"    , openuri(win, APP":help"))
	Z("showhistory" , openuri(win, APP":history"))
	Z("showmainpage", openuri(win, APP":main"))

	Z("clearallwebsitedata",
			WebKitWebsiteDataManager * mgr =
				webkit_web_context_get_website_data_manager(ctx);
			webkit_website_data_manager_clear(mgr,
				WEBKIT_WEBSITE_DATA_ALL, 0, NULL, NULL, NULL);

			removehistory();
			webkit_favicon_database_clear(
				webkit_web_context_get_favicon_database(ctx));

			showmsg(win, action);
	)
	Z("edit"        , openconf(win, false))
	Z("editconf"    , openconf(win, true))
	Z("openconfigdir",
			gchar *dir = path2conf(arg);
			command(win, confcstr("diropener"), dir);
			g_free(dir);
	)

	Z("setv"        , return run(win, "set", "v"))
	Z("setscript"   , return run(win, "set", "script"))
	Z("setimage"    , return run(win, "set", "image"))
	Z("unset"       , return run(win, "set", NULL))
	Z("set"         ,
			if (g_strcmp0(win->overset, arg))
				GFA(win->overset, g_strdup(arg))
			else
				GFA(win->overset, NULL)
			resetconf(win, true))
	Z("set2"        ,
			GFA(win->overset, g_strdup(arg))
			resetconf(win, true))

	Z("addwhitelist", send(win, Cwhite, "white"))
	Z("addblacklist", send(win, Cwhite, "black"))

	Z("textlink", textlinktry(win));

	gchar *msg = g_strdup_printf("Invalid action! %s arg: %s", action, arg);
	showmsg(win, msg);
	puts(msg);
	g_free(msg);
	return false;

#undef Z
out:
	update(win);
	if (retv)
		g_strfreev(retv);
	return true;
}
bool run(Win *win, gchar* action, const gchar *arg)
{
	return _run(win, action, arg, NULL, NULL);
}
static bool setact(Win *win, gchar *key, const gchar *spare)
{
	gchar *act = getset(win, key);
	if (!act) return false;
	gchar **acta = g_strsplit(act, " ", 2);
	run(win, acta[0], acta[1] ?: spare);
	g_strfreev(acta);
	return true;
}


//@win and cbs:
static gboolean focuscb(Win *win)
{
	g_ptr_array_remove(wins, win);
	g_ptr_array_insert(wins, 0, win);
	checkconf(false);
	if (!webkit_web_view_is_loading(win->kit) &&
			webkit_web_view_get_uri(win->kit))
	{
		addhistory(win);
		textlinkcheck(false);
	}

	return false;
}
static gboolean focusoutcb(Win *win)
{
	if (win->mode == Mlist)
		tonormal(win);
	return false;
}
static gboolean drawcb(GtkWidget *ww, cairo_t *cr, Win *win)
{
	static guint csize = 0;
	if (!csize) csize = gdk_display_get_default_cursor_size(
					gtk_widget_get_display(win->winw));

	if (win->lastx || win->lastx || win->mode == Mpointer)
	{
		gdouble x, y, size;
		if (win->mode == Mpointer)
			x = win->px, y = win->py, size = csize / 3;
		else
			x = win->lastx, y = win->lasty, size = csize / 6;

		cairo_move_to(cr, x - size, y - size);
		cairo_line_to(cr, x + size, y + size);
		cairo_move_to(cr, x - size, y + size);
		cairo_line_to(cr, x + size, y - size);

		if (win->mode == Mpointer)
		{
			cairo_set_line_width(cr, 6);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
			cairo_stroke_preserve(cr);
			cairo_set_source_rgba(cr, .9, .0, .0, .9);
		} else
			cairo_set_source_rgba(cr, .9, .0, .0, .3);

		cairo_set_line_width(cr, 2);

		cairo_stroke(cr);
	}
	if (win->msg)
	{
		gint h;
		gdk_window_get_geometry(
				gtk_widget_get_window(LASTWIN->winw), NULL, NULL, NULL, &h);

		if (win->smallmsg)
			cairo_set_font_size(cr, csize * .6);
		else
			cairo_set_font_size(cr, csize * .8);

		h -= csize + (
				gtk_widget_get_visible(win->entw) ?
				gtk_widget_get_allocated_height(win->entw) : 0);

		cairo_set_source_rgba(cr, .9, .9, .9, .7);
		cairo_move_to(cr, csize, h);
		cairo_show_text(cr, win->msg);

		cairo_set_source_rgba(cr, .9, .0, .9, .7);
		cairo_move_to(cr, csize + csize / 30, h);
		cairo_show_text(cr, win->msg);
	}

	winlist(win, 0, cr);
	return false;
}


//@download
static void addlabel(DLWin *win, const gchar *str)
{
	GtkWidget *lbl = gtk_label_new(str);
	gtk_label_set_selectable((GtkLabel *)lbl, true);
	gtk_box_pack_start(win->box, lbl, true, true, 0);
	gtk_label_set_ellipsize((GtkLabel *)lbl, PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_show_all(lbl);
}
static void dldestroycb(DLWin *win)
{
	g_ptr_array_remove(dlwins, win);

	if (!win->finished)
		webkit_download_cancel(win->dl);

	g_free(win->name);
	g_free(win->dldir);
	g_free(win);

	quitif(false);
}
static gboolean dlclosecb(DLWin *win)
{
	if (isin(dlwins, win))
		gtk_widget_destroy(win->winw);

	return false;
}
static void dlfincb(DLWin *win)
{
	if (!isin(dlwins, win) || win->finished) return;

	win->finished = true;

	gchar *title;
	if (win->res)
	{
		title = g_strdup_printf("DL: Finished: %s", win->dispname);
		gtk_progress_bar_set_fraction(win->prog, 1);

		gchar *fn = g_filename_from_uri(
				webkit_download_get_destination(win->dl), NULL, NULL);

		const gchar *nfn = NULL;
		if (win->ent)
		{
			nfn = gtk_entry_get_text(win->ent);
			if (strcmp(fn, nfn) &&
				(g_file_test(nfn, G_FILE_TEST_EXISTS) ||
				 g_rename(fn, nfn) != 0)
			)
				nfn = fn; //failed

			gtk_widget_hide(win->entw);
		}

		gchar *pathstr = g_strdup_printf("=>  %s", nfn);
		addlabel(win, pathstr);

		g_free(pathstr);
		g_free(fn);

		g_timeout_add(confint("dlwinclosemsec"), (GSourceFunc)dlclosecb, win);
	}
	else
		title = g_strdup_printf("DL: Failed: %s", win->dispname);

	gtk_window_set_title(win->win, title);
	g_free(title);
}
static void dlfailcb(WebKitDownload *wd, GError *err, DLWin *win)
{
	if (!isin(dlwins, win)) return; //cancelled

	win->finished = true;

	addlabel(win, err->message);

	gchar *title;
	title = g_strdup_printf("DL: Failed: %s - %s", win->dispname, err->message);
	gtk_window_set_title(win->win, title);
	g_free(title);
}
static void dldatacb(DLWin *win)
{
	gdouble p = webkit_download_get_estimated_progress(win->dl);
	gtk_progress_bar_set_fraction(win->prog, p);

	gchar *title = g_strdup_printf(
			"DL: %.2f%%: %s ", (p * 100), win->dispname);
	gtk_window_set_title(win->win, title);
	g_free(title);
}
//static void dlrescb(DLWin *win) {}
static void dldestcb(DLWin *win)
{
	gchar *fn = g_filename_from_uri(
			webkit_download_get_destination(win->dl), NULL, NULL);

	win->entw = gtk_entry_new();
	gtk_entry_set_text(win->ent, fn);
	gtk_entry_set_alignment(win->ent, .5);

	gtk_box_pack_start(win->box, win->entw, true, true, 4);
	gtk_widget_show_all(win->entw);

	g_free(fn);
}
static gboolean dldecidecb(WebKitDownload *pdl, gchar *name, DLWin *win)
{
	const gchar *base = win->dldir ?: dldir(NULL);
	gchar *path = g_build_filename(base, name, NULL);

	gchar *check = g_path_get_dirname(path);
	if (strcmp(base, check))
		GFA(path, g_build_filename(base, name = "noname", NULL))
	g_free(check);

	gchar *org = g_strdup(path);
	for (int i = 2; g_file_test(path, G_FILE_TEST_EXISTS); i++)
		GFA(path, g_strdup_printf("%s.%d", org, i))
	g_free(org);

	gchar *uri = g_filename_to_uri(path, NULL, NULL);
	webkit_download_set_destination(pdl, uri);

	g_free(path);
	g_free(uri);


	//set view data
	win->res = true;

	win->name     = g_strdup(name);
	win->dispname = win->name ?: "";
	addlabel(win, win->name);

	WebKitURIResponse *res = webkit_download_get_response(win->dl);
	addlabel(win, webkit_uri_response_get_mime_type(res));
	win->len =  webkit_uri_response_get_content_length(res);

	if (win->len)
	{
		gdouble m = win->len / 1000000.0;

		gchar *sizestr = g_strdup_printf("size: %.3f MB", m);
		addlabel(win, sizestr);
		g_free(sizestr);
	}
	return true;
}
static gboolean dlkeycb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (GDK_KEY_q == ek->keyval) gtk_widget_destroy(w);
	return false;
}
static gboolean acceptfocuscb(GtkWindow *w)
{
	gtk_window_set_accept_focus(w, true);
	return false;
}
static void downloadcb(WebKitWebContext *ctx, WebKitDownload *pdl)
{
	DLWin *win = g_new0(DLWin, 1);
	win->dl    = pdl;

	WebKitWebView *kit = webkit_download_get_web_view(pdl);
	if (kit)
		win->dldir = g_strdup(dldir(g_object_get_data(G_OBJECT(kit), "win")));

	win->winw  = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(win->win, "DL : Waiting for a response.");
	gtk_window_set_default_size(win->win, 400, -1);
	SIGW(win->wino, "destroy"         , dldestroycb, win);
	SIGW(win->wino, "key-press-event" , dlkeycb    , win);

	win->boxw = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win->win), win->boxw);

	win->progw = gtk_progress_bar_new();
	gtk_box_pack_end(win->box, win->progw, true, true, 0);

	GObject *o = (GObject *)win->dl;
	SIG( o, "decide-destination" , dldecidecb, win);
	SIGW(o, "created-destination", dldestcb  , win);
//	SIGW(o, "notify::response"   , dlrescb   , win);
	SIG( o, "failed"             , dlfailcb  , win);
	SIGW(o, "finished"           , dlfincb   , win);
	SIGW(o, "received-data"      , dldatacb  , win);

	if (LASTWIN)
	{
		gint gy;
		gdk_window_get_geometry(
				gtk_widget_get_window(LASTWIN->winw), NULL, &gy, NULL, NULL);
		gint x, y;
		gtk_window_get_position(LASTWIN->win, &x, &y);
		gtk_window_move(win->win, MAX(0, x - gy * 2), MAX(0, y + gy));
	}

	if (confbool("dlwinback") && LASTWIN &&
			gtk_window_is_active(LASTWIN->win))
	{
		gtk_window_set_accept_focus(win->win, false);
		gtk_widget_show_all(win->winw);
//not works
//		gdk_window_restack(
//				gtk_widget_get_window(win->winw),
//				gtk_widget_get_window(LASTWIN->winw),
//				false);
//		gdk_window_lower();
		gtk_window_present(LASTWIN->win);
		g_timeout_add(200, (GSourceFunc)acceptfocuscb, win->win);
	} else {
		gtk_widget_show_all(win->winw);
	}

	addlabel(win, webkit_uri_request_get_uri(webkit_download_get_request(pdl)));
	g_ptr_array_insert(dlwins, 0, win);
}


//@uri scheme
static gchar *histdata()
{
	GSList *hist = NULL;
	gint start = 0;
	gint num = 0;
	__time_t mtime = 0;

	bool imgs = confint("histimgs");

	for (int j = 2; j > 0; j--) for (int i = 0; i < logfnum ;i++)
	{
		gchar *path = g_build_filename(logdir, logs[i], NULL);
		bool exists = g_file_test(path, G_FILE_TEST_EXISTS);

		if (!start) {
			if (exists)
			{
				struct stat info;
				stat(path, &info);
				if (mtime > info.st_mtime)
					start++;
				mtime = info.st_mtime;
			} else {
				start++;
			}
		} else start++;

		if (!start) continue;
		if (!exists) continue;
		if (start > logfnum) break;

		GIOChannel *io = g_io_channel_new_file(path, "r", NULL);
		gchar *line;
		while (g_io_channel_read_line(io, &line, NULL, NULL, NULL)
				== G_IO_STATUS_NORMAL)
		{
			hist = g_slist_prepend(hist, g_strsplit(line, " ", 3));
			num++;

			g_free(line);
		}
		g_io_channel_unref(io);
		g_free(path);
	}

	if (!num)
		return g_strdup("<h1>No Data</h1>");

	gchar *sv[num + 2];
	sv[0] = g_strdup_printf(
		"<html><meta charset=utf8>\n"
		"<style>\n"
		"p {margin:.7em 0; white-space:nowrap;}\n"
		"a, a > * {display:inline-block; vertical-align:middle;}"
		"a {color:inherit; text-decoration:none;}\n"
		"time {font-family:monospace;}\n"
		"a > span {padding:0 .6em; white-space:normal; word-wrap:break-word;}\n"
		"i {font-size:.79em; color:#43a;}\n"
		//for img
		"em {min-width:%dpx;}\n"
		"img {"
		" border-radius:.4em;"
		" box-shadow:0 .1em .1em 0 #cbf;"
		" display:block;"
		" margin:auto;"
		"}\n"
		"</style>\n"
		, confint("histimgsize"));
	sv[num + 1] = NULL;

	int i = 0;
	static int unique = 0;

	for (GSList *next = hist; next; next = next->next)
	{
		gchar **stra = next->data;
		gchar *escpd = g_markup_escape_text(stra[2] ?: stra[1], -1);

		if (imgs)
		{
			gchar *itag = i < histimgs->length ?
				g_strdup_printf("<em><img src="APP":histimg/%d/%d></img></em>", i, unique++)
				: g_strdup("");

			sv[++i] = g_strdup_printf(
					"<p><a href=%s>%s"
					"<span>%s<br><i>%s</i><br><time>%.11s</time></span></a></p>\n",
					stra[1], itag, escpd, stra[1], stra[0]);
			g_free(itag);
		} else
			sv[++i] = g_strdup_printf(
					"<p><a href=%s><time>%.11s</time>"
					"<span>%s<br><i>%s</i></span></a>\n",
					stra[1], stra[0], escpd, stra[1]);

		g_free(escpd);
		g_strfreev(stra);
	}
	g_slist_free(hist);

	gchar *allhist = g_strjoinv("", sv);
	for (int j = 0; j <= num; j++)
		g_free(sv[j]);

	return allhist;
}
static gchar *helpdata()
{
	gchar *data = g_strdup_printf(
		"<pre style=\"font-size: large\">\n"
		"%s\n"
		"mouse:\n"
		"  rocker gesture:\n"
		"    left press and       -        right: back\n"
		"    left press and move right and right: forward\n"
		"    left press and move up    and right: raise bottom window and close\n"
		"    left press and move down  and right: raise next   window and close\n"
		"  middle button:\n"
		"    on a link            : new background window\n"
		"    on free space        : raise bottom window\n"
		"    press and move left  : raise bottom window\n"
		"    press and move right : raise next   window\n"
		"    press and move up    : go to top\n"
		"    press and move down  : go to bottom\n"
		"    press and scroll up  : go to top\n"
		"    press and scroll down: go to bottom\n"
		"\n"
		"context-menu:\n"
		"  You can add your own script to context-menu. See 'menu' dir in\n"
		"  the config dir, or click 'editMenu' in the context-menu. SUFFIX,\n"
		"  ISCALLBACK, WINSLEN, WINID, URI, TITLE, PRIMARY/SELECTION,\n"
		"  SECONDARY, CLIPBORAD, LINK, LINK_OR_URI, LINKLABEL, LABEL_OR_TITLE,\n"
		"  MEDIA, IMAGE, MEDIA_IMAGE_LINK, FOCUSURI, CURRENTSET and DLDIR\n"
		"  are set as environment variables. Available\n"
		"  actions are in 'key:' section below. Of course it supports dir\n"
		"  and '.'. '.' hides it from menu but still available in the accels.\n"
		"accels:\n"
		"  You can add your own keys to access context-menu items we added.\n"
		"  To add Ctrl-Z to GtkAccelMap, insert '&lt;Primary&gt;&lt;Shift&gt;z' to the\n"
		"  last \"\" in the file 'accels' in the conf directory assigned 'c'\n"
		"  key, and remove the ';' at the beginning of line. alt is &lt;Alt&gt;.\n"
		"\n"
		"key:\n"
		"#%d - is ctrl\n"
		"#(null) is only for script\n"
		, usage, GDK_CONTROL_MASK);

	for (int i = 0; i < sizeof(dkeys) / sizeof(*dkeys); i++)
	{
		gchar *tmp = g_strdup_printf("%d - %-11s: %-22s : %s\n",
				dkeys[i].mask,
				gdk_keyval_name(dkeys[i].key),
				dkeys[i].name,
				dkeys[i].desc ?: "");
		gchar *last = data;
		data = g_strconcat(data, tmp, NULL);
		g_free(last);
		g_free(tmp);
	}
	return data;
}
static cairo_status_t faviconcairocb(void *p,
		const unsigned char *data, unsigned int len)
{
	g_memory_input_stream_add_data((GMemoryInputStream *)p,
			g_memdup(data, len), len, g_free);
	return CAIRO_STATUS_SUCCESS;
}
static void faviconcb(GObject *src, GAsyncResult *res, gpointer p)
{
	WebKitURISchemeRequest *req = p;
	cairo_surface_t * suf = webkit_favicon_database_get_favicon_finish(
			webkit_web_context_get_favicon_database(ctx), res, NULL);

	GInputStream *st = g_memory_input_stream_new();

	if (!suf)
		suf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);

	cairo_surface_write_to_png_stream(suf, faviconcairocb, st);

	webkit_uri_scheme_request_finish(req, st, -1, "image/png");

	cairo_surface_destroy(suf);
	g_object_unref(st);
	g_object_unref(req);
}
static gboolean _schemecb(WebKitURISchemeRequest *req)
{
	const gchar *path = webkit_uri_scheme_request_get_path(req);

	if (g_str_has_prefix(path, "f/"))
	{
		webkit_favicon_database_get_favicon(
				webkit_web_context_get_favicon_database(ctx),
				path + 2, NULL, faviconcb, req);
		return false;
	}

	gchar *type = NULL;
	gchar *data = NULL;
	gsize len = 0;
	if (g_str_has_prefix(path, "histimg/"))
	{
		gchar **args = g_strsplit(path, "/", 3);
		if (*(args + 1))
		{
			long i = atoi(args[1]);
			Img *img = g_queue_peek_nth(histimgs, i);
			if (img)
			{
				type = "image/jpeg";
				len = img->size;
				data = g_memdup(img->buf, len);
			}
		}
		g_strfreev(args);
	}
	if (!type)
	{
		type = "text/html";
		if (g_str_has_prefix(path, "main"))
		{
			preparemd();
			gchar *cmd = g_strdup_printf(confcstr("generator"), mdpath);
			g_spawn_command_line_sync(cmd, &data, NULL, NULL, NULL);
			g_free(cmd);
		}
		else if (g_str_has_prefix(path, "history"))
			data = histdata();
		else if (g_str_has_prefix(path, "help"))
			data = helpdata();
		if (!data)
			data = g_strdup("<h1>Empty</h1>");
		len = strlen(data);
	}

	GInputStream *st = g_memory_input_stream_new_from_data(data, len, g_free);
	webkit_uri_scheme_request_finish(req, st, len, type);
	g_object_unref(st);

	g_object_unref(req);
	return false;
}
static void schemecb(WebKitURISchemeRequest *req, gpointer p)
{
	WebKitWebView *kit = webkit_uri_scheme_request_get_web_view(req);
	Win *win = kit ? g_object_get_data(G_OBJECT(kit), "win") : NULL;
	if (win) win->scheme = true;

	g_object_ref(req);
	g_idle_add((GSourceFunc)_schemecb, req);
}


//@kit's cbs
static void destroycb(Win *win)
{
	g_ptr_array_remove(wins, win);

	quitif(false);

	send(win, Cfree, NULL);

	g_free(win->pageid);
	g_free(win->lasturiconf);
	g_free(win->lastreset);
	g_free(win->overset);
	g_free(win->msg);

	setresult(win, NULL);
	g_free(win->focusuri);

	g_free(win->spawn);
	g_free(win->spawndir);
	g_free(win->lastfind);
	g_free(win);
}
static void crashcb(Win *win)
{
	win->crashed = true;
	tonormal(win);
}
static void notifycb(Win *win) { update(win); }
static void progcb(Win *win)
{
	gdouble p = webkit_web_view_get_estimated_load_progress(win->kit);
	if (p == 1) {
		gtk_widget_hide(win->progw);
	} else {
		gtk_widget_show(win->progw);
		gtk_progress_bar_set_fraction(win->prog, p);
	}
}
static void favcb(Win *win)
{
	cairo_surface_t *suf = webkit_web_view_get_favicon(win->kit);
	if (suf)
	{
		GdkPixbuf *pix = gdk_pixbuf_get_from_surface(suf, 0, 0,
					cairo_image_surface_get_width(suf),
					cairo_image_surface_get_height(suf));

		gtk_window_set_icon(win->win, pix);
		g_object_unref(pix);
	}
	else
		gtk_window_set_icon(win->win, NULL);
}
static gboolean delaymdlrcb(Win *win)
{
	if (isin(wins, win))
		putbtne(win, GDK_BUTTON_RELEASE, 2);
	return false;
}
static gboolean keycb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (ek->is_modifier) return false;

	if (win->mode == Mpointer &&
			(ek->keyval == GDK_KEY_space || ek->keyval == GDK_KEY_Return))
	{
		putbtne(win, GDK_BUTTON_PRESS,  win->pbtn);
		if (win->pbtn == 2)
			g_timeout_add(40, (GSourceFunc)delaymdlrcb, win);
		else
			putbtne(win, GDK_BUTTON_RELEASE, win->pbtn);

		tonormal(win);
		return true;
	}

	gchar *action = ke2name(ek);

	if (action && !strcmp(action, "tonormal"))
	{
		bool ret = win->mode & Mhint || win->mode == Mpointer;

		if (win->mode == Mpointer)
			win->px = win->py = 0;

		if (win->mode == Mnormal)
		{
			send(win, Cblur, NULL);
			webkit_find_controller_search_finish(win->findct);
		}
		else
			tonormal(win);

		return ret;
	}

	if (win->mode == Minsert)
	{
		if (ek->state & GDK_CONTROL_MASK &&
				(ek->keyval == GDK_KEY_z || ek->keyval == GDK_KEY_Z))
		{
			if (ek->state & GDK_SHIFT_MASK)
				webkit_web_view_execute_editing_command(win->kit, "Redo");
			else
				webkit_web_view_execute_editing_command(win->kit, "Undo");

			return true;
		}
		if (action && !strcmp(action, "textlink"))
			return run(win, action, NULL);

		return false;
	}

	if (win->mode & Mhint)
	{
		gchar key[2] = {0};
		*key = ek->keyval;
		send(win, Ckey, key);
		return true;
	}

	if (win->mode == Mlist)
	{
#define Z(str, func) if (action && !strcmp(action, str)) {func;}
		Z("scrolldown"  , winlist(win, GDK_KEY_Down , NULL))
		Z("scrollup"    , winlist(win, GDK_KEY_Up   , NULL))
		Z("scrollleft"  , winlist(win, GDK_KEY_Left , NULL))
		Z("scrollright" , winlist(win, GDK_KEY_Right, NULL))

		Z("quit"     , winlist(win, 3, NULL))
		Z("quitnext" , winlist(win, 3, NULL))
		Z("quitprev" , winlist(win, 3, NULL))
#undef Z
		switch (ek->keyval) {
		case GDK_KEY_Down:
		case GDK_KEY_Up:
		case GDK_KEY_Left:
		case GDK_KEY_Right:
			winlist(win, ek->keyval, NULL);
			break;

		case GDK_KEY_Return:
		case GDK_KEY_space:
			winlist(win, 1, NULL);
			return true;

		case GDK_KEY_BackSpace:
		case GDK_KEY_Delete:
			winlist(win, 3, NULL);
			return true;
		}
		gtk_widget_queue_draw(win->winw);
		return true;
	}

	win->userreq = true;

	if (!action)
		return false;

	run(win, action, NULL);

	return true;
}
static gboolean keyrcb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (ek->is_modifier) return false;
	if (win->mode == Minsert) return false;
	if (win->mode & Mhint) return true;
	if (win->mode == Mlist) return true;
	if (ke2name(ek)) return true;
	return false;
}
static void targetcb(
		WebKitWebView *w,
		WebKitHitTestResult *htr,
		guint m,
		Win *win)
{
	setresult(win, htr);
	update(win);
}
static GdkEvent *pendingmiddlee = NULL;
static gboolean btncb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	win->userreq = true;

	if (e->type != GDK_BUTTON_PRESS) return false;

	if (win->mode == Mlist)
	{
		win->cursorx = win->cursory = 0;
		if ((e->button == 1 || e->button == 3) &&
				winlist(win, e->button, NULL))
			return true;

		tonormal(win);
		return true;
	}

	//workaround
	//for lacking of target change event when btn event happens with focus in;
	senddelay(win, Cmode, NULL);
//	if (win->oneditable)
//		win->mode = Minsert;
//	else
//		win->mode = Mnormal;

	update(win);

	//D(event button %d, e->button)
	switch (e->button) {
	case 1:
	case 2:
		win->lastx = e->x;
		win->lasty = e->y;
		gtk_widget_queue_draw(win->winw);

	if (e->button == 1) break;
	{
		//for lacking of target change event when btn event happens with focus in;
		//now this is also for back from mlist mode may be
		//and pointer mode making events without the target change.
		if (e->send_event)
		{
			win->lastx = win->lasty = 0;
			break;
		}

		GdkEvent *me = gdk_event_new(GDK_MOTION_NOTIFY);
		GdkEventMotion *em = (GdkEventMotion *)me;

		em->time   = e->time  ;
		em->window = e->window;
		g_object_ref(em->window);
		em->x      = e->x     ;
		em->y      = e->y     ;
		em->axes   = e->axes  ;
		em->state  = e->state ;
		em->device = e->device;
		em->x_root = e->x_root;
		em->y_root = e->y_root;

		em->x      = e->x - 10000; //move somewhere
		gtk_widget_event(win->kitw, me);
		em->x      = e->x;         //and now enter !!
		gtk_widget_event(win->kitw, me);

		em->axes = NULL; //not own then don't free
		gdk_event_free(me);

		if (pendingmiddlee)
			gdk_event_free(pendingmiddlee);
		pendingmiddlee = gdk_event_copy((GdkEvent *)e);
		return true;
	}
	case 3:
		if (e->state & GDK_BUTTON1_MASK) {
			win->cancelcontext = win->cancelbtn1r = true;

			gdouble
				deltax = (e->x - win->lastx) ,
				deltay = e->y - win->lasty;

			if (MAX(abs(deltax), abs(deltay)) < threshold(win) * 3)
			{ //default
				setact(win, "rockerleft", URI(win));
			}
			else if (abs(deltax) > abs(deltay)) {
				if (deltax < 0) //left
					setact(win, "rockerleft", URI(win));
				else //right
					setact(win, "rockerright", URI(win));
			} else {
				if (deltay < 0) //up
					setact(win, "rockerup", URI(win));
				else //down
					setact(win, "rockerdown", URI(win));
			}
		}
		else if (win->crashed && e->button == 3)
			run(win, "reload", NULL);

		break;
	}

	return false;
}
static gboolean btnrcb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	switch (e->button) {
	case 1:
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->winw);

		if (win->cancelbtn1r) {
			win->cancelbtn1r = false;
			return true;
		}
		break;
	case 2:
	{
		gdouble
			deltax = (e->x - win->lastx),
			deltay = e->y - win->lasty;

		if (win->lastx == 0 && win->lasty == 0)
			deltax = deltay = 0;

		win->lastx = win->lasty = 0;

		if (win->cancelmdlr)
		{
			win->cancelmdlr = false;
			return true;
		}

		if (MAX(abs(deltax), abs(deltay)) < threshold(win))
		{ //default
			if (win->oneditable)
			{
				((GdkEventButton *)pendingmiddlee)->send_event = true;
				gtk_widget_event(win->kitw, pendingmiddlee);
				gdk_event_free(pendingmiddlee);
				pendingmiddlee = NULL;
			}
			else if (win->link)
				setact(win, "mdlbtnlinkaction", win->link);
			else if (gtk_window_is_active(win->win))
				setact(win, "mdlbtnleft", URI(win));
		}
		else if (abs(deltax) > abs(deltay)) {
			if (deltax < 0) //left
				setact(win, "mdlbtnleft", URI(win));
			else //right
				setact(win, "mdlbtnright", URI(win));
		} else {
			if (deltay < 0) //up
				setact(win, "mdlbtnup", URI(win));
			else //down
				setact(win, "mdlbtndown", URI(win));
		}

		gtk_widget_queue_draw(win->winw);

		return true;
	}
	}

	update(win);

	return false;
}
static gboolean entercb(GtkWidget *w, GdkEventCrossing *e, Win *win)
{ //for checking drag end with button1
	if (
			!(e->state & GDK_BUTTON1_MASK) &&
			win->lastx + win->lasty)
	{
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->winw);
	}
	update(win);

	return false;
}
static gboolean motioncb(GtkWidget *w, GdkEventMotion *e, Win *win)
{
	if (win->mode == Mlist)
	{
		win->cursorx = win->cursory = 0;
		gtk_widget_queue_draw(win->winw);

		static GdkCursor *hand = NULL;
		if (!hand) hand = gdk_cursor_new(GDK_HAND2);
		gdk_window_set_cursor(gtk_widget_get_window(win->winw),
				winlist(win, 0, NULL) ? hand : NULL);

		return true;
	}
	return false;
}
static gboolean scrollcb(GtkWidget *w, GdkEventScroll *pe, Win *win)
{
	if (pe->send_event) return false;

	if (pe->state & GDK_BUTTON2_MASK && (
		((pe->direction == GDK_SCROLL_UP || pe->delta_y < 0) &&
		 setact(win, "pressscrollup", URI(win))
		) ||
		((pe->direction == GDK_SCROLL_DOWN || pe->delta_y > 0) &&
		setact(win, "pressscrolldown", URI(win))
		) )) {
			win->cancelmdlr = true;
			return true;
		}

	int times = atoi(getset(win, "multiplescroll") ?: "0");
	if (!times) return false;

	GdkEvent *e = gdk_event_new(GDK_SCROLL);
	GdkEventScroll *es = (void *)e;

	es->window = gtk_widget_get_window(win->kitw);
	g_object_ref(es->window);
	es->send_event = true;
	es->direction = pe->direction;
	es->delta_x = pe->delta_x;
	es->delta_y = pe->delta_y;
	es->x = pe->x;
	es->y = pe->y;
	es->device = pe->device;

	for (int i = 0; i < times; i++)
		gdk_event_put(e);

	gdk_event_free(e);
	return false;
}
static gboolean policycb(
		WebKitWebView *v,
		WebKitPolicyDecision *dec,
		WebKitPolicyDecisionType type,
		Win *win)
{
	if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return false;

	WebKitResponsePolicyDecision *rdec = (void *)dec;
//	WebKitURIRequest *req =
//		webkit_response_policy_decision_get_request(rdec);

	bool dl = false;
	gchar *msr = getset(win, "dlmimetypes");
	if (msr && *msr)
	{
		gchar **ms = g_strsplit(msr, ";", -1);
		WebKitURIResponse *res =
			webkit_response_policy_decision_get_response(rdec);
		const gchar *mime = webkit_uri_response_get_mime_type(res);
		for (gchar **m = ms; *m; m++)
			if (**m && (!strcmp(*m, "*") || g_str_has_prefix(mime, *m)))
			{
				dl = true;
				break;
			}
		g_strfreev(ms);
	}

	if (!dl && webkit_response_policy_decision_is_mime_type_supported(rdec))
		webkit_policy_decision_use(dec);
	else
		webkit_policy_decision_download(dec);
	return true;
}
static GtkWidget *createcb(Win *win)
{
	gchar *handle = getset(win, "newwinhandle");
	Win *new = NULL;

	if      (!g_strcmp0(handle, "notnew"))
		return win->kitw;
	else if (!g_strcmp0(handle, "ignore"))
		return NULL;
	else if (!g_strcmp0(handle, "back"))
		new = newwin(NULL, win, win, true);
	else
		new = newwin(NULL, win, win, false);

	return new->kitw;
}
static void closecb(Win *win)
{
	gtk_widget_destroy(win->winw);
}
static gboolean sdialogcb(Win *win)
{
	if (getsetbool(win, "scriptdialog"))
		return false;
	showmsg(win, "Script dialog is blocked");
	return true;
}
static void sendstart(Win *win)
{
	gchar head[3] = {0};
	head[0] = getsetbool(win, "reldomaindataonly") ? 'y' : 'n';
	head[1] = getsetbool(win, "showblocked"      ) ? 'y' : 'n';
	gchar *args = g_strconcat(head, getset(win, "reldomaincutheads"),  NULL);
	send(win, Cstart, args);
	g_free(args);
}
static void setspawn(Win *win, gchar *key)
{
	gchar *fname = getset(win, key);
	if (fname)
	{
		gchar *dir = path2conf("menu");
		gchar *path = g_build_filename(dir, fname, NULL);
		spawnwithenv(win, NULL, path, false, NULL, NULL, 0);
		g_free(dir);
		g_free(path);
	}
}
static void loadcb(WebKitWebView *k, WebKitLoadEvent event, Win *win)
{
	win->crashed = false;
	switch (event) {
	case WEBKIT_LOAD_STARTED:
		//D(WEBKIT_LOAD_STARTED %s, URI(win))
		if (tlwin == win) tlwin = NULL;
		win->px = win->py = 0;
		win->scheme = false;
		setresult(win, NULL);
		GFA(win->focusuri, NULL)

		if (win->mode == Minsert) send(win, Cblur, NULL); //clear im
		tonormal(win);
		if (win->userreq) {
			win->userreq = false; //currently not used
		}
		resetconf(win, false);
		sendstart(win);

		setspawn(win, "onstartmenu");
		break;
	case WEBKIT_LOAD_REDIRECTED:
		//D(WEBKIT_LOAD_REDIRECTED %s, URI(win))
		resetconf(win, false);
		sendstart(win);

		break;
	case WEBKIT_LOAD_COMMITTED:
		//D(WEBKIT_LOAD_COMMITED %s, URI(win))
		if (!win->scheme && g_str_has_prefix(URI(win), APP":"))
		{
			webkit_web_view_reload(win->kit);
			break;
		}

		send(win, Con, NULL);

		if (webkit_web_view_get_tls_info(win->kit, NULL, &win->tlserr))
		{
			if (win->tlserr) showmsg(win, "TLS Error");
		}
		else
			win->tlserr = 0;

		setspawn(win, "onloadmenu");
		break;
	case WEBKIT_LOAD_FINISHED:
		//D(WEBKIT_LOAD_FINISHED %s, URI(win))

		if (g_strcmp0(win->lastreset, URI(win)))
		{ //for load-failed before commit
			resetconf(win, false);
			sendstart(win);
		}
		else if (win->scheme || !g_str_has_prefix(URI(win), APP":"))
		{
			addhistory(win);
			setspawn(win, "onloadedmenu");
			send(win, Con, NULL); //for iframe
		}

		break;
	}
}

//@contextmenu
typedef struct {
	GtkAction *action; //if dir this is NULL
	gchar     *path;
	GSList    *actions;
} AItem;
static void clearai(gpointer p)
{
	AItem *a = p;
	if (a->action)
	{
		g_free(a->path);
		gtk_action_disconnect_accelerator(a->action);
		g_object_unref(a->action);
	}
	else
		g_slist_free_full(a->actions, clearai);
	g_free(a);
}
static void actioncb(GtkAction *action, AItem *ai)
{
	spawnwithenv(LASTWIN, NULL, ai->path, false, NULL, NULL, 0);
}
static guint menuhash = 0;
static GSList *dirmenu(
		WebKitContextMenu *menu,
		gchar *dir,
		gchar *parentaccel)
{
	GDir *gd = g_dir_open(dir, 0, NULL);
	GSList *ret = NULL;

	GSList *names = NULL;

	const gchar *dn;
	while (dn = g_dir_read_name(gd))
	{
		names = g_slist_insert_sorted(names, g_strdup(dn), (GCompareFunc)strcmp);
	}

	for (GSList *next = names; next; next = next->next)
	{
		gchar *org = next->data;
		gchar *name = org + 1;

		if (g_str_has_suffix(name, "---"))
		{
			if (menu)
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new_separator());
			g_free(org);
			continue;
		}


		AItem *ai = g_new0(AItem, 1);
		bool nodata = false;

		gchar *accel = g_strconcat(parentaccel, "/", name, NULL);
		gchar *path = g_build_filename(dir, org, NULL);

		if (g_file_test(path, G_FILE_TEST_IS_DIR))
		{
			WebKitContextMenu *sub = NULL;
			if (menu && *org != '.')
				sub = webkit_context_menu_new();

			ai->actions = dirmenu(sub, path, accel);
			if (!ai->actions)
				nodata = true;
			else if (menu && *org != '.')
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new_with_submenu(name, sub));

			g_free(path);
		} else {
			ai->action = gtk_action_new(name, name, NULL, NULL);
			ai->path = path;
			addhash(path, &menuhash);
			SIG(ai->action, "activate", actioncb, ai);

			gtk_action_set_accel_group(ai->action, accelg);
			gtk_action_set_accel_path(ai->action, accel);
			gtk_action_connect_accelerator(ai->action);

			if (menu && *org != '.')
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new(ai->action));
		}

		g_free(accel);
		if (nodata)
			g_free(ai);
		else
			ret = g_slist_append(ret, ai);

		g_free(org);
	}
	g_slist_free(names);

	g_dir_close(gd);

	return ret;
}
static void makemenu(WebKitContextMenu *menu); //declaration
static void accelmonitorcb(
		GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e)
{
	if (e == G_FILE_MONITOR_EVENT_CREATED)
		makemenu(NULL);
}
static void addscript(gchar* dir, gchar* name, gchar *script)
{
		gchar *ap = g_build_filename(dir, name, NULL);
		mkdirif(ap);
		FILE *f = fopen(ap, "w");
		fputs(script, f);
		fclose(f);
		g_chmod(ap, 0700);
		g_free(ap);
}
void makemenu(WebKitContextMenu *menu)
{
	static GSList *actions = NULL;
	static bool firsttime = true;

	gchar *dir = path2conf("menu");
	if (!g_file_test(dir, G_FILE_TEST_EXISTS))
	{
		addscript(dir, ".openNewRange"    , APP" // tohintrange "
				"'sh -c \""APP" // opennew $MEDIA_IMAGE_LINK\"'");
		addscript(dir, ".openNewSrcURI"   , APP" // tohintcallback "
				"'sh -c \""APP" // opennew $MEDIA_IMAGE_LINK\"'");
		addscript(dir, ".openWithRef"     , APP" // tohintcallback "
				"'sh -c \""APP" // openwithref $MEDIA_IMAGE_LINK\"'");
		addscript(dir, "0editMenu"        , APP" // openconfigdir menu");
		addscript(dir, "1bookmark"        , APP" // bookmark "
				"\"$LINK_OR_URI $LABEL_OR_TITLE\"");
		addscript(dir, "1duplicate"       , APP" // opennew $URI");
		addscript(dir, "1history"         , APP" // showhistory \"\"");
		addscript(dir, "1windowList"      , APP" // winlist \"\"");
		addscript(dir, "2main"            , APP" // open "APP":main");
		addscript(dir, "3---"             , "");
		addscript(dir, "3openClipboard"   , APP" // open \"$CLIPBOARD\"");
		addscript(dir, "3openClipboardNew", APP" // opennew \"$CLIPBOARD\"");
		addscript(dir, "3openSelection"   , APP" // open \"$PRIMARY\"");
		addscript(dir, "3openSelectionNew", APP" // opennew \"$PRIMARY\"");
		addscript(dir, "6searchDictionary", APP" // open \"u $PRIMARY\"");
		addscript(dir, "9---"             , "");
		addscript(dir, "9saveSource2DLdir", APP" // sourcecallback "
				"\"tee -a \\\"$DLDIR/"APP"-source\\\"\"");
		addscript(dir, "v---"             , "");
		addscript(dir, "vchromium"        , "chromium $LINK_OR_URI");
		addscript(dir, "xnoSuffixProcess" , APP" / new $LINK_OR_URI");
	}

	if (firsttime)
	{
		firsttime = false;
		accelg = gtk_accel_group_new();

		GFile *gf = g_file_new_for_path(dir);
		GFileMonitor *gm = g_file_monitor_directory(gf,
				G_FILE_MONITOR_NONE, NULL, NULL);
		SIG(gm, "changed", accelmonitorcb, NULL);
		g_object_unref(gf);

		accelp = path2conf("accels");
		monitor(accelp, checkconf);
	}

	if (g_file_test(accelp, G_FILE_TEST_EXISTS))
		gtk_accel_map_load(accelp);

	if (actions)
		g_slist_free_full(actions, clearai);

	WebKitContextMenuItem *sep = NULL;
	if (menu)
		webkit_context_menu_append(menu,
			sep = webkit_context_menu_item_new_separator());

	guint lasthash = menuhash;
	menuhash = 0;

	actions = dirmenu(menu, dir, "<window>");

	if (menu && !actions)
		webkit_context_menu_remove(menu, sep);

	if (lasthash != menuhash)
	{
		gtk_accel_map_save(accelp);
		getctime(accelp, &accelt);
	}

	g_free(dir);
}
static gboolean contextcb(WebKitWebView *web_view,
		WebKitContextMenu   *menu,
		GdkEvent            *e,
		WebKitHitTestResult *htr,
		Win                 *win
		)
{
	if (win->cancelcontext) {
		win->cancelcontext = false;
		return true;
	}

	setresult(win, htr);
	makemenu(menu);

	//GtkAction * webkit_context_menu_item_get_action(WebKitContextMenuItem *item);
	//GtkWidget * gtk_action_create_menu_item(GtkAction *action);
	return false;
}


//@entry
void setbg(Win *win, int color)
{
	static const gchar *colors[] = {"red", "skyblue"};
	static GtkStyleProvider *cps[2] = {NULL};
	if (!cps[0]) for (int i = 0; i < 2; i++)
		{
			GtkCssProvider *cssp = gtk_css_provider_new();
			gchar *sstr = g_strdup_printf("entry {background-color: %s}", colors[i]);
			gtk_css_provider_load_from_data(cssp, sstr, -1, NULL);

			cps[i] = (void *)cssp;
			g_free(sstr);
		}

	GtkStyleContext *sctx = gtk_widget_get_style_context(win->entw);
	if (win->sp)
		gtk_style_context_remove_provider(sctx, win->sp);

	win->sp = NULL;
	if (color < 1) return;
	if (!getsetbool(win, "entrybgcolor")) return;

	gtk_style_context_add_provider(sctx, win->sp = cps[color - 1],
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
static gboolean focusincb(Win *win)
{
	if (gtk_widget_get_visible(win->entw))
		tonormal(win);
	return false;
}
static gboolean entkeycb(GtkWidget *w, GdkEventKey *ke, Win *win)
{
	switch (ke->keyval) {
	case GDK_KEY_m:
		if (!(ke->state & GDK_CONTROL_MASK)) return false;
	case GDK_KEY_KP_Enter:
	case GDK_KEY_Return:
		{
			const gchar *text = gtk_entry_get_text(win->ent);
			gchar *action = NULL;
			switch (win->mode) {
			case Mfind:
				if (!win->lastfind || strcmp(win->lastfind, text))
					run(win, "find", text);

				senddelay(win, Cfocus, NULL);
				break;
			case Mopen:
				action = "open";
			case Mopennew:
				if (!action) action = "opennew";
				run(win, action, text);
				break;
			default:
					g_assert_not_reached();
			}
			tonormal(win);
		}
		break;

	case GDK_KEY_Escape:
		if (win->mode == Mfind) {
			webkit_find_controller_search_finish(win->findct);
		}
		tonormal(win);
		break;

	case GDK_KEY_Z:
	case GDK_KEY_n:
		if (!(ke->state & GDK_CONTROL_MASK)) return false;
		undo(win, &win->redo, &win->undo);
		break;
	case GDK_KEY_z:
	case GDK_KEY_p:
		if (!(ke->state & GDK_CONTROL_MASK)) return false;
		undo(win, &win->undo, &win->redo);
		break;

	default:
		return false;
	}
	return true;
}
static gboolean textcb(Win *win)
{
	if (win->mode == Mfind && gtk_widget_get_visible(win->entw)) {
		setbg(win, 0);

		const gchar *text = gtk_entry_get_text(win->ent);
		if (strlen(text) > 2)
			run(win, "find", text);
		else
			webkit_find_controller_search_finish(win->findct);
	}
	return false;
}
static void findfailedcb(Win *win)
{
	showmsg(win, "Not found");

	if (win->mode == Mfind)
		setbg(win, 1);
}
static void foundcb(Win *win)
{
	_showmsg(win, NULL, false); //clear
}
static gboolean detachcb(GtkWidget * w)
{
	gtk_widget_grab_focus(w);
	return false;
}

//@newwin
Win *newwin(const gchar *uri, Win *cbwin, Win *caller, bool back)
{
	Win *win = g_new0(Win, 1);
	win->userreq = true;

	win->winw = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gint w, h;
	if (caller)
	{
		win->overset = g_strdup(caller->overset);
		gtk_window_get_size(caller->win, &w, &h);
	}
	else
		w = confint("winwidth"), h = confint("winheight");
	gtk_window_set_default_size(win->win, w, h);

	gtk_widget_show_all(win->winw);
	gdk_flush();

	//delayed init
	if (!accelg) makemenu(NULL);
	gtk_window_add_accel_group(win->win, accelg);

	SIGA(win->wino, "draw"           , drawcb, win);
	SIGW(win->wino, "focus-in-event" , focuscb, win);
	SIGW(win->wino, "focus-out-event", focusoutcb, win);

	if (!ctx) {
		ephemeral = g_key_file_get_boolean(conf, "boot", "ephemeral", NULL);
		gchar *data  = g_build_filename(g_get_user_data_dir() , fullname, NULL);
		gchar *cache = g_build_filename(g_get_user_cache_dir(), fullname, NULL);
		WebKitWebsiteDataManager * mgr = webkit_website_data_manager_new(
				"base-data-directory" , data,
				"base-cache-directory", cache,
				"is-ephemeral", ephemeral, NULL);
		g_free(data);
		g_free(cache);

		ctx = webkit_web_context_new_with_website_data_manager(mgr);

		//cookie  //have to be after ctx are made
		WebKitCookieManager *cookiemgr =
			webkit_website_data_manager_get_cookie_manager(mgr);

		if (!ephemeral)
		{
			//we assume cookies are conf
			gchar *cookiefile = path2conf("cookies");
			webkit_cookie_manager_set_persistent_storage(cookiemgr,
					cookiefile, WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
			g_free(cookiefile);
		}

		webkit_cookie_manager_set_accept_policy(cookiemgr,
				WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);

		shared = !g_key_file_get_boolean(conf, "boot", "multiwebprocs", NULL);
		if (!shared)
			webkit_web_context_set_process_model(ctx,
					WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);

		gchar **argv = g_key_file_get_string_list(
				conf, "boot", "extensionargs", NULL, NULL);
		gchar *args = g_strjoinv(";", argv);
		g_strfreev(argv);
		gchar *udata = g_strconcat(args,
				";", shared ? "s" : "m", fullname, NULL);
		g_free(args);

		webkit_web_context_set_web_extensions_initialization_user_data(
				ctx, g_variant_new_string(udata));
		g_free(udata);

#if DEBUG
		gchar *extdir = g_get_current_dir();
		webkit_web_context_set_web_extensions_directory(ctx, extdir);
		g_free(extdir);
#else
		webkit_web_context_set_web_extensions_directory(ctx, EXTENSION_DIR);
#endif

		SIG(ctx, "download-started", downloadcb, NULL);

		webkit_security_manager_register_uri_scheme_as_local(
				webkit_web_context_get_security_manager(ctx), APP);

		webkit_web_context_register_uri_scheme(
				ctx, APP, schemecb, NULL, NULL);

		if (g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL))
		{
			gchar *favdir =
				g_build_filename(g_get_user_cache_dir(), fullname, "favicon", NULL);
			webkit_web_context_set_favicon_database_directory(ctx, favdir);
			g_free(favdir);
		}

		if (confbool("ignoretlserr"))
			webkit_web_context_set_tls_errors_policy(ctx,
					WEBKIT_TLS_ERRORS_POLICY_IGNORE);
	}
	WebKitUserContentManager *cmgr = webkit_user_content_manager_new();
	win->kito = cbwin ?
		g_object_new(WEBKIT_TYPE_WEB_VIEW,
				"related-view", cbwin->kit, "user-content-manager", cmgr, NULL)
		:
		g_object_new(WEBKIT_TYPE_WEB_VIEW,
				"web-context", ctx, "user-content-manager", cmgr, NULL);

	g_object_set_data(win->kito, "win", win);

	//workaround. without get_inspector inspector doesen't work
	//and have to grab forcus;
	SIGW(webkit_web_view_get_inspector(win->kit),
			"detach", detachcb, win->kitw);

	win->set = webkit_settings_new();
	setprops(win, conf, DSET);
	webkit_web_view_set_settings(win->kit, win->set);
	g_object_unref(win->set);
	webkit_web_view_set_zoom_level(win->kit, confdouble("zoom"));

	GObject *o = win->kito;
	SIGW(o, "destroy"              , destroycb , win);
	SIGW(o, "web-process-crashed"  , crashcb   , win);
	SIGW(o, "notify::title"        , notifycb  , win);
	SIGW(o, "notify::uri"          , notifycb  , win);
	SIGW(o, "notify::estimated-load-progress", progcb, win);

	if (g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL))
		SIGW(o, "notify::favicon"      , favcb     , win);

	SIG( o, "key-press-event"      , keycb     , win);
	SIG( o, "key-release-event"    , keyrcb    , win);
	SIG( o, "mouse-target-changed" , targetcb  , win);
	SIG( o, "button-press-event"   , btncb     , win);
	SIG( o, "button-release-event" , btnrcb    , win);
	SIG( o, "enter-notify-event"   , entercb   , win);
	SIG( o, "motion-notify-event"  , motioncb  , win);
	SIG( o, "scroll-event"         , scrollcb  , win);

	SIG( o, "decide-policy"        , policycb  , win);
	SIGW(o, "create"               , createcb  , win);
	SIGW(o, "close"                , closecb   , win);
	SIGW(o, "script-dialog"        , sdialogcb , win);
	SIG( o, "load-changed"         , loadcb    , win);

	SIG( o, "context-menu"         , contextcb , win);

	//for entry
	SIGW(o, "focus-in-event"       , focusincb , win);

	win->findct = webkit_web_view_get_find_controller(win->kit);
	SIGW(win->findct, "failed-to-find-text", findfailedcb, win);
	SIGW(win->findct, "found-text"         , foundcb     , win);

	//entry
	win->entw = gtk_entry_new();
	SIG(win->ento, "key-press-event", entkeycb, win);
	//SIGW(win->ento, "focus-out-event", entoutcb, win);
	GtkEntryBuffer *buf = gtk_entry_get_buffer(win->ent);
	SIGW(buf, "inserted-text", textcb, win);
	SIGW(buf, "deleted-text" , textcb, win);

	//progress
	win->progw = gtk_progress_bar_new();
	//style
	GtkStyleContext *sctx = gtk_widget_get_style_context(win->progw);
	GtkCssProvider *cssp = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssp,
		"progressbar *{min-height: 0.9em;}", -1, NULL); //bigger
	gtk_style_context_add_provider(sctx, (GtkStyleProvider *)cssp,
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(cssp);

	//label
//	win->lblw = gtk_label_new("");
//	<span foreground='blue' weight='ultrabold' font='40'>Numbers</span>
//	gtk_label_set_use_markup(win->lbl, TRUE);

	//without overlay, show and hide of prog causes slow down
	GtkWidget  *olw = gtk_overlay_new();
	GtkOverlay *ol  = (GtkOverlay *)olw;

	GtkWidget *boxw  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkBox    *box   = (GtkBox *)boxw;
	GtkWidget *box2w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkBox    *box2  = (GtkBox *)box2w;

	gtk_box_pack_start(box , win->kitw , true , true, 0);
	gtk_box_pack_end(  box2, win->entw , false, true, 0);
	gtk_box_pack_end(  box2, win->progw, false, true, 0);
	gtk_widget_set_valign(box2w, GTK_ALIGN_END);

	gtk_overlay_add_overlay(ol, box2w);
	gtk_overlay_set_overlay_pass_through(ol, box2w, true);

	gtk_container_add(GTK_CONTAINER(ol), boxw);
	gtk_container_add(GTK_CONTAINER(win->win), olw);

	gtk_widget_show_all(win->winw);
	gtk_widget_hide(win->entw);
	gtk_widget_hide(win->progw);

	gtk_widget_grab_focus(win->kitw);

	gtk_window_present(
			back && LASTWIN ? LASTWIN->win : win->win);

	win->pageid = g_strdup_printf("%"G_GUINT64_FORMAT,
			webkit_web_view_get_page_id(win->kit));
	g_ptr_array_add(wins, win);

	if (!cbwin)
		_openuri(win, uri, caller);

	return win;
}


//@main
static void runline(const gchar *line, gchar *cdir, gchar *exarg)
{
	gchar **args = g_strsplit(line, ":", 3);

	gchar *arg = args[2];
	if (!*arg) arg = NULL;

	if (!strcmp(args[0], "0"))
		_run(LASTWIN, args[1], arg, cdir, exarg);
	else
	{
		Win *win = winbyid(args[0]);
		if (win)
			_run(win, args[1], arg, cdir, exarg);
	}

	g_strfreev(args);
}
void ipccb(const gchar *line)
{
	//m is from main
	if (*line != 'm') return runline(line, NULL, NULL);

	gchar **args = g_strsplit(line, ":", 4);
	int clen = atoi(args[1]);
	int elen = atoi(args[2]);
	gchar *cdir = g_strndup(args[3], clen);
	gchar *exarg = elen == 0 ? NULL : g_strndup(args[3] + clen, elen);

	runline(args[3] + clen + elen, cdir, exarg);

	g_free(cdir);
	g_free(exarg);
	g_strfreev(args);
}
int main(int argc, char **argv)
{
#if DEBUG
	g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
#endif

	if (argc == 2 && (
			!strcmp(argv[1], "-h") ||
			!strcmp(argv[1], "--help"))
	) {
		g_print(usage);
		exit(0);
	}

	if (argc >= 4)
		suffix = argv[1];
	const gchar *envsuf = g_getenv("SUFFIX") ?: "";
	if (!strcmp(suffix, "//")) suffix = g_strdup(envsuf);
	if (!strcmp(suffix, "/")) suffix = "";
	if (!strcmp(envsuf, "/")) envsuf = "";
	const gchar *winid =
		!strcmp(suffix,  envsuf) ? g_getenv("WINID") : NULL;
	if (!winid || !*winid) winid = "0";


	fullname = g_strconcat(APPNAME, suffix, NULL);

	gchar *exarg = "";
	if (argc > 4)
	{
		exarg = argv[4];
		argc = 4;
	}

	gchar *action = argc > 2 ? argv[argc - 2] : "new";
	gchar *uri    = argc > 1 ? argv[argc - 1] : NULL;

	if (!*action) action = "new";
	if (uri && !*uri) uri = NULL;
	if (argc == 2 && uri && g_file_test(uri, G_FILE_TEST_EXISTS))
		uri = g_strconcat("file://", uri, NULL);

	gchar *cwd = g_get_current_dir();
	gchar *sendstr = g_strdup_printf("m:%ld:%ld:%s%s%s:%s:%s",
			strlen(cwd), strlen(exarg), cwd, exarg, winid, action, uri ?: "");
	g_free(cwd);

	if (ipcsend("main", sendstr)) exit(0);
	g_free(sendstr);

	//start main
	logdir = g_build_filename(
			g_get_user_cache_dir(), fullname, "history", NULL);
	gtk_init(NULL, NULL);
	checkconf(false);

	ipcwatch("main");

	//icon
	GdkPixbuf *pix = gtk_icon_theme_load_icon(
		gtk_icon_theme_get_default(), APP, 128, 0, NULL);
	if (pix)
	{
		gtk_window_set_default_icon(pix);
		g_object_unref(pix);
	}

	wins = g_ptr_array_new();
	dlwins = g_ptr_array_new();
	histimgs = g_queue_new();

	if (run(NULL, action, uri))
		gtk_main();
	else
		exit(1);
	exit(0);
}
