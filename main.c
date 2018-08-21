/*
Copyright 2017-2018 jun7@hush.mail

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
#include <gdk/gdkx.h>

//flock
#include <sys/file.h>

#define LASTWIN (wins && wins->len ? (Win *)*wins->pdata : NULL)
#define URI(win) (webkit_web_view_get_uri(win->kit) ?: "")

#define gdkw(wd) gtk_widget_get_window(wd)

typedef enum {
	Mnormal    = 0,
	Minsert    = 1,
	Mhint      = 2,
	Mhintrange = 2 + 4,
	Mfind      = 512,
	Mopen      = 1024,
	Mopennew   = 2048,
	Mlist      = 4096,
	Mpointer   = 8192,
} Modes;

typedef struct _Spawn Spawn;

typedef struct _WP {
	union {
		GtkWindow *win;
		GtkWidget *winw;
		GObject   *wino;
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
	union {
		GtkLabel  *lbl;
		GtkWidget *lblw;
	};
	union {
		GtkEntry  *ent;
		GtkWidget *entw;
		GObject   *ento;
	};
	char   *pageid;
	WebKitFindController *findct;

	//mode
	Modes   lastmode;
	Modes   mode;
	bool    crashed;
	bool    userreq; //not used

	//conf
	char   *lasturiconf;
	char   *lastreset;
	char   *overset;

	//draw
	double  lastx;
	double  lasty;
	char   *msg;
	double  prog;
	double  progd;
	GdkRectangle progrect;
	guint   drawprogcb;
	GdkRGBA rgba;

	//hittestresult
	char   *link;
	char   *focusuri;
	bool    usefocus;
	char   *linklabel;
	char   *image;
	char   *media;
	bool    oneditable;

	//pointer
	double  lastdelta;
	guint   lastkey;
	double  px;
	double  py;
	guint   pbtn;
	guint   ppress;

	//entry
	GSList *undo;
	GSList *redo;
	char   *lastsearch;
	bool    infind;

	//winlist
	int     cursorx;
	int     cursory;
	bool    scrlf;
	int     scrlcur;
	double  scrlx;
	double  scrly;

	//history
	char   *histstr;
	guint   histcb;

	//hint
	char    com; //Coms
	//hint and spawn
	Spawn  *spawn;

	//misc
	bool    scheme;
	GTlsCertificateFlags tlserr;
	bool    fordl;
	guint   msgfunc;

	bool cancelcontext;
	bool cancelbtn1r;
	bool cancelmdlr;
} Win;

struct _Spawn {
	Win  *win;
	const char *action;
	char *cmd;
	char *path;
	bool once;
};

//@global
static char      *suffix = "";
static GPtrArray *wins;
static GPtrArray *dlwins;
static GQueue    *histimgs;
typedef struct {
	char   *buf;
	gsize   size;
	guint64 id;
} Img;
static char *lastmsg;

static char *mdpath;
static char *accelp;

static char *hists[]  = {"h1", "h2", "h3", "h4", "h5", "h6", "h7", "h8", "h9", NULL};
static int   histfnum = sizeof(hists) / sizeof(*hists) - 1;
static char *histdir;

static GtkAccelGroup *accelg;
static WebKitWebContext *ctx;
static bool ephemeral;

//for xembed
#include <gtk/gtkx.h>
static long plugto;

//shared code
static void _kitprops(bool set, GObject *obj, GKeyFile *kf, char *group);
#define MAINC
#include "general.c"

static char *usage =
	"usage: "APP" [[[suffix] action|\"\"] uri|arg|\"\"]\n"
	"\n"
	"  "APP" www.gnu.org\n"
	"  "APP" new www.gnu.org\n"
	"  "APP" / new www.gnu.org\n"
	"\n"
	"  suffix: Process ID.\n"
	"    It is added to all directories conf, cache and etc.\n"
	"    '/' is default. '//' means $SUFFIX.\n"
	"  action: Such as new(default), open, pagedown ...\n"
	"    Except 'new' and some, without a set of $SUFFIX and $WINID,\n"
	"    actions are sent to the window last focused\n"
	;

static char *mainmdstr =
"<!-- this is text/markdown -->\n"
"<meta charset=utf8>\n"
"<style>\n"
"body{overflow-y:scroll} /*workaround for the delaying of the context-menu*/\n"
"a{background:linear-gradient(to right top, #ddf, white, white, white);\n"
" color:#109; margin:1px; padding:2px; text-decoration:none; display:inline-block}\n"
"a:hover{text-decoration:underline}\n"
"img{height:1em; width:1em; margin:-.1em}\n"
"strong > code{font-size:1.4em}\n"
"</style>\n\n"
"###Specific Keys:\n"
"- **`e`** : Edit this page\n"
"- **`E`** : Edit main config file\n"
"- **`c`** : Open config directory\n"
"- **`m`** : Show this page\n"
"- **`M`** : Show **[history]("APP":history)**\n"
"- **`b`** : Add title and URI of a page opened to this page\n"
"\n"
"If **e,E,c** don't work, edit values '`"MIMEOPEN"`' of ~/.config/"DIRNAME"/main.conf<br>\n"
"or change mimeopen's database by running "
"'<code>mimeopen <i>file/directory</i></code>' in terminals.\n"
"\n"
"For other keys, see **[help]("APP":help)** assigned '**`:`**'.\n"
"Since "APP" is inspired by **[dwb](https://wiki.archlinux.org/index.php/dwb)**\n"
"and luakit, usage is similar to them.\n"
"\n---\n<!--\n"
"wyeb:i/iconname returns an icon image of current icon theme of gtk.\n"
"wyeb:f/uri returns a favicon of the uri loaded before.\n"
"wyeb:F converted to the wyeb:f with a parent tag's href.\n"
"-->\n"
"[![]("APP":i/"APP") "APP"](https://github.com/jun7/"APP")\n"
"[Wiki](https://github.com/jun7/"APP"/wiki)\n"
"[Adblock](https://github.com/jun7/"APP"adblock)\n"
"[![]("APP":F) Testing adblocker](http://simple-adblock.com/faq/testing-your-adblocker/)\n"
"[![]("APP":f/https://www.archlinux.org/) Arch Linux](https://www.archlinux.org/)\n"
;

//@misc
//util (indeipendent
static void addhash(char *str, guint *hash)
{
	if (*hash == 0) *hash = 5381;
	do *hash = *hash * 33 + *str;
	while (*++str);
}

//core
static bool isin(GPtrArray *ary, void *v)
{
	if (ary && v) for (int i = 0; i < ary->len; i++)
		if (v == ary->pdata[i]) return true;
	return false;
}
static Win *winbyid(const char *pageid)
{
	for (int i = 0; i < wins->len; i++)
		if (!strcmp(pageid, ((Win *)wins->pdata[i])->pageid))
			return wins->pdata[i];
	return NULL;
}
static void quitif()
{
	if (!wins->len && !dlwins->len)
		gtk_main_quit();
}
static void reloadlast()
{
	if (!LASTWIN) return;
	static gint64 last = 0;
	gint64 now = g_get_monotonic_time();
	if (now - last < 300000) return;
	last = now;
	webkit_web_view_reload(LASTWIN->kit);
}
static void alert(char *msg)
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

//history
static void append(char *path, const char *str)
{
	FILE *f = fopen(path, "a");
	if (f)
	{
		fprintf(f, "%s\n", str ?: "");
		fclose(f);
	}
	else
		alert(sfree(g_strdup_printf("fopen %s failed", path)));
}
static void freeimg(Img *img)
{
	g_free(img ? img->buf : NULL);
	g_free(img);
}
static void pushimg(Win *win, bool swap)
{
	int maxi = MAX(confint("histimgs"), 0);

	while (histimgs->length > 0 && histimgs->length >= maxi)
		freeimg(g_queue_pop_tail(histimgs));

	if (!maxi) return;

	if (swap)
		freeimg(g_queue_pop_head(histimgs));

	double ww = gtk_widget_get_allocated_width(win->kitw);
	double wh = gtk_widget_get_allocated_height(win->kitw);
	double scale = confint("histimgsize") / MAX(1, MAX(ww, wh));
	if (!(
		gtk_widget_get_visible(win->kitw) &&
		gtk_widget_is_drawable(win->kitw) &&
		ww * scale >= 1 &&
		wh * scale >= 1
	)) {
		g_queue_push_head(histimgs, NULL);
		return;
	}

	Img *img = g_new(Img, 1);
	static guint64 unique;
	img->id = ++unique;

	GdkPixbuf *pix =
		gdk_pixbuf_get_from_window(gdkw(win->kitw), 0, 0, ww, wh);
	GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
			pix, ww * scale, wh * scale, GDK_INTERP_BILINEAR);

	g_object_unref(pix);

	gdk_pixbuf_save_to_buffer(scaled,
			&img->buf, &img->size,
			"jpeg", NULL, "quality", "77", NULL);

	g_object_unref(scaled);

	g_queue_push_head(histimgs, img);
}
static char *histfile;
static char *lasthist;
static gboolean histcb(Win *win)
{
	if (!isin(wins, win)) return false;
	win->histcb = 0;

#define MAXSIZE 22222
	static int ci = -1;
	static int csize;
	if (!histfile || !g_file_test(histdir, G_FILE_TEST_EXISTS))
	{
		_mkdirif(histdir, false);

		ci = -1;
		csize = 0;
		for (char **file = hists; *file; file++)
		{
			ci++;
			GFA(histfile, g_build_filename(histdir, *file, NULL))
			struct stat info;
			if (stat(histfile, &info) == -1)
				break; //first time. errno == ENOENT
			csize = info.st_size;
			if (csize < MAXSIZE)
				break;
		}
	}

	char *str = win->histstr;
	win->histstr = NULL;
	if (lasthist && !strcmp(str + 18, lasthist + 18))
	{
		g_free(str);
		pushimg(win, true);
		return false;
	}
	pushimg(win, false);

	append(histfile, str);
	GFA(lasthist, str)

	csize += strlen(str) + 1;
	if (csize > MAXSIZE)
	{
		ci++;
		if (ci >= histfnum)
			ci = 0;

		GFA(histfile, g_build_filename(histdir, hists[ci], NULL))
		FILE *f = fopen(histfile, "w");
		fclose(f);
		csize = 0;
	}

	return false;
}
static bool updatehist(Win *win)
{
	const char *uri;
	if (ephemeral
	|| !*(uri = URI(win))
	|| g_str_has_prefix(uri, APP":")
	|| g_str_has_prefix(uri, "about:")) return false;

	char tstr[99];
	time_t t = time(NULL);
	strftime(tstr, sizeof(tstr), "%T/%d/%b/%y", localtime(&t));

	GFA(win->histstr, g_strdup_printf("%s %s %s", tstr, uri,
			webkit_web_view_get_title(win->kit) ?: ""))

	return true;
}
static void histperiod(Win *win)
{
	if (win->histstr)
	{
		if (win->histcb)
			g_source_remove(win->histcb);

		//if not cancel updated by load finish(fixhist)
		histcb(win);
	}
}
static void fixhist(Win *win)
{
	if (webkit_web_view_is_loading(win->kit) ||
			!updatehist(win)) return;

	if (win->histcb)
		g_source_remove(win->histcb);

	//drawing delays so for ss have to swap non finished draw
	win->histcb = g_timeout_add(200, (GSourceFunc)histcb, win);
}

static void removehistory()
{
	for (char **file = hists; *file; file++)
		remove(sfree(g_build_filename(histdir, *file, NULL)));

	GFA(histfile, NULL)
	GFA(lasthist, NULL)
}

//msg
static gboolean clearmsgcb(Win *win)
{
	if (!isin(wins, win)) return false;

	GFA(lastmsg, win->msg)
	win->msg = NULL;
	gtk_widget_queue_draw(win->kitw);
	win->msgfunc = 0;
	return false;
}
static void _showmsg(Win *win, char *msg)
{
	if (win->msgfunc) g_source_remove(win->msgfunc);
	GFA(win->msg, msg)
	win->msgfunc = !msg ? 0 :
		g_timeout_add(getsetint(win, "msgmsec"), (GSourceFunc)clearmsgcb, win);
	gtk_widget_queue_draw(win->kitw);
}
static void showmsg(Win *win, const char *msg)
{ _showmsg(win, g_strdup(msg)); }

//com
static void send(Win *win, Coms type, char *args)
{
	char *arg = g_strdup_printf("%s:%c:%s", win->pageid, type, args ?: "");

	static bool alerted;
	if(!ipcsend(shared ? "ext" : win->pageid, arg) &&
			!win->crashed && !alerted && type == Cstart)
	{
		alerted = true;
		alert("Failed to communicate with the Web Extension.\n"
				"Make sure ext.so is in "EXTENSION_DIR".");
	}
}
typedef struct {
	Win  *win;
	Coms  type;
	char *args;
} Send;
static gboolean senddelaycb(Send *s)
{
	if (isin(wins, s->win))
		send(s->win, s->type, s->args);
	g_free(s->args);
	g_free(s);
	return false;
}
static void senddelay(Win *win, Coms type, char *args) //args is eaten
{
	Send s = {win, type, args};
	g_timeout_add(40, (GSourceFunc)senddelaycb, g_memdup(&s, sizeof(Send)));
}

//event
static GdkDevice *pointer()
{ return gdk_seat_get_pointer(
		gdk_display_get_default_seat(gdk_display_get_default())); }
static GdkDevice *keyboard()
{ return gdk_seat_get_keyboard(
		gdk_display_get_default_seat(gdk_display_get_default())); }

static void *kitevent(Win *win, bool ispointer, GdkEventType type)
{
	GdkEvent    *e  = gdk_event_new(type);
	GdkEventAny *ea = (void *)e;

	ea->window = gdkw(win->kitw);
	g_object_ref(ea->window);
	gdk_event_set_device(e, ispointer ? pointer() : keyboard());
	return e;
}
static void putevent(void *e)
{
	gdk_event_put(e);
	gdk_event_free(e);
}
static void _putbtn(Win* win, GdkEventType type, guint btn, double x, double y)
{
	GdkEventButton *eb = kitevent(win, true, type);

	eb->send_event = false; //true destroys the mdl btn hack
	if (btn > 10)
	{
		btn -= 10;
		eb->state = GDK_BUTTON1_MASK;
	}
	eb->button = btn;
	eb->type   = type;
	eb->x      = x;
	eb->y      = y;

	putevent(eb);
}
static void putbtn(Win* win, GdkEventType type, guint btn)
{ _putbtn(win, type, btn, win->px, win->py); }

static gboolean delaymdlrcb(Win *win)
{
	if (isin(wins, win))
		putbtn(win, GDK_BUTTON_RELEASE, 2);
	return false;
}
static void makeclick(Win *win, guint btn)
{
	putbtn(win, GDK_BUTTON_PRESS, btn);
	if (btn == 2)
		g_timeout_add(40, (GSourceFunc)delaymdlrcb, win);
	else
		putbtn(win, GDK_BUTTON_RELEASE, btn);
}

//shared
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

	const char *label = webkit_hit_test_result_get_link_label(htr);
	if (!label)
		label = webkit_hit_test_result_get_link_title(htr);
	win->linklabel = label ? g_strdup(label): NULL;

	win->oneditable = webkit_hit_test_result_context_is_editable(htr);
}
static void undo(Win *win, GSList **undo, GSList **redo)
{
	if (!*undo && redo != undo) return;
	const char *text = gtk_entry_get_text(win->ent);

	if (*text && (!*redo || strcmp((*redo)->data, text)))
		*redo = g_slist_prepend(*redo, g_strdup(text));

	if (redo == undo) return;

	if (!strcmp((*undo)->data, text))
	{
		g_free((*undo)->data);
		*undo = g_slist_delete_link(*undo, *undo);
		if (!*undo) return;
	}

	gtk_entry_set_text(win->ent, (*undo)->data);
	gtk_editable_set_position((void *)win->ent, -1);

	if (*undo == win->redo) //redo
	{
		if (!strcmp((*undo)->data, (*redo)->data))
			g_free((*undo)->data);
		else
			*redo = g_slist_prepend(*redo, (*undo)->data);
		*undo = g_slist_delete_link(*undo, *undo);
	}
}
#define getent(win) gtk_entry_get_text(win->ent)
static void setent(Win *win, const char *str)
{
	undo(win, &win->undo, &win->undo);
	gtk_entry_set_text(win->ent, str);
}

//@@conf
static int threshold(Win *win)
{
	int ret = 8;
	g_object_get(gtk_widget_get_settings(win->winw),
			"gtk-dnd-drag-threshold", &ret, NULL);
	return ret;
}
static char *dldir(Win *win)
{
	return g_build_filename(
			g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD) ?:
				g_get_home_dir(),
			getset(win, "dlsubdir"),
			NULL);
}
static void colorf(Win *win, cairo_t *cr, double alpha)
{
	cairo_set_source_rgba(cr,
			win->rgba.red, win->rgba.green, win->rgba.blue, alpha);
}
static void colorb(Win *win, cairo_t *cr, double alpha)
{
	if (win->rgba.red + win->rgba.green + win->rgba.blue < 1.5)
		cairo_set_source_rgba(cr, 1, 1, 1, alpha);
	else
		cairo_set_source_rgba(cr, 0, 0, 0, alpha - .04);
}
//monitor
static GHashTable *monitored = NULL;
static void monitorcb(GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e,
		void (*func)(const char *))
{
	if (e != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
			e != G_FILE_MONITOR_EVENT_DELETED) return;

	//delete event's path is old and chenge event's path is new,
	//when renamed out new is useless, renamed in old is useless.
	char *path = g_file_get_path(f);
	if (g_hash_table_lookup(monitored, path))
		func(path);
	g_free(path);
}
static bool monitor(char *path, void (*func)(const char *))
{
	if (!monitored) monitored = g_hash_table_new(g_str_hash, g_str_equal);

	if (g_hash_table_lookup(monitored, path)) return false;
	g_hash_table_add(monitored, g_strdup(path));

	if (g_file_test(path, G_FILE_TEST_IS_SYMLINK))
	{ //this only works boot time though
		char buf[PATH_MAX + 1];
		char *rpath = realpath(path, buf);
		if (rpath)
			monitor(rpath, func);
	}

	GFile *gf = g_file_new_for_path(path);
	GFileMonitor *gm = g_file_monitor_file(
			gf, G_FILE_MONITOR_NONE, NULL, NULL);
	SIG(gm, "changed", monitorcb, func);

	g_object_unref(gf);
	return true;
}

void _kitprops(bool set, GObject *obj, GKeyFile *kf, char *group)
{
	//properties
	guint len;
	GParamSpec **list = g_object_class_list_properties(
			G_OBJECT_GET_CLASS(obj), &len);

	for (int i = 0; i < len; i++) {
		GParamSpec *s = list[i];
		const char *key = s->name;
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
				int v = g_key_file_get_integer(kf, group, key, NULL);
				if (g_value_get_uint(&gv) == v) continue;
				g_value_set_uint(&gv, v);
			}
			else
				g_key_file_set_integer(kf, group, key, g_value_get_uint(&gv));
			break;
		case G_TYPE_STRING:
			if (set) {
				char *v = sfree(g_key_file_get_string(kf, group, key, NULL));
				if (!strcmp(g_value_get_string(&gv), v)) continue;;
				g_value_set_string(&gv, v);
			} else
				g_key_file_set_string(kf, group, key, g_value_get_string(&gv));
			break;
		default:
			if (!strcmp(key, "hardware-acceleration-policy")) {
				if (set) {
					char *str = g_key_file_get_string(kf, group, key, NULL);

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

static void setcss(Win *win, char *namesstr); //declaration
static void resetconf(Win *win, int type)
{ //type: 0: uri, 1:force, 2:overset, 3:file
//	"reldomaindataonly", "removeheaders"
	char *checks[] = {"reldomaincutheads", "rmnoscripttag", NULL};
	guint hash = 0;
	char *lastcss = g_strdup(getset(win, "usercss"));

	if (type && LASTWIN == win)
		for (char **check = checks; *check; check++)
			addhash(getset(win, *check) ?: "", &hash);

	_resetconf(win, URI(win), type);
	if (type == 3)
		send(win, Cload, NULL);
	if (type >= 2)
		send(win, Coverset, win->overset);

	if (type && LASTWIN == win)
	{
		guint last = hash;
		hash = 0;
		for (char **check = checks; *check; check++)
			addhash(getset(win, *check) ?: "", &hash);
		if (last != hash)
			reloadlast();
	}

	if (getsetbool(win, "addressbar"))
		gtk_widget_show(win->lblw);
	else
		gtk_widget_hide(win->lblw);

	char *newcss = getset(win, "usercss");
	if (g_strcmp0(lastcss, newcss))
		setcss(win, newcss);

	gdk_rgba_parse(&win->rgba, getset(win, "msgcolor") ?: "");

	g_free(lastcss);
}

static void checkmd(const char *mp)
{
	if (mdpath && wins->len && g_file_test(mdpath, G_FILE_TEST_EXISTS))
		for (int i = 0; i < wins->len; i++)
	{
		Win *win = wins->pdata[i];
		if (g_str_has_prefix(URI(win), APP":main"))
			webkit_web_view_reload(win->kit);
	}
}
static void prepareif(
		char **path,
		char *name, char *initstr, void (*monitorcb)(const char *))
{
	bool first = !*path;
	if (first) *path = path2conf(name);

	if (g_file_test(*path, G_FILE_TEST_EXISTS))
		goto out;

	GFile *gf = g_file_new_for_path(*path);

	GFileOutputStream *o = g_file_create(
			gf, G_FILE_CREATE_PRIVATE, NULL, NULL);
	g_output_stream_write((GOutputStream *)o,
			initstr, strlen(initstr), NULL, NULL);
	g_object_unref(o);

	g_object_unref(gf);

out:
	if (first)
		monitor(*path, monitorcb);
}
static void preparemd()
{
	prepareif(&mdpath, "mainpage.md", mainmdstr, checkmd);
}

static bool wbreload = true;
static void checkwb(const char *mp)
{
	char *arg = wbreload ? "r" : "n";
	if (wins->len)
	{
		if (shared)
			send(LASTWIN, Cwhite, arg);
		else for (int i = 0; i < wins->len; i++)
			send(wins->pdata[i], Cwhite, arg);
	}
	wbreload = true;
}
static void preparewb()
{
	static char *wbpath;
	prepareif(&wbpath, "whiteblack.conf",
			"# First char is 'w':white list or 'b':black list.\n"
			"# Second and following chars are regular expressions.\n"
			"# Preferential order: bottom > top\n"
			"# Keys 'a' and 'A' on "APP" add blocked or loaded list to this file.\n"
			"\n"
			"w^https?://([a-z0-9]+\\.)*githubusercontent\\.com/\n"
			"\n"
			, checkwb);
}

static void clearaccels(gpointer p,
		const char *path, guint key, GdkModifierType mods, gboolean changed)
{
	gtk_accel_map_change_entry(path, 0, 0, true);
}
static bool cancelaccels = false;
static void checkaccels(const char *mp)
{
	if (!cancelaccels && accelp)
	{
		gtk_accel_map_foreach(NULL, clearaccels);
		if (g_file_test(accelp, G_FILE_TEST_EXISTS))
			gtk_accel_map_load(accelp);
	}
	cancelaccels = false;
}

static void checkcss(const char *mp)
{
	if (!wins) return;
	for (int i = 0; i < wins->len; i++)
	{
		Win *lw = wins->pdata[i];
		char *us = getset(lw, "usercss");
		if (!us) continue;

		bool changed = false;
		char **names = g_strsplit(us, ";", -1);
		for (char **name = names; *name; name++)
			if ((changed = !strcmp(mp, sfree(path2conf(*name))))) break;
		g_strfreev(names);

		if (changed)
			setcss(lw, us);
	}
}
void setcss(Win *win, char *namesstr)
{
	char **names = g_strsplit(namesstr ?: "", ";", -1);

	WebKitUserContentManager *cmgr =
		webkit_web_view_get_user_content_manager(win->kit);
	webkit_user_content_manager_remove_all_style_sheets(cmgr);

	for (char **name = names; *name; name++)
	{
		char *path = path2conf(*name);
		monitor(path, checkcss); //even not exists

		char *str;
		if (g_file_test(path, G_FILE_TEST_EXISTS)
				&& g_file_get_contents(path, &str, NULL, NULL))
		{
			WebKitUserStyleSheet *css =
				webkit_user_style_sheet_new(str,
						WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
						WEBKIT_USER_STYLE_LEVEL_USER,
						NULL, NULL);

			webkit_user_content_manager_add_style_sheet(cmgr, css);
			g_free(str);
		}
		g_free(path);
	}
	g_strfreev(names);
}

static void checkconf(const char *mp)
{
	if (!confpath)
	{
		confpath = path2conf("main.conf");
		monitor(confpath, checkconf);
	}

	if (!g_file_test(confpath, G_FILE_TEST_EXISTS))
	{
		if (mp) return;
		if (!conf)
			initconf(NULL);

		mkdirif(confpath);
		g_key_file_save_to_file(conf, confpath, NULL);
		return;
	}
	else if (!mp && conf) return; //from focuscb

	GKeyFile *new = g_key_file_new();
	GError *err = NULL;
	g_key_file_load_from_file(new, confpath, G_KEY_FILE_KEEP_COMMENTS, &err);
	if (err)
	{
		alert(err->message);
		g_error_free(err);
		if (!conf)
			initconf(NULL);
		return;
	}

	initconf(new);

	if (ctx)
		webkit_web_context_set_tls_errors_policy(ctx,
				confbool("ignoretlserr") ?
				WEBKIT_TLS_ERRORS_POLICY_IGNORE :
				WEBKIT_TLS_ERRORS_POLICY_FAIL);

	if (wins)
		for (int i = 0; i < wins->len; i++)
			resetconf(wins->pdata[i], shared && i ? 1 : 3);
}


//@context
static void settitle(Win *win, const char *pstr)
{
	if (!pstr && win->crashed)
		pstr = "!! Web Process Crashed !!";

	bool bar = getsetbool(win, "addressbar");
	const char *wtitle = webkit_web_view_get_title(win->kit) ?: "";
	char *title = pstr && !bar ? NULL : g_strconcat(
		win->tlserr ? "!TLS " : "",
		suffix            , *suffix      ? "| " : "",
		win->overset ?: "", win->overset ? "| " : "",
		wtitle, bar ? "" : " - ", bar && *wtitle ? NULL : URI(win), NULL);

	if (bar)
	{
		gtk_window_set_title(win->win, *title ? title : URI(win));
		gtk_label_set_text(win->lbl, pstr ?: URI(win));
	}
	else
		gtk_window_set_title(win->win, pstr ?: title);
	g_free(title);
}
static void enticon(Win *win, const char *name); //declaration
static void pmove(Win *win, guint key); //declaration
static bool winlist(Win *win, guint type, cairo_t *cr); //declaration
static void _modechanged(Win *win)
{
	Modes last = win->lastmode;
	win->lastmode = win->mode;

	switch (last) {
	case Minsert:
		break;

	case Mfind:
	case Mopen:
	case Mopennew:
		gtk_widget_hide(win->entw);
		gtk_widget_grab_focus(win->kitw);
		break;

	case Mlist:
		gtk_widget_queue_draw(win->kitw);
		gdk_window_set_cursor(gdkw(win->winw), NULL);
		break;

	case Mpointer:
		if (win->mode != Mhint) win->pbtn = 0;
		gtk_widget_queue_draw(win->kitw);
		break;

	case Mhint:
		if (win->mode != Mpointer) win->pbtn = 0;
	case Mhintrange:
		send(win, Crm, NULL);
		break;

	case Mnormal:
		gtk_window_remove_accel_group(win->win, accelg);
		break;
	}

	//into
	switch (win->mode) {
	case Minsert:
		break;

	case Mfind:
		win->infind = false;
		if (win->crashed)
		{
			win->mode = Mnormal;
			break;
		}

	case Mopen:
	case Mopennew:
		enticon(win, NULL);

		if (win->mode != Mfind)
		{
			if (g_strcmp0(getset(NULL, "search"), getset(win, "search")))
				enticon(win, "system-search");
			else if (!getset(NULL, "search"))
				showmsg(win, "No search settings");
		}

		gtk_widget_show(win->entw);
		gtk_widget_grab_focus(win->entw);
		undo(win, &win->undo, &win->undo);
		break;

	case Mlist:
		winlist(win, 2, NULL);
		gtk_widget_queue_draw(win->kitw);
		break;

	case Mpointer:
		pmove(win, 0);
		break;

	case Mhint:
	case Mhintrange:
		if (win->crashed)
			win->mode = Mnormal;
		else
			send(win, win->com, sfree(g_strdup_printf("%c",
					win->pbtn > 1 || getsetbool(win, "hackedhint4js") ?
					'y' : 'n')));
		break;

	case Mnormal:
		gtk_window_add_accel_group(win->win, accelg);
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
		if (win->link) goto normal;
		settitle(win, sfree(g_strdup_printf("-- POINTER MODE %d --", win->pbtn)));
		break;

	case Mhintrange:
		settitle(win, "-- RANGE MODE --");
		break;

	case Mhint:
	case Mopen:
	case Mopennew:
	case Mfind:
		settitle(win, NULL);
		break;
	}

	//normal mode
	if (win->mode != Mnormal) return;
normal:

	if (win->focusuri || win->link)
	{
		bool f = (win->usefocus && win->focusuri) || !win->link;
		settitle(win, sfree(g_strconcat(f ? "Focus" : "Link",
				": ", f ? win->focusuri : win->link, NULL)));
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
static bool run(Win *win, char* action, const char *arg); //declaration

static int formaturi(char **uri, char *key, const char *arg, char *spare)
{
	int checklen = 1;
	char *format;

	if      ((format = g_key_file_get_string(conf, "template", key, NULL))) ;
	else if ((format = g_key_file_get_string(conf, "raw"     , key, NULL))) ; //backward
	else if ((format = g_key_file_get_string(conf, "search"  , key, NULL) ?:
				g_strdup(spare)))
	{
		checklen = strlen(arg) ?: 1; //only search else are 1 even ""
		arg = sfree(g_uri_escape_string(arg, NULL, false));
	}
	else return 0;

	*uri = g_strdup_printf(format, arg);
	g_free(format);
	return checklen;
}
static void _openuri(Win *win, const char *str, Win *caller)
{
	win->userreq = true;
	if (!str || !*str) str = APP":main";

	if (
		g_str_has_prefix(str, "http:") ||
		g_str_has_prefix(str, "https:") ||
		g_str_has_prefix(str, APP":") ||
		g_str_has_prefix(str, "file:") ||
		g_str_has_prefix(str, "data:") ||
		g_str_has_prefix(str, "about:")
	) {
		webkit_web_view_load_uri(win->kit, str);
		return;
	}

	if (str != getent(win))
		setent(win, str ?: "");

	if (g_str_has_prefix(str, "javascript:")) {
		webkit_web_view_run_javascript(win->kit, str + 11, NULL, NULL, NULL);
		return;
	}

	char *uri = NULL;
	int checklen = 0;
	char **stra = g_strsplit(str, " ", 2);

	if (*stra && stra[1] &&
			(checklen = formaturi(&uri, stra[0], stra[1], NULL)))
	{
		GFA(win->lastsearch, g_strdup(stra[1]))
		goto out;
	}

	static regex_t *url;
	if (!url)
	{
		url = g_new(regex_t, 1);
		regcomp(url,
				"^([a-zA-Z0-9-]{2,256}\\.)+[a-z]{2,6}(/.*)?$",
				REG_EXTENDED | REG_NOSUB);
	}

	char *dsearch;
	if (regexec(url, str, 0, NULL, 0) == 0)
		uri = g_strdup_printf("http://%s", str);
	else if ((dsearch = getset(caller ?: win, "search")))
	{
		checklen = formaturi(&uri, dsearch, str, dsearch);
		GFA(win->lastsearch, g_strdup(str))
	}

	if (!uri) uri = g_strdup(str);
out:
	g_strfreev(stra);

	int max;
	if (checklen > 1 && (max = getsetint(win, "searchstrmax")) && checklen > max)
		_showmsg(win, g_strdup_printf("Input Len(%d) > searchstrmax=%d",
					checklen, max));
	else
	{
		webkit_web_view_load_uri(win->kit, uri);

		SoupURI *suri = soup_uri_new(uri);
		if (suri)
			soup_uri_free(suri);
		else
			_showmsg(win, g_strdup_printf("Invalid URI: %s", uri));
	}

	g_free(uri);
}
static void openuri(Win *win, const char *str)
{ _openuri(win, str, NULL); }

static Spawn *spawnp(Win *win,
		const char *action, const char *cmd, const char *path, bool once)
{
	Spawn ret = {win, action, g_strdup(cmd), g_strdup(path), once};
	return g_memdup(&ret, sizeof(Spawn));
}
static void spawnfree(Spawn* s, bool force)
{
	if (!s || (!s->once && !force)) return;
	g_free(s->cmd);
	g_free(s->path);
	g_free(s);
}
static void envspawn(Spawn *p,
		bool iscallback, char *result, char *piped, gsize plen)
{
	Win *win = p->win;
	if (!isin(wins, win)) goto out;

	char **argv;
	if (p->cmd)
	{
		GError *err = NULL;
		if (*p->action == 's' && 'h' == p->action[1])
		{
			argv = g_new0(char*, 4);
			argv[0] = g_strdup("sh");
			argv[1] = g_strdup("-c");
			argv[2] = g_strdup(p->cmd);
		}
		else if (!g_shell_parse_argv(p->cmd, NULL, &argv, &err))
		{
			showmsg(win, err->message);
			g_error_free(err);
			goto out;
		}
	} else {
		argv = g_new0(char*, 2);
		argv[0] = g_strdup(p->path);
	}

	if (getsetbool(win, "spawnmsg"))
		_showmsg(win, g_strdup_printf("spawn: %s", p->cmd ?: p->path));

	char *dir = p->cmd ?
		(p->path ? g_strdup(p->path) : path2conf("menu"))
		: g_path_get_dirname(p->path);

	char **envp = g_get_environ();
	envp = g_environ_setenv(envp, "ISCALLBACK",
			iscallback ? "1" : "0", true);
	envp = g_environ_setenv(envp, "RESULT", result ?: "", true);
	//for backward compatibility
	envp = g_environ_setenv(envp, "JSRESULT", result ?: "", true);

	char buf[9];
	snprintf(buf, 9, "%d", wins->len);
	envp = g_environ_setenv(envp, "WINSLEN", buf, true);
	envp = g_environ_setenv(envp, "SUFFIX" , *suffix ? suffix : "/", true);
	envp = g_environ_setenv(envp, "WINID"  , win->pageid, true);
	envp = g_environ_setenv(envp, "CURRENTSET", win->overset ?: "", true);
	envp = g_environ_setenv(envp, "URI"    , URI(win), true);
	envp = g_environ_setenv(envp, "LINK_OR_URI", URI(win), true);
	envp = g_environ_setenv(envp, "DLDIR"  , sfree(dldir(win)), true);
	envp = g_environ_setenv(envp, "CONFDIR", sfree(path2conf(NULL)), true);
	envp = g_environ_setenv(envp, "CANBACK",
			webkit_web_view_can_go_back(   win->kit) ? "1" : "0", true);
	envp = g_environ_setenv(envp, "CANFORWARD",
			webkit_web_view_can_go_forward(win->kit) ? "1" : "0", true);
	int WINX, WINY, WIDTH, HEIGHT;
	gtk_window_get_position(win->win, &WINX, &WINY);
	gtk_window_get_size(win->win, &WIDTH, &HEIGHT);
#define Z(x) \
	snprintf(buf, 9, "%d", x); \
	envp = g_environ_setenv(envp, #x, buf, true);

	Z(WINX) Z(WINY) Z(WIDTH) Z(HEIGHT)
#undef Z

	const char *title = webkit_web_view_get_title(win->kit);
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

	char *cbtext;
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

	int input;
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
				p->cmd ? G_SPAWN_SEARCH_PATH : G_SPAWN_DEFAULT,
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
		close(input);
	}

	g_spawn_close_pid(child_pid);

	g_strfreev(envp);
	g_strfreev(argv);
	g_free(dir);

out:
	spawnfree(p, false);
}

static void scroll(Win *win, int x, int y)
{
	GdkEventScroll *es = kitevent(win, false, GDK_SCROLL);

	es->send_event = false; //for multiplescroll
	//es->time   = GDK_CURRENT_TIME;
	es->direction =
		x < 0 ? GDK_SCROLL_LEFT :
		x > 0 ? GDK_SCROLL_RIGHT :
		y < 0 ? GDK_SCROLL_UP :
		        GDK_SCROLL_DOWN;

	es->delta_x = x;
	es->delta_y = y;
	es->x = win->px;
	es->y = win->py;

	putevent(es);
}
static void motion(Win *win, double x, double y)
{
	GdkEventMotion *em = kitevent(win, true, GDK_MOTION_NOTIFY);
	em->x = x;
	em->y = y;
	if (win->ppress)
		em->state = win->pbtn == 3 ? GDK_BUTTON3_MASK :
		            win->pbtn == 2 ? GDK_BUTTON2_MASK : GDK_BUTTON1_MASK;

	gtk_widget_event(win->kitw, (void *)em);
	gdk_event_free((void *)em);
}
void pmove(Win *win, guint key)
{
	//GDK_KEY_Down
	double ww = gtk_widget_get_allocated_width(win->kitw);
	double wh = gtk_widget_get_allocated_height(win->kitw);
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
		(lkey == GDK_KEY_Left && key  == GDK_KEY_Right)
	)
		win->lastdelta /= 2;

	guint32 unit = MAX(10, webkit_settings_get_default_font_size(win->set)) / 3;
	if (win->lastdelta < unit) win->lastdelta = unit;
	double d = win->lastdelta;
	if (key == GDK_KEY_Up   ) win->py -= d;
	if (key == GDK_KEY_Down ) win->py += d;
	if (key == GDK_KEY_Left ) win->px -= d;
	if (key == GDK_KEY_Right) win->px += d;

	win->px = CLAMP(win->px, 0, ww);
	win->py = CLAMP(win->py, 0, wh);

	win->lastdelta *= .9;
	win->lastkey = key;

	motion(win, win->px, win->py);

	gtk_widget_queue_draw(win->kitw);
}
static void altcur(Win *win, double x, double y)
{
	static GdkCursor *cur;
	if (!cur) cur = gdk_cursor_new_for_display(
			gdk_display_get_default(), GDK_CENTER_PTR);
	if (x + y == 0)
		gdk_window_set_cursor(gdkw(win->kitw), cur);
	else if (gdk_window_get_cursor(gdkw(win->kitw)) == cur)
		motion(win, x, y); //clear
}
static void putkey(Win *win, guint key)
{
	GdkEventKey *ek = kitevent(win, false, GDK_KEY_PRESS);
	ek->send_event = true;
//	ek->time   = GDK_CURRENT_TIME;
	ek->keyval = key;
//	ek->state  = ek->state & ~GDK_MODIFIER_MASK;
	putevent(ek);
}

static void command(Win *win, const char *cmd, const char *arg)
{
	cmd = sfree(g_strdup_printf(cmd, arg));
	_showmsg(win, g_strdup_printf("Run '%s'", cmd));
	GError *err = NULL;
	if (!g_spawn_command_line_async(cmd, &err))
		alert(err->message), g_error_free(err);
}

static void openeditor(Win *win, const char *path, char *editor)
{
	command(win, editor ?: getset(win, "editor") ?: MIMEOPEN, path);
}
static void openconf(Win *win, bool shift)
{
	char *path;
	char *editor = NULL;

	const char *uri = URI(win);
	if (g_str_has_prefix(uri, APP":main"))
	{
		if (shift)
			path = confpath;
		else {
			path = mdpath;
			editor = getset(win, "mdeditor");
		}
	}
	else if (!shift && g_str_has_prefix(uri, APP":"))
	{
		showmsg(win, "No config");
		return;
	} else {
		path = confpath;
		if (!shift)
		{
			char *name = sfree(g_strdup_printf("uri:^%s", sfree(regesc(uri))));
			if (!g_key_file_has_group(conf, name))
				append(path, sfree(g_strdup_printf("\n[%s]", name)));
		}
	}

	openeditor(win, path, editor);
}

static void present(Win *win)
{
	gtk_window_present(win->win);
	if (confbool("pointerwarp") &&
			keyboard() == gtk_get_current_event_device())
	{
		int px, py;
		gdk_device_get_position(pointer(), NULL, &px, &py);
		GdkRectangle rect;
		gdk_window_get_frame_extents(gdkw(win->winw), &rect);

		gdk_device_warp(pointer(),
				gdk_display_get_default_screen(
					gdk_window_get_display(gdkw(win->winw))),
				CLAMP(px, rect.x, rect.x + rect.width  - 1),
				CLAMP(py, rect.y, rect.y + rect.height - 1));
	}
}
static int inwins(Win *win, GSList **list, bool onlylen)
{
	guint len = 0;
	GdkWindow  *dw = gdkw(win->winw);
	GdkDisplay *dd = gdk_window_get_display(dw);
	for (int i = 0; i < wins->len; i++)
	{
		Win *lw = wins->pdata[i];
		if (lw == win) continue;

		GdkWindow *ldw = gdkw(lw->winw);

		if (gdk_window_get_state(ldw) & GDK_WINDOW_STATE_ICONIFIED)
			continue;

#ifdef GDK_WINDOWING_X11
		if (GDK_IS_X11_DISPLAY(dd) &&
			(gdk_x11_window_get_desktop(dw) !=
					gdk_x11_window_get_desktop(ldw)))
			continue;
#endif

		len++;
		if (!onlylen)
			*list = g_slist_append(*list, lw);
	}
	return len;
}
static void nextwin(Win *win, bool next)
{
	GSList *list = NULL;

	if (!inwins(win, &list, false))
		return showmsg(win, "No other windows");

	Win *nextwin = NULL;
	if (next)
	{
		g_ptr_array_remove(wins, win);
		g_ptr_array_add(wins, win);
		present(nextwin = list->data); //present first to keep focus on xfce
		if (!plugto)
			gdk_window_lower(gdkw(win->winw));
	}
	else
		present(nextwin = g_slist_last(list)->data);

	nextwin->lastx = win->lastx;
	nextwin->lasty = win->lasty;
	g_slist_free(list);
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
		return false;

	double w = gtk_widget_get_allocated_width(win->kitw);
	double h = gtk_widget_get_allocated_height(win->kitw);

	double yrate = h / w;

	double yunitd = sqrt(len * yrate);
	double xunitd = yunitd / yrate;
	int yunit = yunitd;
	int xunit = xunitd;

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
		if (win->scrlf)
			type = 0;
		win->scrlf = false;
		if (win->cursorx > 0 && win->cursory > 0)
			break;
	case 2:
		win->scrlf = false;
		win->cursorx = xunit / 2.0 - .5;
		win->cursory = yunit / 2.0 - .5;
		win->cursorx++;
		win->cursory++;
		if (type == 2)
			return true;
	}

	switch (type) {
	case GDK_KEY_Page_Up:
		if ((win->scrlf || win->scrlcur == 0) &&
				--win->scrlcur < 1) win->scrlcur = len;
		win->scrlf = true;
		return true;
	case GDK_KEY_Page_Down:
		if ((win->scrlf || win->scrlcur == 0) &&
				++win->scrlcur > len) win->scrlcur = 1;
		win->scrlf = true;
		return true;

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

	double uw = w / xunit;
	double uh = h / yunit;

	if (cr)
	{
		cairo_set_source_rgba(cr, .4, .4, .4, win->scrlf ? 1 : .6);
		cairo_paint(cr);
	}

	double px, py;
	gdk_window_get_device_position_double(
			gdkw(win->kitw), pointer(), &px, &py, NULL);

	int count = 0;
	bool ret = false;
	GSList *crnt = actvs;
	for (int yi = 0; yi < yunit; yi++) for (int xi = 0; xi < xunit; xi++)
	{
		if (!crnt) break;
		Win *lw = crnt->data;
		crnt = crnt->next;
		count++;
		if (win->scrlf)
		{
			if (count != win->scrlcur) continue;
			win->cursorx = xi + 1;
			win->cursory = yi + 1;
		}

		double lww = gtk_widget_get_allocated_width(lw->kitw);
		double lwh = gtk_widget_get_allocated_height(lw->kitw);

		if (lww == 0 || lwh == 0) lww = lwh = 9;

		double scale = MIN(uw / lww, uh / lwh) * (1.0 - 1.0/(yunit * xunit + 1));
		double tw = lww * scale;
		double th = lwh * scale;
		//pos is double makes blur
		int tx = xi * uw + (uw - tw) / 2;
		int ty = yi * uh + (uh - th) / 2;
		double tr = tx + tw;
		double tb = ty + th;

		if (win->scrlf)
		{
			scale = 1;
			tx = ty = 2;
			tr = w - 2;
			tb = h - 2;
		}

		bool pin = win->cursorx + win->cursory == 0 ?
			px > tx && px < tr && py > ty && py < tb :
			xi + 1 == win->cursorx && yi + 1 == win->cursory;
		ret = ret || pin;

		if (!pin)
		{
			if (!cr) continue;
		}
		else if (type == 1) //present
			present(lw);
		else if (type == 3) //close
		{
			run(lw, "quit", NULL);
			if (len > 1)
			{
				if (count == len)
					win->scrlcur = len - 1;
				gtk_widget_queue_draw(win->kitw);
			}
			else
				tonormal(win);
		} else {
			settitle(win, sfree(g_strdup_printf("LIST| %s",
					webkit_web_view_get_title(lw->kit))));

			win->cursorx = xi + 1;
			win->cursory = yi + 1;
			win->scrlcur = count;
		}

		if (!cr) goto out;

		cairo_reset_clip(cr);
		cairo_new_sub_path(cr);
		double r = 4 + th / 66.0;
		cairo_arc(cr, tr - r, ty + r, r, M_PI / -2, 0         );
		cairo_arc(cr, tr - r, tb - r, r, 0        , M_PI / 2  );
		cairo_arc(cr, tx + r, tb - r, r, M_PI / 2 , M_PI      );
		cairo_arc(cr, tx + r, ty + r, r, M_PI     , M_PI * 1.5);
		cairo_close_path(cr);
		if (pin)
		{
			colorf(lw, cr, 1);
			cairo_set_line_width(cr, 6.0);
			cairo_stroke_preserve(cr);
		}
		cairo_clip(cr);

		if (!plugto
				&& gtk_widget_get_visible(lw->kitw)
				&& gtk_widget_is_drawable(lw->kitw)
		) {
			cairo_scale(cr, scale, scale);

			GdkPixbuf *pix =
				gdk_pixbuf_get_from_window(gdkw(lw->kitw), 0, 0, lww, lwh);
			cairo_surface_t *suf =
				gdk_cairo_surface_create_from_pixbuf(pix, 0, NULL);

			if (win->scrlf)
			{
				tx = MAX(tx, (w - lww) / 2);
				ty = MAX(ty, (h - lwh) / 2);
			}

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
	}
out:

	g_slist_free(actvs);

	if (!ret)
		update(win);

	return ret;
}

static void addlink(Win *win, const char *title, const char *uri)
{
	char *str = NULL;
	preparemd();
	if (uri)
	{
		char *escttl;
		if (title && *title)
			escttl = _escape(sfree(g_markup_escape_text(title, -1)), "[]");
		else
			escttl = g_strdup(uri);

		char *fav = g_strdup_printf(APP":f/%s",
				sfree(g_uri_escape_string(uri, "/:=&", true)));

		char *items = getset(win, "linkdata") ?: "tu";
		int i = 0;
		const char *as[9] = {""};
		for (char *c = items; *c && i < 9; c++)
			as[i++] =
				*c == 't' ? escttl:
				*c == 'u' ? uri:
				*c == 'f' ? fav:
				"";
		str = g_strdup_printf(getset(win, "linkformat"),
				as[0], as[1], as[2], as[3], as[4], as[5], as[6], as[7], as[8]);

		g_free(fav);
		g_free(escttl);
	}
	append(mdpath, str);
	g_free(str);

	showmsg(win, "Added");
}

#define findtxt(win) webkit_find_controller_get_search_text(win->findct)
static void find(Win *win, const char *arg, bool next, bool insensitive)
{
	const char *u = insensitive ? "" : arg;
	do if (g_ascii_isupper(*u)) break; while (*++u);
	webkit_find_controller_search(win->findct, arg
		, (*u   ? 0 : WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE)
		| (next ? 0 : WEBKIT_FIND_OPTIONS_BACKWARDS)
		| WEBKIT_FIND_OPTIONS_WRAP_AROUND
		, G_MAXUINT);
	win->infind = true;
	GFA(win->lastsearch, NULL)
}
static void findnext(Win *win, bool next)
{
	if (findtxt(win))
	{
		if (next)
			webkit_find_controller_search_next(win->findct);
		else
			webkit_find_controller_search_previous(win->findct);
	}
	else if (win->lastsearch)
	{
		setent(win, win->lastsearch);
		find(win, win->lastsearch, next, true);
	}
	else
	{
		showmsg(win, "No search words");
		return;
	}
	senddelay(win, Cfocus, NULL);
}

static void jscb(GObject *po, GAsyncResult *pres, gpointer p)
{
	GError *err = NULL;
	WebKitJavascriptResult *res =
		webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(po), pres, &err);

	char *resstr = NULL;
	if (res)
	{
#if V22
		resstr = jsc_value_to_string(
				webkit_javascript_result_get_js_value(res));
#else
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
#endif
		webkit_javascript_result_unref(res);
	}
	else
	{
		resstr = g_strdup(err->message);
		g_error_free(err);
	}

	envspawn(p, true, resstr, NULL, 0);
	g_free(resstr);
}
static void resourcecb(GObject *srco, GAsyncResult *res, gpointer p)
{
	gsize len;
	guchar *data = webkit_web_resource_get_data_finish(
			(WebKitWebResource *)srco, res, &len, NULL);

	envspawn(p, true, NULL, (char *)data, len);
	g_free(data);
}
#if WEBKIT_CHECK_VERSION(2, 20, 0)
static void cookiescb(GObject *cm, GAsyncResult *res, gpointer p)
{
	char *header = NULL;
	GList *gl = webkit_cookie_manager_get_cookies_finish(
				(WebKitCookieManager *)cm, res, NULL);
	if (gl)
	{
		header = soup_cookies_to_cookie_header((GSList *)gl);
		g_list_free_full(gl, (GDestroyNotify)soup_cookie_free);
	}

	envspawn(p, true, header ?: "", NULL, 0);
	g_free(header);
}
#endif

//textlink
static char *tlpath;
static Win  *tlwin;
static void textlinkcheck(const char *mp)
{
	if (!isin(wins, tlwin)) return;
	send(tlwin, Ctlset, tlpath);
}
static void textlinkon(Win *win)
{
	run(win, "openeditor", tlpath);
	tlwin = win;
}
static void textlinktry(Win *win)
{
	tlwin = NULL;
	if (!tlpath)
	{
		tlpath = g_build_filename(
			g_get_user_data_dir(), fullname, "textlink.txt", NULL);
		monitor(tlpath, textlinkcheck);
	}
	send(win, Ctlget, tlpath);
}


//@actions
typedef struct {
	char *name;
	guint key;
	guint mask;
	char *desc;
} Keybind;
static Keybind dkeys[]= {
//every mode
	{"tonormal"      , GDK_KEY_Escape, 0, "To Normal Mode"},
	{"tonormal"      , '[', GDK_CONTROL_MASK},

//normal
	{"toinsert"      , 'i', 0},
	{"toinsertinput" , 'I', 0, "To Insert Mode with focus of first input"},
	{"topointer"     , 'p', 0, "pp resets damping. Esc clears pos. Press enter/space makes btn press"},
	{"topointermdl"  , 'P', 0, "Makes middle click"},
	{"topointerright", 'p', GDK_CONTROL_MASK, "right click"},

	{"tohint"        , 'f', 0},
	{"tohintnew"     , 'F', 0},
	{"tohintback"    , 't', 0},
	{"tohintdl"      , 'd', 0, "dl is Download"},
	{"tohintbookmark", 'T', 0},
	{"tohintrangenew", 'r', GDK_CONTROL_MASK, "Open new windows"},

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
	{"halfdown"      , 'd', GDK_CONTROL_MASK},
	{"halfup"        , 'u', GDK_CONTROL_MASK},

	{"top"           , 'g', 0},
	{"bottom"        , 'G', 0},
	{"zoomin"        , '+', 0},
	{"zoomout"       , '-', 0},
	{"zoomreset"     , '=', 0},

	//tab
	{"nextwin"       , 'J', 0},
	{"prevwin"       , 'K', 0},
	{"quitnext"      , 'x', 0, "Raise next win and quit current win"},
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
	{"edituri"       , 'O', 0, "Edit arg or focused link or current page's URI"},
	{"editurinew"    , 'W', 0},

//	{"showsource"    , 'S', 0}, //not good
	{"showhelp"      , ':', 0},
	{"showhistory"   , 'M', 0},
	{"showhistoryall", 'm', GDK_CONTROL_MASK},
	{"showmainpage"  , 'm', 0},

	{"clearallwebsitedata", 'C', GDK_CONTROL_MASK},
	{"edit"          , 'e', 0, "Edit current uri conf or mainpage"},
	{"editconf"      , 'E', 0},
	{"openconfigdir" , 'c', 0},

	{"setv"          , 'v', 0, "Use the 'set:v' group"},
	{"setscript"     , 's', GDK_CONTROL_MASK, "Use the 'set:script' group"},
	{"setimage"      , 'i', GDK_CONTROL_MASK, "set:image"},
	{"unset"         , 'u', 0},

	{"addwhitelist"  , 'a', 0, "Add URIs blocked to whiteblack.conf as white list"},
	{"addblacklist"  , 'A', 0, "URIs loaded"},

//insert
	{"textlink"      , 'e', GDK_CONTROL_MASK, "For text elements in insert mode"},

//nokey
	{"set"           , 0, 0, "Use 'set:' + arg group of main.conf. This toggles"},
	{"set2"          , 0, 0, "Not toggle"},
	{"setstack"      , 0, 0,
		"arg == NULL ? remove last : add set without checking duplicate"},
	{"new"           , 0, 0},
	{"newclipboard"  , 0, 0, "Open [arg + ' ' +] clipboard text in a new window"},
	{"newselection"  , 0, 0, "Open [arg + ' ' +] selection ..."},
	{"newsecondary"  , 0, 0, "Open [arg + ' ' +] secondaly ..."},
	{"findclipboard" , 0, 0},
	{"findsecondary" , 0, 0},

	{"tohintopen"    , 0, 0, "not click but opens uri as opennew/back"},

	{"openback"      , 0, 0},
	{"openwithref"   , 0, 0, "Current uri is sent as Referer"},
	{"download"      , 0, 0},
	{"dlwithheaders" , 0, 0, "Current uri is sent as Referer. Also cookies"},
	{"showmsg"       , 0, 0},
	{"raise"         , 0, 0},
	{"winpos"        , 0, 0, "x:y"},
	{"winsize"       , 0, 0, "w:h"},
	{"click"         , 0, 0, "x:y"},
	{"openeditor"    , 0, 0},

	{"spawn"         , 0, 0, "arg is called with environment variables"},

	{"sh"            , 0, 0, "sh -c arg with env vars"},
	{"shjs"          , 0, 0, "sh(arg2) with javascript(arg)'s $RESULT"},
	{"shhint"        , 0, 0, "sh with envs selected by a hint"},
	{"shrange"       , 0, 0, "sh with envs selected by ranged hints"},
	{"shsrc"         , 0, 0, "sh with src of current page via pipe"},
#if WEBKIT_CHECK_VERSION(2, 20, 0)
	{"shcookie"      , 0, 0,
		"` "APP" // cookies $URI 'echo $RESULT' ` prints headers."
			"\n  Make sure, the callbacks of "APP" are async."
			"\n  The stdout is not caller's but first process's stdout."},
#endif

//todo pagelist
//	{"windowimage"   , 0, 0}, //pageid
//	{"windowlist"    , 0, 0}, //=>pageid uri title
};
static char *ke2name(Win *win, GdkEventKey *ke)
{
	guint key = ke->keyval;

	char **swaps = getsetsplit(win, "keybindswaps");
	if (swaps)
	{
		for (char **swap = swaps; *swap; swap++)
		{
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
	}

	guint mask = ke->state & (~GDK_SHIFT_MASK &
			gdk_keymap_get_modifier_mask(
				gdk_keymap_get_for_display(gdk_display_get_default()),
				GDK_MODIFIER_INTENT_DEFAULT_MOD_MASK));
	static int len = sizeof(dkeys) / sizeof(*dkeys);
	for (int i = 0; i < len; i++) {
		Keybind b = dkeys[i];
		if (key == b.key && b.mask == mask)
			return b.name;
	}
	return NULL;
}
//declaration
static Win *newwin(const char *uri, Win *cbwin, Win *caller, int back);
static bool _run(Win *win, const char* action, const char *arg, char *cdir, char *exarg)
{
	const char *zz = NULL;
#define Z(str, func) if (!strcmp(action, str)) {func; goto out;}
#define ZZ(t1, t2, f) Z((zz = t1), f) Z((zz = t2), f)
	//D(action %s, action)
	if (action == NULL) return false;
	char **agv = NULL;

	Z("new"         , win = newwin(arg, NULL, NULL, 0))
	Z("plugto"      , plugto = atol(exarg ?: arg ?: "0");
			return run(win, "new", exarg ? arg : NULL))

#define CLIP(clip) \
		char *uri = g_strdup_printf(arg ? "%s %s" : "%s%s", arg ?: "", \
			gtk_clipboard_wait_for_text(gtk_clipboard_get(clip))); \
		win = newwin(uri, NULL, NULL, 0); \
		g_free(uri)
	Z("newclipboard", CLIP(GDK_SELECTION_CLIPBOARD))
	Z("newselection", CLIP(GDK_SELECTION_PRIMARY))
	Z("newsecondary", CLIP(GDK_SELECTION_SECONDARY))
#undef CLIP

	if (win == NULL) return false;

	//internal
	Z("_setreq"    , send(win, Coverset, win->overset))
	Z("_textlinkon", textlinkon(win))
	Z("_blocked"   ,
			_showmsg(win, g_strdup_printf("Blocked %s", arg));
			return true;)
	Z("_reloadlast", reloadlast())
	Z("_focusuri"  , win->usefocus = true; GFA(win->focusuri, g_strdup(arg)))
	if (!strcmp(action, "_hintret"))
	{
		const char *orgarg = arg;
		exarg = *++arg == '0' ? "0" : "1";
		agv = g_strsplit(++arg, " ", 3);
		arg = agv[1];

		action = win->spawn->action;
		if (!strcmp(action, "bookmark"))
			arg = strchr(orgarg, ' ') + 1;
		else if (!strcmp(action, "spawn") || !strcmp(action, "sh"))
		{
			setresult(win, NULL);
			win->linklabel = g_strdup(agv[2]);

			switch (*orgarg) {
			case 'l':
				win->link  = g_strdup(arg); break;
			case 'i':
				win->image = g_strdup(arg); break;
			case 'm':
				win->media = g_strdup(arg); break;
			}

			envspawn(win->spawn, true, exarg, NULL, 0);
			goto out;
		}
	}

	if (arg != NULL) {
		Z("find"   , find(win, arg, true, false))
		Z("open"   , openuri(win, arg))
		Z("opennew", newwin(arg, NULL, win, 0))

		Z("bookmark",
				agv = g_strsplit(arg, " ", 2); addlink(win, agv[1], *agv);)

		//nokey
		Z("openback",
			altcur(win, 0,0); showmsg(win, "Opened"); newwin(arg, NULL, win, 1))
		Z("openwithref",
			const char *ref = agv ? *agv : URI(win);
			char *nrml = soup_uri_normalize(arg, NULL);
			if (!g_str_has_prefix(ref, APP":") &&
				!g_str_has_prefix(ref, "file:")
			) send(win, Cwithref, sfree(g_strdup_printf("%s %s", ref, nrml)));

			webkit_web_view_load_uri(win->kit, nrml);
			g_free(nrml);
		)
		Z("download", webkit_web_view_download_uri(win->kit, arg))
		Z("dlwithheaders",
			Win *dlw = newwin(NULL, win, win, 2);
			dlw->fordl = true;

			const char *ref = agv ? *agv : URI(win);
			WebKitURIRequest *req = webkit_uri_request_new(arg);
			SoupMessageHeaders *hdrs = webkit_uri_request_get_http_headers(req);
			if (hdrs && //scheme APP: returns NULL
				!g_str_has_prefix(ref, APP":") &&
				!g_str_has_prefix(ref, "file:"))
				soup_message_headers_append(hdrs, "Referer", ref);
			//load request lacks cookies except policy download at nav action
			webkit_web_view_load_request(dlw->kit, req);
			g_object_unref(req);
		)

		Z("showmsg" , showmsg(win, arg))
		Z("click",
			agv = g_strsplit(arg ?: "100:100", ":", 2);
			double z = webkit_web_view_get_zoom_level(win->kit);
			win->px = atof(*agv) * z;
			win->py = atof(agv[1] ?: exarg) * z;
			makeclick(win, win->pbtn ?: 1);
		)
		Z("openeditor", openeditor(win, arg, NULL))
		ZZ("sh", "spawn",
				envspawn(spawnp(win, zz, arg, cdir, true)
					, true, exarg, NULL, 0))
		ZZ("shjs", "jscallback"/*backward*/,
			webkit_web_view_run_javascript(win->kit, arg, NULL, jscb
				, spawnp(win, zz, exarg, cdir, true)))
		ZZ("shsrc", "sourcecallback"/*backward*/,
			WebKitWebResource *res =
				webkit_web_view_get_main_resource(win->kit);
			webkit_web_resource_get_data(res, NULL, resourcecb
				, spawnp(win, zz, arg, cdir, true));
			)
#if WEBKIT_CHECK_VERSION(2, 20, 0)
		ZZ("shcookie", "cookies"/*backward*/,
			WebKitCookieManager *cm =
				webkit_web_context_get_cookie_manager(ctx);
			webkit_cookie_manager_get_cookies(cm, arg, NULL, cookiescb
				, spawnp(win, zz, exarg, cdir, true)))
#endif
	}

	Z("tonormal"    , win->mode = Mnormal)

	Z("toinsert"    , win->mode = Minsert)
	Z("toinsertinput", win->mode = Minsert; send(win, Ctext, NULL))

	if (g_str_has_prefix(action, "topointer"))
	{
		guint prevbtn = win->pbtn;
		if (!strcmp(action, "topointerright"))
			win->pbtn = 3;
		else if (!strcmp(action, "topointermdl"))
			win->pbtn = 2;
		else
			win->pbtn = 1;

		win->mode = win->mode == Mpointer && prevbtn == win->pbtn ?
			Mnormal : Mpointer;
		goto out;
	}

#define H(str, pcom, paction, arg, dir, pmode) Z(str, win->com = pcom; \
		spawnfree(win->spawn, true); \
		win->spawn = spawnp(win, paction, arg, dir, false); \
		win->mode = pmode)
	H("tohint"        , Cclick, ""        , NULL, NULL, Mhint) //click
	H("tohintopen"    , Clink , "open"    , NULL, NULL, Mhint)
	H("tohintnew"     , Clink , "opennew" , NULL, NULL, Mhint)
	H("tohintback"    , Clink , "openback", NULL, NULL, Mhint)
	H("tohintdl"      , Curi  , getsetbool(win, "dlwithheaders") ?
			"dlwithheaders" : "download"  , NULL, NULL, Mhint)
	H("tohintbookmark", Curi  , "bookmark", NULL, NULL, Mhint)
	H("tohintrangenew", Crange, "sh"      ,
			APP" // opennew $MEDIA_IMAGE_LINK"  , NULL, Mhintrange)

	if (arg != NULL) {
	H("shhint"        , Cspawn, "sh"      , arg , cdir, Mhint)
	H("tohintcallback", Cspawn, "spawn"   , arg , cdir, Mhint) //backward
	H("shrange"       , Crange, "sh"      , arg , cdir, Mhintrange)
	H("tohintrange"   , Crange, "spawn"   , arg , cdir, Mhintrange) //backward
	}
#undef H

	Z("showdldir"   ,
		command(win, getset(win, "diropener") ?: MIMEOPEN, sfree(dldir(win)));
	)

	Z("yankuri"     ,
		gtk_clipboard_set_text(
			gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), URI(win), -1);
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
	Z("quitall"     , gtk_main_quit())

	if (win->mode == Mpointer)
	{
		Z("scrolldown" , pmove(win, GDK_KEY_Down))
		Z("scrollup"   , pmove(win, GDK_KEY_Up))
		Z("scrollleft" , pmove(win, GDK_KEY_Left))
		Z("scrollright", pmove(win, GDK_KEY_Right))
	}
	bool arrow = getsetbool(win, "hjkl2arrowkeys");
	Z(arrow ? "scrolldown"  : "arrowdown" , putkey(win, GDK_KEY_Down))
	Z(arrow ? "scrollup"    : "arrowup"   , putkey(win, GDK_KEY_Up))
	Z(arrow ? "scrollleft"  : "arrowleft" , putkey(win, GDK_KEY_Left))
	Z(arrow ? "scrollright" : "arrowright", putkey(win, GDK_KEY_Right))
	Z(arrow ? "arrowdown"  : "scrolldown" , scroll(win, 0, 1))
	Z(arrow ? "arrowup"    : "scrollup"   , scroll(win, 0, -1))
	Z(arrow ? "arrowleft"  : "scrollleft" , scroll(win, -1, 0))
	Z(arrow ? "arrowright" : "scrollright", scroll(win, 1, 0))

	Z("pagedown"    , putkey(win, GDK_KEY_Page_Down))
	Z("pageup"      , putkey(win, GDK_KEY_Page_Up))
	Z("halfdown"    , send(win, Cscroll, "d"))
	Z("halfup"      , send(win, Cscroll, "u"))

	Z("top"         , putkey(win, GDK_KEY_Home))
	Z("bottom"      , putkey(win, GDK_KEY_End))
	Z("zoomin"      ,
			double z = webkit_web_view_get_zoom_level(win->kit);
			webkit_web_view_set_zoom_level(win->kit, z * 1.06))
	Z("zoomout"     ,
			double z = webkit_web_view_get_zoom_level(win->kit);
			webkit_web_view_set_zoom_level(win->kit, z / 1.06))
	Z("zoomreset"   ,
			webkit_web_view_set_zoom_level(win->kit, 1.0))

	Z("nextwin"     , nextwin(win, true))
	Z("prevwin"     , nextwin(win, false))
	Z("winlist"     ,
			if (inwins(win, NULL, true) > 0)
				win->mode = Mlist;
			else
				showmsg(win, "No other windows");
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
	Z("findnext"    , findnext(win, true))
	Z("findprev"    , findnext(win, false))

#define CLIP(clip) \
	char *val = gtk_clipboard_wait_for_text(gtk_clipboard_get(clip)); \
	if (val) setent(win, val); \
	run(win, "find", val); \
	senddelay(win, Cfocus, NULL);

	Z("findselection", CLIP(GDK_SELECTION_PRIMARY))
	Z("findclipboard", CLIP(GDK_SELECTION_CLIPBOARD))
	Z("findsecondary", CLIP(GDK_SELECTION_SECONDARY))
#undef CLIP

	Z("open"        , win->mode = Mopen)
	Z("edituri"     ,
			win->mode = Mopen;
			setent(win, arg ?: win->focusuri ?: URI(win)))
	Z("opennew"     , win->mode = Mopennew)
	Z("editurinew"  ,
			win->mode = Mopennew;
			setent(win, arg ?: win->focusuri ?: URI(win)))

//	Z("showsource"  , )
	Z("showhelp"    , openuri(win, APP":help"))
	Z("showhistory" , openuri(win, APP":history"))
	Z("showhistoryall", openuri(win, APP":history/all"))
	Z("showmainpage", openuri(win, APP":main"))

	Z("clearallwebsitedata",
			WebKitWebsiteDataManager *mgr =
				webkit_web_context_get_website_data_manager(ctx);
			webkit_website_data_manager_clear(mgr,
				WEBKIT_WEBSITE_DATA_ALL, 0, NULL, NULL, NULL);

			removehistory();
			if (!getsetbool(win, "keepfavicondb"))
				webkit_favicon_database_clear(
					webkit_web_context_get_favicon_database(ctx));

			showmsg(win, action);
	)
	Z("edit"        , openconf(win, false))
	Z("editconf"    , openconf(win, true))
	Z("openconfigdir",
			command(win, getset(win, "diropener") ?: MIMEOPEN, sfree(path2conf(arg))))

	Z("setv"        , return run(win, "set", "v"))
	Z("setscript"   , return run(win, "set", "script"))
	Z("setimage"    , return run(win, "set", "image"))
	bool unset = false;
	if (!strcmp(action, "unset"))
		action = (unset = arg) ? "set" : "setstack";
	Z("set"         ,
			char **os = &win->overset;
			char **ss = g_strsplit(*os ?: "", "/", -1);
			GFA(*os, NULL)
			bool add = !unset;
			if (arg) for (char **s = ss; *s; s++)
			{
				if (g_strcmp0(*s, arg))
					GFA(*os, g_strconcat(*os ?: *s, *os ? "/" : NULL, *s, NULL))
				else
					add = false;
			}
			if (add)
				GFA(*os, g_strconcat(*os ?: arg, *os ? "/" : NULL, arg, NULL))
			g_strfreev(ss);
			resetconf(win, 2))
	Z("set2"        ,
			GFA(win->overset, g_strdup(arg))
			resetconf(win, 2))
	Z("setstack"    ,
			char **os = &win->overset;
			if (arg)
				GFA(*os, g_strconcat(*os ?: arg, *os ? "/" : NULL, arg, NULL))
			else if (*os && strrchr(*os, '/'))
				*(strrchr(*os, '/')) = '\0';
			else if (*os)
				GFA(*os, NULL)
			resetconf(win, 2))

	Z("wbnoreload", wbreload = false) //internal
	Z("addwhitelist", send(win, Cwhite, "white"))
	Z("addblacklist", send(win, Cwhite, "black"))

	Z("textlink", textlinktry(win));
	Z("raise"   , present(arg ? winbyid(arg) ?: win : win))
	ZZ("winpos", "winsize",
		agv = g_strsplit(arg ?: "100:100", ":", 2);
		(!strcmp(action, "winpos") ? gtk_window_move : gtk_window_resize)
			(win->win, atoi(*agv), atoi(agv[1] ?: exarg)))

	char *msg = g_strdup_printf("Invalid action! %s arg: %s", action, arg);
	showmsg(win, msg);
	puts(msg);
	g_free(msg);
	return false;

#undef ZZ
#undef Z
out:
	update(win);
	if (agv) g_strfreev(agv);
	return true;
}
bool run(Win *win, char* action, const char *arg)
{
	return _run(win, action, arg, NULL, NULL);
}
static bool setact(Win *win, char *key, const char *spare)
{
	char *act = getset(win, key);
	if (!act) return false;
	char **acta = g_strsplit(act, " ", 2);
	run(win, acta[0], acta[1] ?: spare);
	g_strfreev(acta);
	return true;
}


//@@win and cbs:
static gboolean focuscb(Win *win)
{
	if (LASTWIN->mode == Mlist && win != LASTWIN)
		tonormal(LASTWIN);

	g_ptr_array_remove(wins, win);
	g_ptr_array_insert(wins, 0, win);

	checkconf(NULL); //to create conf
	fixhist(win);

	return false;
}


//@download
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
	char   *name;
	char   *dldir;
	const char *dispname;
	guint64 len;
	bool    res;
	bool    finished;
	int     closemsec;
} DLWin;
static void addlabel(DLWin *win, const char *str)
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

	quitif();
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

	char *title;
	if (win->res)
	{
		title = g_strdup_printf("DL: Finished: %s", win->dispname);
		gtk_progress_bar_set_fraction(win->prog, 1);

		char *fn = g_filename_from_uri(
				webkit_download_get_destination(win->dl), NULL, NULL);

		const char *nfn = NULL;
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

		addlabel(win, sfree(g_strdup_printf("=>  %s", nfn)));
		g_free(fn);

		g_timeout_add(win->closemsec, (GSourceFunc)dlclosecb, win);
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
	gtk_window_set_title(win->win, sfree(g_strdup_printf(
					"DL: Failed: %s - %s", win->dispname, err->message)));
}
static void dldatacb(DLWin *win)
{
	double p = webkit_download_get_estimated_progress(win->dl);
	gtk_progress_bar_set_fraction(win->prog, p);

	gtk_window_set_title(win->win, sfree(g_strdup_printf(
					"DL: %.2f%%: %s ", (p * 100), win->dispname)));
}
//static void dlrescb(DLWin *win) {}
static void dldestcb(DLWin *win)
{

	win->entw = gtk_entry_new();
	gtk_entry_set_text(win->ent, sfree(g_filename_from_uri(
			webkit_download_get_destination(win->dl), NULL, NULL)));
	gtk_entry_set_alignment(win->ent, .5);

	gtk_box_pack_start(win->box, win->entw, true, true, 4);
	gtk_widget_show_all(win->entw);
}
static gboolean dldecidecb(WebKitDownload *pdl, char *name, DLWin *win)
{
	char *path = g_build_filename(win->dldir, name, NULL);

	if (strcmp(win->dldir, sfree(g_path_get_dirname(path))))
		GFA(path, g_build_filename(win->dldir, name = "noname", NULL))

	mkdirif(path);

	char *org = g_strdup(path);
	//Last ext is duplicated for keeping order and easily rename
	char *dot = strrchr(org, '.');
	if (!dot || dot == org || !*(dot + 1) ||
			strlen(dot) > 4 + 1) //have not to support long ext
		dot = "";
	for (int i = 2; g_file_test(path, G_FILE_TEST_EXISTS); i++)
		GFA(path, g_strdup_printf("%s.%d%s", org, i, dot))
	g_free(org);

	webkit_download_set_destination(pdl, sfree(
				g_filename_to_uri(path, NULL, NULL)));

	g_free(path);


	//set view data
	win->res = true;

	win->name     = g_strdup(name);
	win->dispname = win->name ?: "";
	addlabel(win, win->name);

	WebKitURIResponse *res = webkit_download_get_response(win->dl);
	addlabel(win, webkit_uri_response_get_mime_type(res));
	win->len =  webkit_uri_response_get_content_length(res);

	if (win->len)
		addlabel(win, sfree(g_strdup_printf(
						"size: %.3f MB", win->len / 1000000.0)));
	return true;
}
static gboolean dlkeycb(GtkWidget *w, GdkEventKey *ek, DLWin *win)
{
	if (GDK_KEY_q == ek->keyval &&
			(!win->ent || !gtk_widget_has_focus(win->entw)))
		gtk_widget_destroy(w);
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
	Win *mainwin = kit ? g_object_get_data(G_OBJECT(kit), "win") : NULL;
	win->dldir   = dldir(mainwin);
	win->closemsec = getsetint(mainwin, "dlwinclosemsec");

	win->winw  = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(win->win, "DL : Waiting for a response.");
	gtk_window_set_default_size(win->win, 400, -1);
	SIGW(win->wino, "destroy"         , dldestroycb, win);
	SIG( win->wino, "key-press-event" , dlkeycb    , win);

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
		int gy;
		gdk_window_get_geometry(gdkw(LASTWIN->winw), NULL, &gy, NULL, NULL);
		int x, y;
		gtk_window_get_position(LASTWIN->win, &x, &y);
		gtk_window_move(win->win, MAX(0, x - 400), y + gy);
	}

	if (getsetbool(mainwin, "dlwinback") && LASTWIN &&
			gtk_window_is_active(LASTWIN->win))
	{
		gtk_window_set_accept_focus(win->win, false);
		gtk_widget_show_all(win->winw);
//not works
//		gdk_window_restack(gdkw(win->winw), gdkw(LASTWIN->winw), false);
//		gdk_window_lower();
		gtk_window_present(LASTWIN->win);
		g_timeout_add(100, (GSourceFunc)acceptfocuscb, win->win);
	} else {
		gtk_widget_show_all(win->winw);
	}

	addlabel(win, webkit_uri_request_get_uri(webkit_download_get_request(pdl)));
	g_ptr_array_insert(dlwins, 0, win);

	if (mainwin && mainwin->fordl)
		run(mainwin, "quit", NULL);
}


//@uri scheme
static char *histdata(bool rest, bool all)
{
	GSList *hist = NULL;
	int num = 0;
	int size = all ? 0 : confint("histviewsize");

	int start = 0;
	__time_t mtime = 0;
	for (int j = 2; j > 0; j--) for (int i = histfnum - 1; i >= 0; i--)
	{
		if (!rest && size && num >= size) break;
		if (start >= histfnum) break;

		char *path = g_build_filename(histdir, hists[i], NULL);
		bool exists = g_file_test(path, G_FILE_TEST_EXISTS);

		if (start) start++;
		else if (exists)
		{
			struct stat info;
			stat(path, &info);
			if (mtime && mtime <= info.st_mtime)
				start = 1;
			mtime = info.st_mtime;
		}

		if (start && exists)
		{
			GSList *lf = NULL;
			GIOChannel *io = g_io_channel_new_file(path, "r", NULL);
			char *line;
			while (g_io_channel_read_line(io, &line, NULL, NULL, NULL)
					== G_IO_STATUS_NORMAL)
			{
				char **stra = g_strsplit(line, " ", 3);
				if (stra[0] && stra[1])
				{
					if (stra[2]) g_strchomp(stra[2]);
					lf = g_slist_prepend(lf, stra);
					num++;
				}
				else
					g_strfreev(stra);

				g_free(line);
			}
			if (lf) hist = g_slist_append(hist, lf);
			g_io_channel_unref(io);
		}
		g_free(path);
	}

	if (!num)
		return g_strdup("<h1>No Data</h1>");

	GString *ret = g_string_new(NULL);
	g_string_append_printf(ret,
		"<html><meta charset=utf8>\n"
		"<style>\n"
		"* {border-radius:.4em;}\n"
		"p {margin:.4em 0; white-space:nowrap;}\n"
		"a, a > * {display:inline-block; vertical-align:middle;}"
		"a {padding:.2em; color:inherit; text-decoration:none;}\n"
		"a:hover {background-color:#faf6ff}\n"
		"time {font-family:monospace;}\n"
		"a > span {padding:0 .4em 0 .6em; white-space:normal; word-wrap:break-word;}\n"
		"i {font-size:.79em; color:#43a;}\n"
		//for img
		"em {min-width:%dpx; text-align:center;}\n"
		"img {"
		" box-shadow:0 .1em .1em 0 #ccf;"
		" display:block;"
		" margin:auto;"
		"}\n"
		"</style>\n"
		, confint("histimgsize"));

	int i = 0;
	GList *il = confint("histimgs") ? g_queue_peek_head_link(histimgs) : NULL;
	for (GSList *ns = hist; ns; ns = ns->next)
		for (GSList *next = ns->data; next; next = next->next)
	{
		if (!size) ;
		else if (rest)
		{
			if (size > i++)
			{
				if (il) il = il->next;
				continue;
			}
		}
		else if (size == i++)
		{
			if (num > size)
				g_string_append(ret,
						"<h3><i>"
						"<a href="APP":history/rest>Show Rest</a>"
						"&nbsp|&nbsp;"
						"<a href="APP":history/all>Show All</a>"
						"</i></h3>");
			goto loopout;
		}

		char **stra = next->data;
		char *escpd = g_markup_escape_text(stra[2] ?: stra[1], -1);

		if (il)
		{
			char *itag = !il->data ? NULL : g_strdup_printf(
					"<img src="APP":histimg/%"G_GUINT64_FORMAT"></img>",
					((Img *)il->data)->id);

			g_string_append_printf(ret,
					"<p><a href=%s><em>%s</em>"
					"<span>%s<br><i>%s</i><br><time>%.11s</time></span></a>\n",
					stra[1], itag ?: "-", escpd, stra[1], stra[0]);

			g_free(itag);
			il = il->next;
		} else
			g_string_append_printf(ret,
					"<p><a href=%s><time>%.11s</time>"
					"<span>%s<br><i>%s</i></span></a>\n",
					stra[1], stra[0], escpd, stra[1]);

		g_free(escpd);
	}
loopout:

	for (GSList *ns = hist; ns; ns = ns->next)
		g_slist_free_full(ns->data, (GDestroyNotify)g_strfreev);
	g_slist_free(hist);

	return g_string_free(ret, false);
}
static char *helpdata()
{
	GString *ret = g_string_new(NULL);
	g_string_append_printf(ret,
		"<body style=margin:0>\n"
		"<p style=padding:.3em;background-color:#ccc>Last MSG: %s</p>\n"
		"<pre style=margin:.3em;font-size:large>\n"
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
		"    press and scroll down: winlist\n"
		"\n"
		"context-menu:\n"
		"  You can add your own script to the context-menu. See 'menu' dir in\n"
		"  the config dir, or click 'editMenu' in the context-menu.\n"
		"  ISCALLBACK, SUFFIX, WINID, WINSLEN, CURRENTSET, URI, TITLE, FOCUSURI,\n"
		"  LINK, LINK_OR_URI, LINKLABEL, LABEL_OR_TITLE,\n"
		"  MEDIA, IMAGE, MEDIA_IMAGE_LINK,\n"
		"  WINX, WINY, WIDTH, HEIGHT, CANBACK, CANFORWARD,\n"
		"  PRIMARY/SELECTION, SECONDARY, CLIPBORAD,\n"
		"  DLDIR and CONFDIR are set as environment variables.\n"
		"  Available actions are in the 'key:' section below.\n"
		"  Of course it supports directories and '.'.\n"
		"  '.' hides it from the menu but still available in the accels.\n"
		"accels:\n"
		"  You can add your own keys to access context-menu items we added.\n"
		"  To add Ctrl-Z to GtkAccelMap, insert '&lt;Primary&gt;&lt;Shift&gt;z' to the\n"
		"  last \"\" in the file 'accels' in the conf directory assigned 'c'\n"
		"  key, and remove the ';' at the beginning of the line. alt is &lt;Alt&gt;.\n"
		"\n"
		"key:\n"
		"#%d - is ctrl\n"
		"#(null) is only for script\n"
		, lastmsg, usage, GDK_CONTROL_MASK);

	for (int i = 0; i < sizeof(dkeys) / sizeof(*dkeys); i++)
		g_string_append_printf(ret, "%d - %-11s: %-19s: %s\n",
				dkeys[i].mask,
				gdk_keyval_name(dkeys[i].key),
				dkeys[i].name,
				dkeys[i].desc ?: "");

	return g_string_free(ret, false);
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
	cairo_surface_t *suf = webkit_favicon_database_get_favicon_finish(
			webkit_web_context_get_favicon_database(ctx), res, NULL);
	GInputStream *st = g_memory_input_stream_new();
	if (suf)
	{
		cairo_surface_write_to_png_stream(suf, faviconcairocb, st);
		cairo_surface_destroy(suf);
	}
	webkit_uri_scheme_request_finish(req, st, -1, "image/png");

	g_object_unref(st);
	g_object_unref(req);
}
static void schemecb(WebKitURISchemeRequest *req, gpointer p)
{
	WebKitWebView *kit = webkit_uri_scheme_request_get_web_view(req);
	Win *win = kit ? g_object_get_data(G_OBJECT(kit), "win") : NULL;
	if (win) win->scheme = true;

	const char *path = webkit_uri_scheme_request_get_path(req);

	if (g_str_has_prefix(path, "f/"))
	{
		char *unesc = g_uri_unescape_string(path + 2, NULL);
		g_object_ref(req);
		webkit_favicon_database_get_favicon(
				webkit_web_context_get_favicon_database(ctx),
				unesc, NULL, faviconcb, req);
		g_free(unesc);
		return;
	}

	char *type = NULL;
	char *data = NULL;
	gsize len = 0;
	if (g_str_has_prefix(path, "histimg/"))
	{
		char **args = g_strsplit(path, "/", 2);
		if (*(args + 1))
		{
			guint64 id = g_ascii_strtoull(args[1], NULL, 0);
			for (GList *next = g_queue_peek_head_link(histimgs);
					next; next = next->next)
			{
				Img *img = next->data;
				if (!img || img->id != id) continue;

				type = "image/jpeg";
				data = g_memdup(img->buf, len = img->size);
				break;
			}
		}
		g_strfreev(args);
	}
	else if (g_str_has_prefix(path, "i/"))
	{
		GdkPixbuf *pix = gtk_icon_theme_load_icon(
			gtk_icon_theme_get_default(), path + 2, 256, 0, NULL);
		if (pix)
		{
			type = "image/png";
			gdk_pixbuf_save_to_buffer(pix, &data, &len, "png", NULL, NULL);
			g_object_unref(pix);
		}
	}
	if (!type)
	{
		type = "text/html";
		if (g_str_has_prefix(path, "main"))
		{
			preparemd();
			g_spawn_command_line_sync(
					sfree(g_strdup_printf(
							getset(win, "generator") ?: "cat %s", mdpath)),
					&data, NULL, NULL, NULL);
		}
		else if (g_str_has_prefix(path, "history"))
			data = histdata(
					g_str_has_prefix(path + 7, "/rest"),
					g_str_has_prefix(path + 7, "/all"));
		else if (g_str_has_prefix(path, "help"))
			data = helpdata();
		if (!data)
			data = g_strdup("<h1>Empty</h1>");
		len = strlen(data);
	}

	GInputStream *st = g_memory_input_stream_new_from_data(data, len, g_free);
	webkit_uri_scheme_request_finish(req, st, len, type);
	g_object_unref(st);
}


//@kit's cbs
static gboolean detachcb(GtkWidget * w)
{
	gtk_widget_grab_focus(w);
	return false;
}
static gboolean drawcb(GtkWidget *ww, cairo_t *cr, Win *win)
{
	if (win->lastx || win->lastx || win->mode == Mpointer)
	{
		guint csize = gdk_display_get_default_cursor_size(
				gtk_widget_get_display(win->winw));

		double x, y, size;
		if (win->mode == Mpointer)
			x = win->px, y = win->py, size = csize * .6;
		else
			x = win->lastx, y = win->lasty, size = csize * .2;

		cairo_move_to(cr, x, y - size);
		cairo_line_to(cr, x, y + size);
		cairo_move_to(cr, x - size, y);
		cairo_line_to(cr, x + size, y);

		cairo_set_line_width(cr, size / 6);
		colorb(win, cr, 1);
		cairo_stroke_preserve(cr);
		colorf(win, cr, 1);
		cairo_set_line_width(cr, size / 12);
		cairo_stroke(cr);
	}
	if (win->msg)
	{
		PangoLayout *layout =
			gtk_widget_create_pango_layout(win->winw, win->msg);
		PangoFontDescription *desc = pango_font_description_copy(
				pango_context_get_font_description(
					gtk_widget_get_pango_context(win->winw)));
		pango_font_description_set_size(desc,
				pango_font_description_get_size(desc) * 1.6);

		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		int w, h;
		pango_layout_get_pixel_size(layout, &w, &h);

		int y = gtk_widget_get_allocated_height(win->kitw) - h*1.7;
		y -= gtk_widget_get_visible(win->entw) ?
				gtk_widget_get_allocated_height(win->entw) : 0;

		colorb(win, cr, .8);
		cairo_rectangle(cr, 0, y, w + h*1.2, h);
		cairo_fill(cr);

		colorf(win, cr, .9);
		cairo_move_to(cr, h, y);
		pango_cairo_show_layout(cr, layout);

		g_object_unref(layout);
	}
	if (win->progd != 1)
	{
		guint32 fsize = MAX(10,
				webkit_settings_get_default_font_size(win->set));

		int h = gtk_widget_get_allocated_height(win->kitw);
		int w = gtk_widget_get_allocated_width(win->kitw);
		h -= gtk_widget_get_visible(win->entw) ?
				gtk_widget_get_allocated_height(win->entw) : 0;

		int px, py;
		gdk_window_get_device_position(
				gdkw(win->kitw), pointer(), &px, &py, NULL);

		double alpha = !gtk_widget_has_focus(win->kitw) ? .6 :
			px > 0 && px < w ? MIN(1, .3 + ABS(h - py) / (h * .1)): 1.0;

		double base = fsize/14. + (fsize/7.) * pow(1 - win->progd, 4);
		//* 2: for monitors hide bottom pixels when viewing top to bottom
		double y = h - base * 2;

		cairo_set_line_width(cr, base * 1.4);
		cairo_move_to(cr, 0, y);
		cairo_line_to(cr, w, y);
		colorb(win, cr, alpha * .6);
		cairo_stroke(cr);

		win->progrect = (GdkRectangle){0, y - base - 1, w, y + base * 2 + 2};

		cairo_set_line_width(cr, base * 2);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_move_to(cr,     w/2 * win->progd, y);
		cairo_line_to(cr, w - w/2 * win->progd, y);
		colorf(win, cr, alpha);
		cairo_stroke(cr);
	} else
		win->progrect.width = 0;

	winlist(win, 0, cr);
	return false;
}
static void destroycb(Win *win)
{
	g_ptr_array_remove(wins, win);

	quitif();

	g_free(win->pageid);
	g_free(win->lasturiconf);
	g_free(win->lastreset);
	g_free(win->overset);
	g_free(win->msg);

	setresult(win, NULL);
	g_free(win->focusuri);

	g_slist_free_full(win->undo, g_free);
	g_slist_free_full(win->redo, g_free);
	g_free(win->lastsearch);

	g_free(win->histstr);

	//spawn
	spawnfree(win->spawn, true);

	g_free(win);
}
static void crashcb(Win *win)
{
	win->crashed = true;
	tonormal(win);
}
static void notifycb(Win *win) { update(win); }
static void drawprogif(Win *win, bool force)
{
	if ((win->progd != 1 || force) && win->progrect.width)
		gdk_window_invalidate_rect(gdkw(win->kitw), &win->progrect, TRUE);
}
static gboolean drawprogcb(Win *win)
{
	if (!isin(wins, win)) return false;
	double shift = win->prog + .4 * (1 - win->prog);
	if (shift - win->progd < 0) return true; //when reload prog is may mixed
	win->progd = shift - (shift - win->progd) * .96;
	drawprogif(win, false);
	return true;
}
static void progcb(Win *win)
{
	win->prog = webkit_web_view_get_estimated_load_progress(win->kit);
	//D(prog %f, win->prog)

	if (win->prog > .3) //.3 emits after other events just about
		updatehist(win);
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
static bool checkppress(Win *win, guint key)
{
	if (!win->ppress || (key && key != win->ppress)) return false;
	win->ppress = 0;
	putbtn(win, GDK_BUTTON_RELEASE, win->pbtn);
	tonormal(win);
	return true;
}
static bool keyr = true;
static gboolean keycb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (ek->is_modifier) return false;

	if (win->mode == Mpointer &&
			(ek->keyval == GDK_KEY_space || ek->keyval == GDK_KEY_Return))
	{
		if (!win->ppress)
		{
			putbtn(win, GDK_BUTTON_PRESS, win->pbtn);
			win->ppress = ek->keyval;
		}
		return true;
	}

	keyr = true;
	char *action = ke2name(win, ek);

	if (action && !strcmp(action, "tonormal"))
	{
		if (win->lastx || win->lasty)
		{
			win->lastx = win->lasty = 0;
			gtk_widget_queue_draw(win->kitw);
		}

		keyr = !(win->mode & (Mnormal | Minsert));

		if (win->mode == Mpointer)
			win->px = win->py = 0;

		if (win->mode == Mnormal)
		{
			send(win, Cblur, NULL);
			webkit_find_controller_search_finish(win->findct);
		}
		else
			tonormal(win);

		return keyr;
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

		return keyr = false;
	}

	if (win->mode & Mhint && !(ek->state & GDK_CONTROL_MASK) &&
			(ek->keyval == GDK_KEY_Tab || ek->keyval == GDK_KEY_Return
			 || (ek->keyval < 128
				 && strchr(getset(win, "hintkeys") ?: "", ek->keyval)))
	) {
		char key[2] = {0};
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

		Z("arrowdown"  , winlist(win, GDK_KEY_Page_Down , NULL))
		Z("arrowup"    , winlist(win, GDK_KEY_Page_Up   , NULL))

		Z("quit"     , winlist(win, 3, NULL))
		Z("quitnext" , winlist(win, 3, NULL))
		Z("quitprev" , winlist(win, 3, NULL))

		Z("winlist"  , tonormal(win); return true;)
#undef Z
		switch (ek->keyval) {
		case GDK_KEY_Page_Down:
		case GDK_KEY_Page_Up:
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
		gtk_widget_queue_draw(win->kitw);
		return true;
	}

	win->userreq = true;

	if (!action)
		return keyr = false;

	run(win, action, NULL);

	return true;
}
static gboolean keyrcb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (checkppress(win, ek->keyval)) return true;

	if (ek->is_modifier) return false;
	return keyr;
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
static GdkEvent *pendingmiddlee;
static gboolean btncb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	win->userreq = true;

	if (e->type != GDK_BUTTON_PRESS) return false;
	altcur(win, e->x, e->y); //clears if it is alt cur

	if (win->mode == Mlist)
	{
		win->cursorx = win->cursory = 0;
		if (e->button == 1)
			win->cancelbtn1r = true;
		if ((e->button != 1 && e->button != 3)
				|| !winlist(win, e->button, NULL))
			tonormal(win);

		if (e->button == 3)
		{
			GdkEvent *ne = gdk_event_peek();
			if (ne && (ne->type == GDK_2BUTTON_PRESS || ne->type == GDK_3BUTTON_PRESS))
				win->cancelcontext = true;
			gdk_event_free(ne);
		}
		return true;
	}

	//workaround
	//for lacking of target change event when btn event happens with focus in;
	if (win->mode != Mpointer || !win->ppress)
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
		gtk_widget_queue_draw(win->kitw);

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

		motion(win, e->x, e->y - 10000); //move somewhere
		motion(win, e->x, e->y        ); //and now enter !!

		if (pendingmiddlee)
			gdk_event_free(pendingmiddlee);
		pendingmiddlee = gdk_event_copy((GdkEvent *)e);
		return true;
	}
	case 3:
		if (!(e->state & GDK_BUTTON1_MASK))
			return win->crashed ?
				run(win, "reload", NULL) : false;

		win->cancelcontext = win->cancelbtn1r = true;
		if (!(win->lastx + win->lasty)) break;

		double deltax = e->x - win->lastx,
		       deltay = e->y - win->lasty;

		if (MAX(abs(deltax), abs(deltay)) < threshold(win) * 3)
		{ //default
			setact(win, "rockerleft", URI(win));
		}
		else if (fabs(deltax) > fabs(deltay)) {
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

	return false;
}
static gboolean btnrcb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	switch (e->button) {
	case 1:
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->kitw);

		if (win->cancelbtn1r) {
			win->cancelbtn1r = false;
			return true;
		}
		break;
	case 2:
	{
		bool cancel = win->cancelmdlr || !(win->lastx + win->lasty);
		double deltax = e->x - win->lastx;
		double deltay = e->y - win->lasty;
		win->lastx = win->lasty = 0;
		win->cancelmdlr = false;

		if (cancel) return true;

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
		else if (fabs(deltax) > fabs(deltay)) {
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

		gtk_widget_queue_draw(win->kitw);

		return true;
	}
	}

	update(win);
	return false;
}
static void dragccb(GdkDragContext *ctx, GdkDragCancelReason reason, Win *win)
{
	if (reason != GDK_DRAG_CANCEL_NO_TARGET) return;

	GdkWindow *gw = gdkw(win->kitw);
	GdkDevice *gd = gdk_drag_context_get_device(ctx);
	GdkModifierType mask;
	gdk_device_get_state(gd, gw, NULL, &mask);

	if (mask & GDK_BUTTON1_MASK || mask & GDK_BUTTON3_MASK)
	{ //we assume this is right click though it only means a btn released
		double px, py;
		gdk_window_get_device_position_double(gw, gd, &px, &py, NULL);
		_putbtn(win, GDK_BUTTON_PRESS, 13, px, py);
	}
}
static void dragbcb(GtkWidget *w, GdkDragContext *ctx ,Win *win)
{
	if (win->mode == Mpointer)
	{
		showmsg(win, "Pointer Mode does not support drag");
		putkey(win, GDK_KEY_Escape);
		checkppress(win, 0);
	}
	else
		SIG(ctx, "cancel", dragccb, win);
}
static gboolean entercb(GtkWidget *w, GdkEventCrossing *e, Win *win)
{ //for checking drag end with button1
	if (!(e->state & GDK_BUTTON1_MASK) && win->lastx + win->lasty)
	{
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->kitw);
	}
	else drawprogif(win, false);

	checkppress(win, 0); //right click
	return false;
}
static gboolean leavecb(GtkWidget *w, GdkEventCrossing *e, Win *win)
{
	drawprogif(win, false);
	return false;
}
static gboolean motioncb(GtkWidget *w, GdkEventMotion *e, Win *win)
{
	if (win->mode == Mlist)
	{
		if (win->scrlf &&
				MAX(abs(e->x - win->scrlx), abs(e->y - win->scrly))
				 < threshold(win))
			return true;

		win->scrlf = false;
		win->scrlcur = 0;
		win->cursorx = win->cursory = 0;
		gtk_widget_queue_draw(win->kitw);

		static GdkCursor *hand = NULL;
		if (!hand) hand = gdk_cursor_new_for_display(
				gdk_display_get_default(), GDK_HAND2);
		gdk_window_set_cursor(gdkw(win->kitw),
				winlist(win, 0, NULL) ? hand : NULL);

		return true;
	}

	drawprogif(win, false);

	return false;
}

typedef struct {
	int times;
	GdkEvent *e;
} Scrl;
static int scrlcnt;
static gboolean multiscrlcb(Scrl *si)
{
	if (si->times--)
	{
		gdk_event_put(si->e);
		return true;
	}
	gdk_event_free(si->e);
	g_free(si);
	scrlcnt--;
	return false;
}
static gboolean scrollcb(GtkWidget *w, GdkEventScroll *pe, Win *win)
{
	if (pe->send_event) return false;

	if (win->mode == Mlist)
	{
		win->scrlx = pe->x;
		win->scrly = pe->y;
		winlist(win,
			pe->direction == GDK_SCROLL_UP || pe->delta_y < 0 ?
				GDK_KEY_Page_Up : GDK_KEY_Page_Down,
			NULL);
		gtk_widget_queue_draw(win->kitw);
		return true;
	}

	if (pe->state & GDK_BUTTON2_MASK && (
		((pe->direction == GDK_SCROLL_UP || pe->delta_y < 0) &&
		 setact(win, "pressscrollup", URI(win))
		) ||
		((pe->direction == GDK_SCROLL_DOWN || pe->delta_y > 0) &&
		 setact(win, "pressscrolldown", URI(win))
		) ))
	{
		win->cancelmdlr = true;
		return true;
	}

	if (scrlcnt > 44) return false;

	int times = getsetint(win, "multiplescroll");
	if (!times) return false;

	times--;
#define Z 3
	if (scrlcnt >= Z && pe->device != keyboard())
		times = (times + 1) * scrlcnt / Z;
#undef Z

	GdkEventScroll *es = kitevent(win, true, GDK_SCROLL);

	es->send_event = true;
	es->state      = pe->state;
	es->direction  = pe->direction;
	es->delta_x    = pe->delta_x;
	es->delta_y    = pe->delta_y;
	es->x          = pe->x;
	es->y          = pe->y;
	es->device     = pe->device;

	Scrl *si = g_new0(Scrl, 1);
	si->times = times;
	si->e = (void *)es;

	g_timeout_add(300 / (times + 4), (GSourceFunc)multiscrlcb, si);
	scrlcnt++;
	return false;
}
static bool urihandler(Win *win, const char *uri, char *group)
{
	if (!g_key_file_has_key(conf, group, "handler", NULL)) return false;

	char *buf = g_key_file_get_boolean(conf, group, "handlerunesc", NULL) ?
			g_uri_unescape_string(uri, NULL) : NULL;

	char *esccs = g_key_file_get_string(conf, group, "handlerescchrs", NULL);
	if (esccs && *esccs)
		GFA(buf, _escape(buf ?: uri, esccs))
	g_free(esccs);

	char *command = g_key_file_get_string(conf, group, "handler", NULL);
	GFA(command, g_strdup_printf(command, buf ?: uri))
	g_free(buf);

	run(win, "spawn", command);
	_showmsg(win, g_strdup_printf("Handled: %s", command));

	g_free(command);
	return true;
}
static gboolean policycb(
		WebKitWebView *v,
		void *dec, //WebKitPolicyDecision
		WebKitPolicyDecisionType type,
		Win *win)
{
	if (win->fordl && type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
	{
		//webkit_policy_decision_download in nav is illegal but sends cookies.
		//it changes uri of wins and can't recover.
		webkit_policy_decision_download(dec);
		return true;
	}

	if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE)
	{
		WebKitNavigationAction *na =
			webkit_navigation_policy_decision_get_navigation_action(dec);
		WebKitURIRequest *req =
			webkit_navigation_action_get_request(na);

		if (eachuriconf(win, webkit_uri_request_get_uri(req), true, urihandler))
		{
			webkit_policy_decision_ignore(dec);
			return true;
		} else
		if (webkit_navigation_action_is_user_gesture(na))
			altcur(win, 0, 0);

		return false;
	}

	WebKitResponsePolicyDecision *rdec = dec;
	WebKitURIResponse *res = webkit_response_policy_decision_get_response(rdec);

	bool dl = false;
	char *msr = getset(win, "dlmimetypes");
	//for checking whether is sub frame or not.
	//this time webkit_web_resource_get_response is null yet except on sub frames
	//unfortunately on nav it returns prev page though
	WebKitWebResource *mresrc = webkit_web_view_get_main_resource(win->kit);
	if (msr && *msr && !webkit_web_resource_get_response(mresrc))
	{
		char **ms = g_strsplit(msr, ";", -1);
		const char *mime = webkit_uri_response_get_mime_type(res);
		for (char **m = ms; *m; m++)
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
	char *handle = getset(win, "newwinhandle");
	Win *new = NULL;

	if      (!g_strcmp0(handle, "notnew")) return win->kitw;
	else if (!g_strcmp0(handle, "ignore")) return NULL;
	else if (!g_strcmp0(handle, "back"  )) new = newwin(NULL, win, win, 1);
	else                       /*normal*/  new = newwin(NULL, win, win, 0);
	return new->kitw;
}
static gboolean sdialogcb(Win *win)
{
	if (getsetbool(win, "scriptdialog"))
		return false;
	showmsg(win, "Script dialog is blocked");
	return true;
}
static void setspawn(Win *win, char *key)
{
	char *fname = getset(win, key);
	if (!fname) return;
	char *path = sfree(g_build_filename(sfree(path2conf("menu")), fname, NULL));
	envspawn(spawnp(win, "", NULL , path, true) , false, NULL, NULL, 0);
}
static void loadcb(WebKitWebView *k, WebKitLoadEvent event, Win *win)
{
	win->crashed = false;
	switch (event) {
	case WEBKIT_LOAD_STARTED:
		//D(WEBKIT_LOAD_STARTED %s, URI(win))
		histperiod(win);
		if (tlwin == win) tlwin = NULL;
		win->scheme = false;
		setresult(win, NULL);
		GFA(win->focusuri, NULL)
		win->tlserr = 0;

		if (win->mode == Minsert) send(win, Cblur, NULL); //clear im
		tonormal(win);
		if (win->userreq) {
			win->userreq = false; //currently not used
		}
		resetconf(win, 0);
		setspawn(win, "onstartmenu");

		//there is progcb before this event but sometimes it is
		//before page's prog and we can't get which is it.
		//policycb? no it emits even sub frames and of course
		//we can't get if it is sub or not.
		win->progd = 0;
		if (!win->drawprogcb)
			win->drawprogcb = g_timeout_add(30, (GSourceFunc)drawprogcb, win);
		gtk_widget_queue_draw(win->kitw);

		//loadcb is multi thread!? and send may block others by alert
		send(win, Cstart, NULL);
		break;
	case WEBKIT_LOAD_REDIRECTED:
		//D(WEBKIT_LOAD_REDIRECTED %s, URI(win))
		resetconf(win, 0);
		send(win, Cstart, NULL);

		break;
	case WEBKIT_LOAD_COMMITTED:
		//D(WEBKIT_LOAD_COMMITED %s, URI(win))
		if (!win->scheme && g_str_has_prefix(URI(win), APP":"))
		{
			webkit_web_view_reload(win->kit);
			break;
		}

		send(win, Con, "c");

		if (webkit_web_view_get_tls_info(win->kit, NULL, &win->tlserr))
			if (win->tlserr) showmsg(win, "TLS Error");

		setspawn(win, "onloadmenu");
		break;
	case WEBKIT_LOAD_FINISHED:
		//D(WEBKIT_LOAD_FINISHED %s, URI(win))

		if (g_strcmp0(win->lastreset, URI(win)))
		{ //for load-failed before commit e.g. download
			resetconf(win, 0);
			send(win, Cstart, NULL);
		}
		else if (win->scheme || !g_str_has_prefix(URI(win), APP":"))
		{
			fixhist(win);
			setspawn(win, "onloadedmenu");
			send(win, Con, "f");
		}

		win->progd = 1;
		drawprogif(win, true);
		g_source_remove(win->drawprogcb);
		win->drawprogcb = 0;
		break;
	}
}
static gboolean failcb(WebKitWebView *k, WebKitLoadEvent event,
		char *uri, GError *err, Win *win)
{
	//D(failcb %d %d %s, err->domain, err->code, err->message)
	// 2042 6 Unacceptable TLS certificate
	// 2042 6 Error performing TLS handshake: An unexpected TLS packet was received.
	static char *last;
	if (err->code == 6 && confbool("ignoretlserr"))
	{
		static int count;
		if (g_strcmp0(last, uri))
			count = 0;
		else if (++count > 2) //three times
		{
			count = 0;
			return false;
		}

		GFA(last, g_strdup(uri))
		//webkit_web_view_reload(win->kit); //this reloads prev page
		webkit_web_view_load_uri(win->kit, uri);
		showmsg(win, "Reloaded by TLS error");
		return true;
	}
	return false;
}

//@contextmenu
typedef struct {
	GClosure *gc; //when dir gc and path are NULL
	char     *path;
	GSList   *actions;
} AItem;
static void clearai(gpointer p)
{
	AItem *a = p;
	if (a->gc)
	{
		g_free(a->path);
		gtk_accel_group_disconnect(accelg, a->gc);
	}
	else
		g_slist_free_full(a->actions, clearai);
	g_free(a);
}
static gboolean actioncb(char *path)
{
	envspawn(spawnp(LASTWIN, "", NULL, path, true), false, NULL, NULL, 0);
	return true;
}
static guint menuhash;
static GSList *dirmenu(
		WebKitContextMenu *menu,
		char *dir,
		char *parentaccel)
{
	GSList *ret = NULL;
	GSList *names = NULL;

	GDir *gd = g_dir_open(dir, 0, NULL);
	const char *dn;
	while ((dn = g_dir_read_name(gd)))
		names = g_slist_insert_sorted(names, g_strdup(dn), (GCompareFunc)strcmp);
	g_dir_close(gd);

	for (GSList *next = names; next; next = next->next)
	{
		char *org = next->data;
		char *name = org + 1;

		if (g_str_has_suffix(name, "---"))
		{
			if (menu)
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new_separator());
			continue;
		}

		AItem *ai = g_new0(AItem, 1);
		bool nodata = false;

		char *laccelp = g_strconcat(parentaccel, "/", name, NULL);
		char *path = g_build_filename(dir, org, NULL);

		if (g_file_test(path, G_FILE_TEST_IS_DIR))
		{
			WebKitContextMenu *sub = NULL;
			if (menu && *org != '.')
				sub = webkit_context_menu_new();

			ai->actions = dirmenu(sub, path, laccelp);
			if (!ai->actions)
				nodata = true;
			else if (menu && *org != '.')
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new_with_submenu(name, sub));

			g_free(path);
		} else {
			ai->path = path;
			addhash(path, &menuhash);
			ai->gc = g_cclosure_new_swap(G_CALLBACK(actioncb), path, NULL);
			gtk_accel_group_connect_by_path(accelg, laccelp, ai->gc);

			if (menu && *org != '.')
			{
#if V18
				GSimpleAction *gsa = g_simple_action_new(laccelp, NULL);
				SIGW(gsa, "activate", actioncb, path);
				webkit_context_menu_append(menu,
						webkit_context_menu_item_new_from_gaction(
							(GAction *)gsa, name, NULL));
				g_object_unref(gsa);
#else
				GtkAction *action = gtk_action_new(name, name, NULL, NULL);
				SIGW(action, "activate", actioncb, path);
				webkit_context_menu_append(menu,
						webkit_context_menu_item_new(action));
				g_object_unref(action);
#endif
			}
		}

		g_free(laccelp);
		if (nodata)
			g_free(ai);
		else
			ret = g_slist_append(ret, ai);
	}
	g_slist_free_full(names, g_free);
	return ret;
}
static void makemenu(WebKitContextMenu *menu); //declaration
static guint menudirtime;
static gboolean menudirtimecb(gpointer p)
{
	makemenu(NULL);
	menudirtime = 0;
	return false;
}
static void menudircb(
		GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e)
{
	if (e == G_FILE_MONITOR_EVENT_CREATED && menudirtime == 0)
		//For editors making temp files
		menudirtime = g_timeout_add(100, menudirtimecb, NULL);
}
static void addscript(char *dir, char *name, char *script)
{
	char *ap = g_build_filename(dir, name, NULL);
	FILE *f = fopen(ap, "w");
	fputs(script, f);
	fclose(f);
	g_chmod(ap, 0700);
	g_free(ap);
}
static char *menuitems[][2] =
 {{".openBackRange"   , APP" // shrange '"APP" // openback $MEDIA_IMAGE_LINK'"
},{".openNewSrcURI"   , APP" // shhint '"APP" // opennew $MEDIA_IMAGE_LINK'"
},{".openWithRef"     , APP" // shhint '"APP" // openwithref $MEDIA_IMAGE_LINK'"
},{"0editMenu"        , APP" // openconfigdir menu"
},{"1bookmark"        , APP" // bookmark \"$LINK_OR_URI $LABEL_OR_TITLE\""
},{"1duplicate"       , APP" // opennew $URI"
},{"1editLabelOrTitle", APP" // edituri \"$LABEL_OR_TITLE\""
},{"1history"         , APP" // showhistory ''"
},{"1windowList"      , APP" // winlist ''"
},{"2main"            , APP" // open "APP":main"
},{"3---"             , ""
},{"3openClipboard"   , APP" // open \"$CLIPBOARD\""
},{"3openClipboardNew", APP" // opennew \"$CLIPBOARD\""
},{"3openSelection"   , APP" // open \"$PRIMARY\""
},{"3openSelectionNew", APP" // opennew \"$PRIMARY\""
},{"6searchDictionary", APP" // open \"u $PRIMARY\""
},{"9---"             , ""
},{"cviewSource"      , APP" // shsrc 'd=\"$DLDIR/"APP"-source\" && tee > \"$d\" && mimeopen -n \"$d\"'"
},{"v---"             , ""
},{"vchromium"        , "chromium $LINK_OR_URI"
},{"xnoSuffixProcess" , APP" / new $LINK_OR_URI"
},{NULL}};
void makemenu(WebKitContextMenu *menu)
{
	static GSList *actions;
	static bool firsttime = true;

	char *dir = path2conf("menu");
	if (_mkdirif(dir, false))
		for (char *(*ss)[2] = menuitems; **ss; ss++)
			addscript(dir, **ss, (*ss)[1]);

	if (firsttime)
	{
		firsttime = false;
		accelg = gtk_accel_group_new();

		GFile *gf = g_file_new_for_path(dir);
		GFileMonitor *gm = g_file_monitor_directory(gf,
				G_FILE_MONITOR_NONE, NULL, NULL);
		SIG(gm, "changed", menudircb, NULL);
		g_object_unref(gf);

		accelp = path2conf("accels");
		monitor(accelp, checkaccels);

		if (g_file_test(accelp, G_FILE_TEST_EXISTS))
			gtk_accel_map_load(accelp);
	}

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
		cancelaccels = true;
		gtk_accel_map_save(accelp);
	}

	g_free(dir);
}
static gboolean contextcb(WebKitWebView *k,
		WebKitContextMenu   *menu,
		GdkEvent            *e,
		WebKitHitTestResult *htr,
		Win                 *win)
{
	if (win->cancelcontext)
	{
		win->cancelcontext = false;
		return true;
	}
	setresult(win, htr);
	makemenu(menu);
	return false;
}


//@entry
void enticon(Win *win, const char *name)
{
	if (!name) name =
		win->mode == Mfind    ? "edit-find"  :
		win->mode == Mopen    ? "go-jump"    :
		win->mode == Mopennew ? "window-new" : NULL;

	gtk_entry_set_icon_from_icon_name(win->ent, GTK_ENTRY_ICON_PRIMARY, name);
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
		switch (win->mode) {
		case Mfind:
			if (!win->infind || !findtxt(win) || strcmp(findtxt(win), getent(win)))
				run(win, "find", getent(win));

			senddelay(win, Cfocus, NULL);
			break;
		case Mopen:
			run(win, "open", getent(win)); break;
		case Mopennew:
			run(win, "opennew", getent(win)); break;
		default:
				g_assert_not_reached();
		}
		tonormal(win);
		return true;
	case GDK_KEY_Escape:
		if (win->mode == Mfind)
			webkit_find_controller_search_finish(win->findct);
		tonormal(win);
		return true;
	}

	if (!(ke->state & GDK_CONTROL_MASK)) return false;
	//ctrls
	static char *buf;
	int wpos = 0;
	GtkEditable *e = (void *)w;
	int pos = gtk_editable_get_position(e);
	switch (ke->keyval) {
	case GDK_KEY_Z:
	case GDK_KEY_n:
		undo(win, &win->redo, &win->undo); break;
	case GDK_KEY_z:
	case GDK_KEY_p:
		undo(win, &win->undo, &win->redo); break;

	case GDK_KEY_a:
		gtk_editable_set_position(e, 0); break;
	case GDK_KEY_e:
		gtk_editable_set_position(e, -1); break;
	case GDK_KEY_b:
		gtk_editable_set_position(e, MAX(0, pos - 1)); break;
	case GDK_KEY_f:
		gtk_editable_set_position(e, pos + 1); break;

	case GDK_KEY_d:
		gtk_editable_delete_text(e, pos, pos + 1); break;
	case GDK_KEY_h:
		gtk_editable_delete_text(e, pos - 1, pos); break;
	case GDK_KEY_k:
		GFA(buf, g_strdup(gtk_editable_get_chars(e, pos, -1)));
		gtk_editable_delete_text(e, pos, -1); break;
	case GDK_KEY_w:
		for (int i = pos; i > 0; i--)
		{
			char c = *sfree(gtk_editable_get_chars(e, i - 1, i));
			if (c != ' ' && c != '/')
				wpos = i - 1;
			else if (wpos)
				break;
		}
	case GDK_KEY_u:
		GFA(buf, g_strdup(gtk_editable_get_chars(e, wpos, pos)));
		gtk_editable_delete_text(e, wpos, pos);
		break;
	case GDK_KEY_y:
		wpos = pos;
		gtk_editable_insert_text(e, buf ?: "", -1, &wpos);
		gtk_editable_select_region(e, pos, wpos);
		break;
	case GDK_KEY_t:
		if (pos == 0) pos++;
		gtk_editable_set_position(e, -1);
		int chk = gtk_editable_get_position(e);
		if (chk < 2)
			break;
		if (chk == pos)
			pos--;

		char *rm = gtk_editable_get_chars(e, pos - 1, pos);
		          gtk_editable_delete_text(e, pos - 1, pos);
		gtk_editable_insert_text(e, rm, -1, &pos);
		gtk_editable_set_position(e, pos);
		g_free(rm);
		break;
	case GDK_KEY_l:
	{
		int ss, se;
		gtk_editable_get_selection_bounds(e, &ss, &se);
		char *str = gtk_editable_get_chars(e, 0, -1);
		for (char *c = str; *c; c++)
			if (ss == se || (c - str >= ss && c - str < se))
				*c = g_ascii_tolower(*c);

		gtk_editable_delete_text(e, 0, -1);
		gtk_editable_insert_text(e, str, -1, &pos);
		gtk_editable_select_region(e, ss, se);
		break;
	}
	default:
		return false;
	}

	return true;
}
static gboolean textcb(Win *win)
{
	if (win->mode == Mfind && gtk_widget_get_visible(win->entw))
	{
		if (strlen(getent(win)) > 2)
			run(win, "find", getent(win));
		else
		{
			enticon(win, NULL);
			webkit_find_controller_search_finish(win->findct);
		}
	}
	return false;
}
static void findfailedcb(Win *win)
{
	enticon(win, "dialog-warning");
	_showmsg(win, g_strdup_printf("Not found: '%s'", findtxt(win)));
}
static void foundcb(WebKitFindController *f, guint cnt, Win *win)
{
	enticon(win, NULL);
	_showmsg(win, cnt > 1 ? g_strdup_printf("%d", cnt) : NULL);
}


//@newwin
Win *newwin(const char *uri, Win *cbwin, Win *caller, int back)
{
	Win *win = g_new0(Win, 1);
	win->userreq = true;
	win->winw = plugto ?
		gtk_plug_new(plugto) : gtk_window_new(GTK_WINDOW_TOPLEVEL);

	int w, h;
	if (caller)
	{
		win->overset = g_strdup(caller->overset);
		gtk_window_get_size(caller->win, &w, &h);
	}
	else
		w = confint("winwidth"), h = confint("winheight");
	gtk_window_set_default_size(win->win, w, h);

	if (back != 2)
		gtk_widget_show(win->winw);

	SIGW(win->wino, plugto ? "configure-event":"focus-in-event", focuscb, win);

	if (!ctx)
	{
		makemenu(NULL);
		preparewb();

		ephemeral = g_key_file_get_boolean(conf, "boot", "ephemeral", NULL);
		char *data  = g_build_filename(g_get_user_data_dir() , fullname, NULL);
		char *cache = g_build_filename(g_get_user_cache_dir(), fullname, NULL);
		WebKitWebsiteDataManager *mgr = webkit_website_data_manager_new(
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
			//we assume cookies are conf
			webkit_cookie_manager_set_persistent_storage(cookiemgr,
					sfree(path2conf("cookies")),
					WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);

		webkit_cookie_manager_set_accept_policy(cookiemgr,
				WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);

		shared = !g_key_file_get_boolean(conf, "boot", "multiwebprocs", NULL);
		if (!shared)
			webkit_web_context_set_process_model(ctx,
					WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);

		char **argv = g_key_file_get_string_list(
				conf, "boot", "extensionargs", NULL, NULL);
		char *args = g_strjoinv(";", argv);
		g_strfreev(argv);
		char *udata = g_strconcat(args,
				";"APP"abapi;", shared ? "s" : "m", fullname, NULL);
		g_free(args);

		webkit_web_context_set_web_extensions_initialization_user_data(
				ctx, g_variant_new_string(udata));
		g_free(udata);

#if DEBUG
		webkit_web_context_set_web_extensions_directory(ctx,
				sfree(g_get_current_dir()));
#else
		webkit_web_context_set_web_extensions_directory(ctx, EXTENSION_DIR);
#endif

		SIG(ctx, "download-started", downloadcb, NULL);

		webkit_security_manager_register_uri_scheme_as_local(
				webkit_web_context_get_security_manager(ctx), APP);

		webkit_web_context_register_uri_scheme(
				ctx, APP, schemecb, NULL, NULL);

		if (g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL))
			webkit_web_context_set_favicon_database_directory(ctx,
				sfree(g_build_filename(
						g_get_user_cache_dir(), fullname, "favicon", NULL)));

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

	gtk_window_add_accel_group(win->win, accelg);
	//workaround. without get_inspector inspector doesen't work
	//and have to grab forcus;
	SIGW(webkit_web_view_get_inspector(win->kit),
			"detach", detachcb, win->kitw);

	win->set = webkit_settings_new();
	setprops(win, conf, DSET);
	webkit_web_view_set_settings(win->kit, win->set);
	g_object_unref(win->set);
	webkit_web_view_set_zoom_level(win->kit, confdouble("zoom"));
	setcss(win, getset(win, "usercss"));
	gdk_rgba_parse(&win->rgba, getset(win, "msgcolor") ?: "");

	GObject *o = win->kito;
	SIGA(o, "draw"                 , drawcb    , win);
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
	SIG( o, "drag-begin"           , dragbcb   , win);
	SIG( o, "enter-notify-event"   , entercb   , win);
	SIG( o, "leave-notify-event"   , leavecb   , win);
	SIG( o, "motion-notify-event"  , motioncb  , win);
	SIG( o, "scroll-event"         , scrollcb  , win);

	SIG( o, "decide-policy"        , policycb  , win);
	SIGW(o, "create"               , createcb  , win);
	SIGW(o, "close"                , gtk_widget_destroy, win->winw);
	SIGW(o, "script-dialog"        , sdialogcb , win);
	SIG( o, "load-changed"         , loadcb    , win);
	SIG( o, "load-failed"          , failcb    , win);

	SIG( o, "context-menu"         , contextcb , win);

	//for entry
	SIGW(o, "focus-in-event"       , focusincb , win);

	win->findct = webkit_web_view_get_find_controller(win->kit);
	SIGW(win->findct, "failed-to-find-text", findfailedcb, win);
	SIG( win->findct, "found-text"         , foundcb     , win);

	//entry
	win->entw = gtk_entry_new();
	SIG(win->ento, "key-press-event", entkeycb, win);
	GtkEntryBuffer *buf = gtk_entry_get_buffer(win->ent);
	SIGW(buf, "inserted-text", textcb, win);
	SIGW(buf, "deleted-text" , textcb, win);

	//label
	win->lblw = gtk_label_new("");
	gtk_label_set_selectable(win->lbl, true);
	gtk_label_set_ellipsize(win->lbl, PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_xalign(win->lbl, 0);
//	gtk_label_set_line_wrap(win->lbl, true);
//	gtk_label_set_line_wrap_mode(win->lbl, PANGO_WRAP_CHAR);
//	gtk_label_set_use_markup(win->lbl, TRUE);

	//without overlay, showing ent delays when a page is heavy
	GtkWidget  *olw = gtk_overlay_new();
	GtkOverlay *ol  = (GtkOverlay *)olw;

	GtkWidget *boxw  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkBox    *box   = (GtkBox *)boxw;
	GtkWidget *box2w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkBox    *box2  = (GtkBox *)box2w;

	gtk_box_pack_start(box , win->lblw , false, true, 0);
	gtk_box_pack_end(  box , win->kitw , true , true, 0);
	gtk_box_pack_end(  box2, win->entw , false, true, 0);
	gtk_widget_set_valign(box2w, GTK_ALIGN_END);

	gtk_overlay_add_overlay(ol, box2w);
	gtk_overlay_set_overlay_pass_through(ol, box2w, true);

	gtk_container_add(GTK_CONTAINER(ol), boxw);
	gtk_container_add(GTK_CONTAINER(win->win), olw);

	win->pageid = g_strdup_printf("%"G_GUINT64_FORMAT,
			webkit_web_view_get_page_id(win->kit));

	g_ptr_array_add(wins, win);
	if (back == 2)
		return win;

	if (caller)
	{
		webkit_web_view_set_zoom_level(win->kit,
				webkit_web_view_get_zoom_level(caller->kit));
		win->undo = g_slist_copy_deep(caller->undo, (GCopyFunc)g_strdup, NULL);
		win->redo = g_slist_copy_deep(caller->redo, (GCopyFunc)g_strdup, NULL);
		win->lastsearch = g_strdup(findtxt(caller) ?: caller->lastsearch);
		gtk_entry_set_text(win->ent, getent(caller));
	}

	if (getsetbool(win, "addressbar"))
		gtk_widget_show(win->lblw);
	SIGW(win->lblw, "notify::visible", update, win);

	gtk_widget_show(olw);
	gtk_widget_show(boxw);
	gtk_widget_show(box2w);
	gtk_widget_show(win->kitw);
	gtk_widget_grab_focus(win->kitw);

	if (!cbwin)
		_openuri(win, uri, caller);

	present(back && LASTWIN ? LASTWIN : win);

	return win;
}


//@main
static void runline(const char *line, char *cdir, char *exarg)
{
	char **args = g_strsplit(line, ":", 3);

	char *arg = args[2];
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
void ipccb(const char *line)
{
	//m is from main
	if (*line != 'm') return runline(line, NULL, NULL);

	char **args = g_strsplit(line, ":", 4);
	int clen = atoi(args[1]);
	int elen = atoi(args[2]);
	char *cdir = g_strndup(args[3], clen);
	char *exarg = elen == 0 ? NULL : g_strndup(args[3] + clen, elen);

	runline(args[3] + clen + elen, cdir, exarg);

	g_free(cdir);
	g_free(exarg);
	g_strfreev(args);
}
int main(int argc, char **argv)
{
#if DEBUG
	g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
	DD(This bin is compiled with DEBUG)
#endif

	if (argc == 2 && (
			!strcmp(argv[1], "-h") ||
			!strcmp(argv[1], "--help"))
	) {
		g_print("%s", usage);
		exit(0);
	}

	if (argc >= 4)
		suffix = argv[1];
	const char *envsuf = g_getenv("SUFFIX") ?: "";
	if (!strcmp(suffix, "//")) suffix = g_strdup(envsuf);
	if (!strcmp(suffix, "/")) suffix = "";
	if (!strcmp(envsuf, "/")) envsuf = "";
	const char *winid =
		!strcmp(suffix,  envsuf) ? g_getenv("WINID") : NULL;
	if (!winid || !*winid) winid = "0";

	fullname = g_strconcat(OLDNAME, suffix, NULL); //for backward
	if (!g_file_test(path2conf(NULL), G_FILE_TEST_EXISTS))
		GFA(fullname, g_strconcat(DIRNAME, suffix, NULL));

	char *exarg = "";
	if (argc > 4)
	{
		exarg = argv[4];
		argc = 4;
	}

	char *action = argc > 2 ? argv[argc - 2] : "new";
	char *uri    = argc > 1 ? argv[argc - 1] : NULL;

	if (!*action) action = "new";
	if (uri && !*uri) uri = NULL;
	if (argc == 2 && uri && g_file_test(uri, G_FILE_TEST_EXISTS))
		uri = g_strconcat("file://", uri, NULL);

	char *cwd = g_get_current_dir();
	char *sendstr = g_strdup_printf("m:%ld:%ld:%s%s%s:%s:%s",
			strlen(cwd), strlen(exarg), cwd, exarg, winid, action, uri ?: "");
	g_free(cwd);

	int lock = open(ipcpath("lock"), O_RDONLY | O_CREAT, S_IRUSR);
	flock(lock, LOCK_EX);

	if (ipcsend("main", sendstr)) exit(0);

	//start main
	histdir = g_build_filename(
			g_get_user_cache_dir(), fullname, "history", NULL);
	g_set_prgname(fullname);
	gtk_init(0, NULL);
	checkconf(NULL);
	ipcwatch("main");

	close(lock);

	if (g_key_file_get_boolean(conf, "boot",
				"unsetGTK_OVERLAY_SCROLLING", NULL))
		g_unsetenv("GTK_OVERLAY_SCROLLING");

	GtkCssProvider *cssp = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssp,
			"tooltip *{padding:0}menuitem{padding:.2em}", -1, NULL);
	gtk_style_context_add_provider_for_screen(
			gdk_display_get_default_screen(gdk_display_get_default()),
			(void *)cssp, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(cssp);

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

	if (_run(NULL, action, uri, cwd, *exarg ? exarg : NULL))
		gtk_main();
	else
		exit(1);
	exit(0);
}
