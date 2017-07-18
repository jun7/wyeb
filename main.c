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
#include <gdk/gdkx.h>
#include <math.h>

#define APPNAME  "wyebrowser"
#define APP      "wyeb"
#define MIMEOPEN "mimeopen -n %s"

#define DSET "set;"
#define DIST 4

#define LASTWIN ((Win *)*wins->pdata)
#define URI(win) (webkit_web_view_get_uri(win->kit))

static gchar *fullname = APPNAME; //+suffix
#include "general.c"

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

	Mselect    = 256,

	Mopen      = 512,
	Mopennew   = 1024,
	Mfind      = 2048,
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
	gchar  *overset;

	//draw
	gdouble lastx;
	gdouble lasty;
	gchar  *msg;
	bool    smallmsg;

	//hittestresult
	gchar  *link;
	gchar  *linklabel;
	gchar  *image;
	gchar  *media;
	bool    oneditable;

	//misc
	gint    cursorx;
	gint    cursory;
	gchar  *spawn;
	gchar  *lastfind;
	bool    scheme;
	GTlsCertificateFlags tlserr;
//	gint    backwinnum;
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
	WebKitDownload *dl;
	gchar  *name;
	const gchar *dispname;
	guint64 len;
	bool    res;
	bool    finished;
} DLWin;


//@global
static gchar     *suffix = "";
static GPtrArray *wins = NULL;
static GPtrArray *dlwins = NULL;

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

//static bool ephemeral = false;

static WebKitWebContext *ctx = NULL;

typedef struct {
	gchar *group;
	gchar *key;
	gchar *val;
	gchar *desc;
} Conf;
Conf dconf[] = {
	{"all"   , "editor"       , MIMEOPEN,
		"editor=xterm -e nano %s\n"
		"editor=gvim --servername wyeb --remote-silent \"%s\""
	},
	{"all"   , "mdeditor"     , ""},
	{"all"   , "diropener"    , MIMEOPEN},
	{"all"   , "generator"    , "markdown %s"},

	{"all"   , "hintkeys"     , HINTKEYS},
	{"all"   , "keybindswaps" , "",
			"keybindswaps=Xx;ZZ;zZ ->if typed x: x to X, if Z: Z to Z"},

	{"all"   , "winwidth"     , "1000"},
	{"all"   , "winheight"    , "1000"},
	{"all"   , "zoom"         , "1.000"},

	{"all"   , "dlwinback"    , "false"},
	{"all"   , "dlwinclosemsec","3000"},
	{"all"   , "msgmsec"      , "400"},

	{"boot"  , "enablefavicon", "false"},
	{"boot"  , "extensionargs", "adblock:true;"},
//	{"all"   , "configreload" , "true",
//			"reload last window when whiteblack.conf or reldomain are changed"},

	{"search", "d"            , "https://duckduckgo.com/?q=%s"},
	{"search", "g"            , "https://www.google.com/search?q=%s"},
	{"search", "u"            , "http://www.urbandictionary.com/define.php?term=%s"},

	{"set:script", "enable-javascript", "true"},
	{"set:image" , "auto-load-images" , "true"},

	{DSET    , "search"           , "https://www.google.com/search?q=%s"},
	{DSET    , "usercss"          , "user.css"},
//	{DSET    , "loadsightedimages", "false"},
	{DSET    , "reldomaindataonly", "false"},
	{DSET    , "reldomaincutheads", "www.;wiki.;bbs.;developer."},
	{DSET    , "showblocked"      , "false"},
	{DSET    , "mdlbtnlinkaction" , "openback"},
	{DSET    , "mdlbtn2winlist"   , "false"},
	{DSET    , "newwinhandle"     , "normal",
			"newwinhandle=notnew | ignore | back | normal"},
	{DSET    , "hjkl2allowkeys"   , "false",
			"hjkl's default are scrolls, not allow keys"},
	{DSET    , "linkformat"       , "[%.40s](%s)"},
	{DSET    , "scriptdialog"     , "true"},

	//changes
	//{DSET      , "auto-load-images" , "false"},
	//{DSET      , "enable-plugins"   , "false"},
	//{DSET      , "enable-java"      , "false"},
	//{DSET      , "enable-fullscreen", "false"},

//	{"all"    , "nostorewebcontext", "false"}, //ephemeral
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
	g_free(str);
	str = g_key_file_get_string(conf, "all", key, NULL);
	return str;
}
static gchar *getset(Win *win, gchar *key)
{
	return  g_object_get_data(win->seto, key);
}
static bool getsetbool(Win *win, gchar *key)
{
	return strcmp(getset(win, key), "true") == 0;
}

static gchar *usage =
	"Usage: "APP" [[[suffix] action|\"\"] uri|arg|\"\"]\n"
	"  suffix: Process ID.\n"
	"    It added to all directories conf, cache and etc.\n"
	"  action: Such as new(default), open, opennew ...\n"
	"    Except 'new' and some, actions are sent to a window last focused.\n"
	;

static gchar *mainmdstr = 
"<!-- this is text/markdown -->\n"
"<meta charset=utf8>\n"
"Key:\n"
"**e**: Edit this page;\n"
"**E**: Edit main config file;\n"
"**c**: Open config directory;\n"
"**m**: Show this page;\n"
"**b**: Add title and URI of a page opened to this page;\n"
"\n"
"If **e**,**E**,**c** don't work, open 'main.conf' in\n"
"config directory/'"APPNAME"' and edit '"MIMEOPEN"' values.\n"
"If you haven't any gui editor or filer, set them like 'xterm -e nano %s'.\n"
"\n"
"For other keys, see [help](wyeb:help) assigned '**:**'.\n"
"Since this application is inspired from dwb and luakit,\n"
"usage is similar to those,\n"
"\n"
"<form style=display:inline method=get "
	"action=http://google.com/search>\n"
"	<input name=q /> google\n"
"</form>\n"
"<form style=display:inline method=get "
	"action=http://urbandictionary.com/define.php>\n"
"	<input name=term /> urban dictionary\n"
"</form>\n"
"\n"
"<style>\n"
" .links a{\n"
"  background: linear-gradient(to right top, #ddf, white, white);\n"
"  color: #109; padding: 0 .3em; border-radius: .2em;\n"
"  text-decoration: none;\n"
" }\n"
"</style>\n"
"<div class=links style=line-height:1.4;>\n"
"\n"
"[WYEBrowser](https://github.com/jun7/wyeb)\n"
"[WYEBAdblock](https://github.com/jun7/wyebadblock)\n"
"[Arch Linux](https://www.archlinux.org/)\n"
"[dwb - ArchWiki](https://wiki.archlinux.org/index.php/dwb)\n"
;


//@misc
static const gchar *dldir()
{
	return g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD) ?:
		g_get_home_dir();
}
static bool isin(GPtrArray *ary, void *v)
{
	for (int i = 0; i < ary->len; i++)
		if (v == ary->pdata[i]) return true;
	return false;
}

static void quitif(bool force)
{
	if (!force && (wins->len != 0 || dlwins->len != 0)) return;

	gtk_main_quit();
}

static void alert(gchar *msg)
{
	if (LASTWIN)
	{
		GtkWidget *dialog = gtk_message_dialog_new(
				LASTWIN->win,
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"%s", msg);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	} else {
		g_error("alert: %s\n", msg);
	}
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
static bool historycb(Win *win)
{
	if (win && !isin(wins, win)) return false;

#define MAXSIZE 22222
	static gchar *current = NULL;
	static gint currenti = -1;
	static gint logsize = 0;

	if (!logdir ||
			!g_file_test(logdir, G_FILE_TEST_EXISTS))
	{
		if (!logdir)
			logdir = g_build_filename(
				g_get_user_cache_dir(), fullname, "history", NULL);
		_mkdirif(logdir, false);

		currenti = -1;
		logsize = 0;
		for (gchar **file = logs; *file; file++)
		{
			currenti++;
			g_free(current);
			current = g_build_filename(logdir, *file, NULL);

			if (!g_file_test(current, G_FILE_TEST_EXISTS))
				break;

			struct stat info;
			stat(current, &info);
			logsize = info.st_size;
			if (logsize < MAXSIZE)
				break;
		}
	}

	if (!win) return false;

	gchar tstr[99];
	time_t t = time(NULL);
	strftime(tstr, sizeof(tstr), "%T/%d/%b/%y", localtime(&t));
	gchar *str = g_strdup_printf("%s %s %s", tstr, URI(win),
			webkit_web_view_get_title(win->kit) ?: "");

	static gchar *last = NULL;
	if (last && strcmp(str + 18, last + 18) == 0)
	{
		g_free(str);
		return false;
	}
	g_free(last);
	last = str;


	append(current, str);

	logsize += strlen(str) + 1;

	if (logsize > MAXSIZE)
	{
		currenti++;
		if (currenti >= logfnum)
			currenti = 0;

		g_free(current);
		current = g_build_filename(logdir, logs[currenti], NULL);
		
		FILE *f = fopen(current, "w");
		fclose(f);

		logsize = 0;
	}

	return false;
}
static void addhistory(Win *win)
{
	const gchar *uri = URI(win);
	if (!uri || g_str_has_prefix(uri, APP":")) return;

	g_idle_add((GSourceFunc)historycb, win);
}
static void removehistory()
{
	historycb(NULL);
	for (gchar **file = logs; *file; file++)
	{
		gchar *tmp = g_build_filename(logdir, *file, NULL);
		remove(tmp);
		g_free(tmp);
	}
	g_free(logdir);
	logdir = NULL;
}

static guint msgfunc = 0;
static bool clearmsgcb(Win *win)
{
	if (isin(wins, win))
	{
		g_free(win->msg);
		win->msg = NULL;
		gtk_widget_queue_draw(win->winw);
	}

	msgfunc = 0;
	return false;
}
static void _showmsg(Win *win, gchar *msg, bool small)
{
	if (msgfunc) g_source_remove(msgfunc);
	g_free(win->msg);
	win->msg = msg;
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
	if(!ipcsend(
#if SHARED
				"ext",
#else
				win->pageid,
#endif
				arg) &&
			!win->crashed && !alerted)
	{
		alert("Failed to communicate with the Web Extension.\n"
				"Make sure ext.so is in "EXTENSION_DIR".");
		alerted = true;
	}

	g_free(arg);
}
typedef struct {
	Win   *win;
	Coms   type;
	gchar *args;
} Send;
static bool senddelaycb(Send *s)
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
		if (strcmp(pageid, ((Win *)wins->pdata[i])->pageid) == 0)
			return wins->pdata[i];
	return NULL;
}

static guint reloadfunc = 0;
static bool reloadlastcb()
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
				if (strcmp(g_value_get_string(&gv), v) == 0) {
					g_free(v);
					continue;;
				}
				g_value_set_string(&gv, v);
				g_free(v);
			} else
				g_key_file_set_string(kf, group, key, g_value_get_string(&gv));
			break;
		default:
			if (strcmp(key, "hardware-acceleration-policy") == 0) {
				if (set) {
					gchar *str = g_key_file_get_string(kf, group, key, NULL);

					WebKitHardwareAccelerationPolicy v;
					if (strcmp(str, "ON_DEMAND") == 0)
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND;
					else if (strcmp(str, "ALWAYS") == 0)
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS;
					else if (strcmp(str, "NEVER") == 0)
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER;

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
		if (strcmp(next->data, name) == 0)
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
	{
		g_free(path);
		path = NULL;
	}
	return path;
}
static void setcss(Win *win, gchar *namesstr)
{
	WebKitUserContentManager *cmgr =
		webkit_web_view_get_user_content_manager(win->kit);
	webkit_user_content_manager_remove_all_style_sheets(cmgr);

	gchar **names = g_strsplit(namesstr, ";", -1);

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
		if (dconf[i].group != DSET) continue;
		gchar *key = dconf[i].key;
		if (!g_key_file_has_key(kf, group, key, NULL)) continue;

		gchar *val = g_key_file_get_string(kf, group, key, NULL);

		if (strcmp(key, "usercss") == 0 &&
			g_strcmp0(g_object_get_data(win->seto, key), val) != 0)
		{
			setcss(win, val);
		}
		g_object_set_data_full(win->seto, key, val, g_free);
	}
}
static void getkitprops(GObject *obj, GKeyFile *kf, gchar *group)
{
	_kitprops(false, obj, kf, group);
}
static bool _seturiconf(Win *win, const gchar* uri)
{
	if (uri == NULL || strlen(uri) == 0) return false;
	
	bool ret = false;

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
			g_free(win->lasturiconf);
			win->lasturiconf = g_strdup(uri);
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
		g_free(win->lasturiconf);
		win->lasturiconf = NULL;
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
			if (strcmp(c.group, "search") == 0) continue;
			if (g_str_has_prefix(c.group, "set:")) continue;
		}

		if (!g_key_file_has_key(kf, c.group, c.key, NULL))
			g_key_file_set_value(kf, c.group, c.key, c.val);

		if (isnew && c.desc)
			g_key_file_set_comment(conf, c.group, c.key, c.desc, NULL);
	}

	if (!isnew) return;

	//sample and comment
	g_key_file_set_comment(conf, "set;", NULL, "Default of 'set's.", NULL);

	const gchar *sample = "uri:^https?://(www\\.)?google\\..*";

	g_key_file_set_boolean(conf, sample, "enable-javascript", false);
	g_key_file_set_comment(conf, sample, NULL,
			"After 'uri:' is regular expressions for 'set'.\n"
			"preferential order of sections: Last > First > 'set;'"
			, NULL);

	sample = "uri:^a-zA-Z0-9*";

	g_key_file_set_string(conf, sample, "reg", "^[^a-zA-Z0-9]*$");
	g_key_file_set_comment(conf, sample, "reg",
			"Use reg if a regular expression has []."
			, NULL);

	g_key_file_set_string(conf, sample, "sets", "image;sctipt");
	g_key_file_set_comment(conf, sample, "sets",
			"include other sets." , NULL);

	//fill vals not set
	if (wins && LASTWIN)
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
		{
			for (int i = 0; i < wins->len; i++)
			{
				Win *win = wins->pdata[i];
				const gchar *str = URI(win);
				if (g_str_has_prefix(str, APP":main"))
					webkit_web_view_reload(win->kit);
			}
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
		}
		else
			getdconf(new, false);

		if (conf) g_key_file_free(conf);
		conf = new;

		if (wins)
			for (int i = 0; i < wins->len; i++)
				resetconf(wins->pdata[i], true);
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

			g_free(path);
			path = path2conf(name);

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
		win->tlserr ? "!TLS has problems! " : "",
		suffix,
		*suffix ? "| " : "",
		win->overset ?: "",
		win->overset ? "| " : "",
		wtitle,
		uri);

	gtk_window_set_title(win->win, title);
	g_free(title);
}
static bool winlist(Win *win, guint type, cairo_t *cr); //declaration
static void _modechanged(Win *win)
{
	Modes last = win->lastmode;
	win->lastmode = win->mode;

	switch (last) {
	case Mfind:
		g_free(win->lastfind);
		win->lastfind = g_strdup(gtk_entry_get_text(win->ent));
	case Mopen:
	case Mopennew:
		gtk_entry_set_text(win->ent, "");

		gtk_widget_hide(win->entw);
		gtk_widget_grab_focus(win->kitw);
		break;

	case Mselect:
		gtk_widget_queue_draw(win->winw);
		gdk_window_set_cursor(gtk_widget_get_window(win->winw), NULL);
//		gtk_widget_set_sensitive(win->kitw, true);
		break;
	case Minsert: break;

	case Mhintspawn:
		g_free(win->spawn);
		win->spawn = NULL;
	case Mhint:
	case Mhintopen:
	case Mhintnew:
	case Mhintback:
	case Mhintdl:
	case Mhintbkmrk:
		send(win, Crm, NULL);
		break;
	}

	//into
	Coms com = 0;
	switch (win->mode) {
	case Mfind:
		if (win->crashed)
		{
			win->mode = Mnormal;
			break;
		}
		gtk_entry_set_text(win->ent, win->lastfind ?: "");
		g_free(win->lastfind);
		win->lastfind = NULL;
	case Mopen:
	case Mopennew:
		gtk_widget_show(win->entw);
		gtk_widget_grab_focus(win->entw);
		break;

	case Mselect:
//		gtk_widget_set_sensitive(win->kitw, false);
		winlist(win, 2, NULL);
		gtk_widget_queue_draw(win->winw);
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

		if (win->crashed)
			win->mode = Mnormal;
		else
			send(win, com, confcstr("hintkeys"));

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

	case Mselect:
		settitle(win, "-- LIST MODE --");

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
	default:
		settitle(win, "-- UNKNOWN MODE --");
	}

	//normal mode
	if (win->mode != Mnormal) return;

	if (win->link)
	{
		gchar *str = g_strconcat("Link: ", win->link, NULL);
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
static void openuri(Win *win, const gchar *str)
{
	win->userreq = true;
	if (!str || strlen(str) == 0)
		str = APP":main";

	if (
		g_str_has_prefix(str, "http:") ||
		g_str_has_prefix(str, "https:") ||
		g_str_has_prefix(str, "file:") ||
		g_str_has_prefix(str, "about:") ||
		g_str_has_prefix(str, APP":")
	) {
		webkit_web_view_load_uri(win->kit, str);
		return;
	}
	if (g_str_has_prefix(str, "javascript:")) {
		webkit_web_view_run_javascript(win->kit, str + 11, NULL, NULL, NULL);
		return;
	}

	gchar *uri = NULL;

	gchar **stra = g_strsplit(str, " ", 2);

	if (*stra && stra[1])
	{
		gchar **kv = g_key_file_get_keys(conf, "search", NULL, NULL);
		for (gchar **key = kv; *key; key++) {

			if (strcmp(stra[0], *key) == 0) {
				char *esc = g_uri_escape_string (stra[1], NULL, true);
				uri = g_strdup_printf(
					g_key_file_get_string(conf, "search", *key, NULL),
					esc);
				g_free(esc);

				g_free(win->lastfind);
				win->lastfind = g_strdup(stra[1]);

				g_strfreev(kv);
				goto out;
			}
		}
		g_strfreev(kv);
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
	} else if (dsearch = getset(win, "search")) {
		char *esc = g_uri_escape_string (str, NULL, true);
		uri = g_strdup_printf(dsearch, esc);
		g_free(esc);

		g_free(win->lastfind);
		win->lastfind = g_strdup(str);
	}

	if (!uri) uri = g_strdup(str);
out:
	g_strfreev(stra);

	webkit_web_view_load_uri(win->kit, uri);
	g_free(uri);
}

static void spawnwithenv(Win *win, gchar* path, bool ispath)
{
	gchar **argv;
	if (ispath)
	{
		argv = g_new0(gchar*, 2);
		argv[0] = g_strdup(path);
	} else {
		//_showmsg(win, g_strdup_printf("Run %s", path), false);
		GError *err = NULL;
		if (!g_shell_parse_argv(path, NULL, &argv, &err))
		{
			alert(err->message);
			g_error_free(err);
			return;
		}
	}

	gchar *dir = ispath ? g_path_get_dirname(path) : NULL;

	gchar **envp = g_get_environ();
	envp = g_environ_setenv(envp, "SUFFIX" , suffix, true);
	envp = g_environ_setenv(envp, "ISCALLBACK",
			win->spawn ? "1" : "0", true);
	gchar buf[9];
	snprintf(buf, 9, "%d", wins->len);
	envp = g_environ_setenv(envp, "WINSLEN", buf, true);
	envp = g_environ_setenv(envp, "WINID"  , win->pageid, true);
	envp = g_environ_setenv(envp, "URI"    , URI(win), true);
	envp = g_environ_setenv(envp, "LINK_OR_URI", URI(win), true);

	const gchar *title = webkit_web_view_get_title(win->kit);
	if (!title) title = URI(win);
	envp = g_environ_setenv(envp, "TITLE" , title, true);

	gchar *cbtext;
	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_PRIMARY));
	if (cbtext) {
		envp = g_environ_setenv(envp, "PRIMARY"  , cbtext, true);
		envp = g_environ_setenv(envp, "SELECTION", cbtext, true);
	}

	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_SECONDARY));
	if (cbtext)
		envp = g_environ_setenv(envp, "SECONDARY", cbtext, true);

	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
	if (cbtext)
		envp = g_environ_setenv(envp, "CLIPBOARD", cbtext, true);

	if (win->link)
	{
		envp = g_environ_setenv(envp, "LINK", win->link, true);
		envp = g_environ_setenv(envp, "LINK_OR_URI", win->link, true);
	}
	if (win->linklabel)
	{
		envp = g_environ_setenv(envp, "LINKLABEL", win->linklabel, true);
	}

	if (win->media)
		envp = g_environ_setenv(envp, "MEDIA", win->media, true);
	if (win->image)
		envp = g_environ_setenv(envp, "IMAGE", win->image, true);

	if (win->media || win->image || win->link)
		envp = g_environ_setenv(envp, "MEDIA_IMAGE_LINK",
				win->media ?: win->image ?: win->link, true);

	GPid child_pid;
	GError *err = NULL;
	if (!g_spawn_async(
				dir, argv, envp,
				ispath ? G_SPAWN_DEFAULT : G_SPAWN_SEARCH_PATH,
				NULL, NULL, &child_pid, &err))
	{
		alert(err->message);
		g_error_free(err);
	}
	g_spawn_close_pid(child_pid);

	g_strfreev(envp);
	g_strfreev(argv);
	g_free(dir);
}

static void scroll(Win *win, gint x, gint y)
{
	GdkEvent *e = gdk_event_new(GDK_SCROLL);
	GdkEventScroll *es = (GdkEventScroll *)e;

	es->window = gtk_widget_get_window(win->kitw);
	g_object_ref(es->window);
	es->send_event = true;
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
static void sendkey(Win *win, guint key)
{
	GdkEvent *e = gdk_event_new(GDK_KEY_PRESS);
	GdkEventKey *ek = (GdkEventKey *)e;

	ek->window = gtk_widget_get_window(win->kitw);
	g_object_ref(ek->window);
	ek->send_event = true;
	//ek->time   = GDK_CURRENT_TIME;
	ek->keyval = key;
	//k->state = ke->state & ~GDK_MODIFIER_MASK;

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
	if (!editor || *editor == '\0')
		editor = confcstr("editor");
	if (*editor == '\0')
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
		gdk_window_lower(gtk_widget_get_window(win->winw));

		gtk_window_present(((Win *)dwins->pdata[1])->win);
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
bool winlist(Win *win, guint type, cairo_t *cr)
//type: 0: none 1:present 2:setcursor, and GDK_KEY_Down ... GDK_KEY_Right
{
	//for window select mode
	if (win->mode != Mselect) return false;
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
		if (w > h)
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

	Win *titlew = NULL;
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
		gchar *str = g_strdup_printf(getset(win, "linkformat"),
				escttl ?: uri, uri);

		append(mdpath, str);

		g_free(str);
		g_free(escttl);
	}
	else
		append(mdpath, NULL);

	showmsg(win, "Added");
	checkconf(false);
}

void resourcecb(GObject *srco, GAsyncResult *res, gpointer p)
{
	gsize len;
	guchar *data = webkit_web_resource_get_data_finish(
			(WebKitWebResource *)srco, res, &len, NULL);

	gchar *esc = g_strescape(data, "");
	gchar *cmd = g_strdup_printf(p, esc);
	g_spawn_command_line_async(cmd, NULL);

	g_free(cmd);
	g_free(esc);
	g_free(data);
	g_free(p);
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

//normal /'pvz' are left
	{"toinsert"      , 'i', 0},
	{"toinsertinput" , 'I', 0, "To Insert Mode with focus of first input"},

	{"tohint"        , 'f', 0},
	{"tohintnew"     , 'F', 0},
	{"tohintback"    , 't', 0},
	{"tohintbookmark", 'T', 0},
	{"tohintdl"      , 'd', 0, "dl is Download"},

	{"showdldir"     , 'D', 0},

	{"yankuri"       , 'y', 0, "Clipboard"},
	{"bookmark"      , 'b', 0},
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
	{"winlist"       , 'x', 0},

	{"back"          , 'H', 0},
	{"forward"       , 'L', 0},
	{"stop"          , 's', 0},
	{"reload"        , 'r', 0},
	{"reloadbypass"  , 'R', 0, "reload bypass cache"},

	{"find"          , '/', 0},
	{"findnext"      , 'n', 0},
	{"findprev"      , 'N', 0},
	{"findselection" , '*', 0},

	{"open"          , 'o', 0},
	{"opennew"       , 'w', 0, "New window"},
	{"edituri"       , 'O', 0},
	{"editurinew"    , 'W', 0},

//	{"showsource"    , 'S', 0}, //not good
	{"showhelp"      , ':', 0},
	{"showhistory"   , 'M', 0},
	{"showmainpage"  , 'm', 0},

	{"clearallwebsitedata", 'C', GDK_CONTROL_MASK},
	{"edit"          , 'e', 0},//normaly conf, if in main edit mainpage.
	{"editconf"      , 'E', 0},
	{"openconfigdir" , 'c', 0},

	{"setscript"     , 's', GDK_CONTROL_MASK, "Use the 'set:script' section"},
	{"setimage"      , 'i', GDK_CONTROL_MASK, "set:image"},
	{"unset"         , 'u', 0},

	{"addwhitelist"  , 'a', 0,
		"URIs blocked by reldomain limitation and black list are added to whiteblack.conf"},
	{"addblacklist"  , 'A', 0, "URIs loaded"},

//insert
//	{"editor"        , 'e', GDK_CONTROL_MASK},

//nokey
	{"set"           , 0, 0, "Use 'set:' + arg section of main.conf"},
	{"new"           , 0, 0},
	{"newclipboard"  , 0, 0, "Open [arg + ' ' +] clipboard text in a new window."},
	{"newselection"  , 0, 0, "Open [arg + ' ' +] selection ..."},
	{"newsecondary"  , 0, 0, "Open [arg + ' ' +] secondaly ..."},
	{"findclipboard" , 0, 0},
	{"findsecondary" , 0, 0},

	{"tohintopen"    , 0, 0},
	{"openback"      , 0, 0},

	{"download"      , 0, 0},
	{"bookmarkthis"  , 0, 0},
	{"bookmarklinkor", 0, 0},
	{"showmsg"       , 0, 0},
	{"tohintcallback", 0, 0,
		"arg is called with environment variables selected by hint."},
	{"sourcecallback", 0, 0},
//	{"headercallback"  , 0, 0}, //todo

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
static Win *newwin(const gchar *uri, Win *cbwin, Win *relwin, bool back);
static bool run(Win *win, gchar* action, const gchar *arg)
{
#define Z(str, func) if (strcmp(action, str) == 0) {func; goto out;}
	if (action == NULL) return false;
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
	Z("blocked"    ,
			_showmsg(win, g_strdup_printf("Blocked %s", arg), true);
			return true;)
	Z("openeditor" , openeditor(win, arg, NULL))
	Z("reloadlast" , reloadlast())

	if (strcmp(action, "hintret") == 0)
	{
		switch (win->mode) {
		case Mhintnew:
			action = "opennew"     ; break;
		case Mhintback:
			showmsg(win, "Opened");
			action = "openback"    ; break;
		case Mhintdl:
			action = "download"    ; break;
		case Mhintbkmrk:
			action = "bookmarkthis"; break;
		case Mhintopen:
			action = "open"        ; break;

		case Mhintspawn:
			g_free(win->link);
			g_free(win->image);
			g_free(win->media);
			win->link = win->image = win->media = NULL;

			switch (*arg) {
			case 'l':
				win->link  = g_strdup(arg + 1); break;
			case 'i':
				win->image = g_strdup(arg + 1); break;
			case 'm':
				win->media = g_strdup(arg + 1); break;
			}
			action = "spawn";
			break;

		case Mhint:
			break;
		}
		win->mode = Mnormal;
	}
	Z("spawn"      , spawnwithenv(win, win->spawn, false))

	if (arg != NULL) {
		Z("find"  ,
				g_free(win->lastfind);
				win->lastfind = g_strdup(arg);
				webkit_find_controller_search(win->findct, arg,
					WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
					WEBKIT_FIND_OPTIONS_WRAP_AROUND, G_MAXUINT))

		Z("open"   , openuri(win, arg))
		Z("opennew", newwin(arg, NULL, win, false))

		//nokey
		Z("openback", newwin(arg, NULL, win, true))
		Z("download", webkit_web_view_download_uri(win->kit, arg))
	}

	Z("tonormal"    , win->mode = Mnormal)

	Z("toinsert"    , win->mode = Minsert)
	Z("toinsertinput", win->mode = Minsert; send(win, Ctext, NULL))

	Z("tohint"      , win->mode = Mhint)
	Z("tohintopen"  , win->mode = Mhintopen)
	Z("tohintnew"   , win->mode = Mhintnew)
	Z("tohintback"  , win->mode = Mhintback)
	Z("tohintdl"    , win->mode = Mhintdl)
	Z("tohintbookmark", win->mode = Mhintbkmrk)
	Z("tohintcallback", win->mode = Mhintspawn; win->spawn = g_strdup(arg))

	Z("showdldir"   ,
		command(win, confcstr("diropener"), dldir());
	)

	Z("yankuri"     ,
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), URI(win), -1);
		showmsg(win, "URI is yanked to clipboard")
	)
	Z("bookmarkthis", addlink(win, NULL, arg))
	Z("bookmark"    , addlink(win, webkit_web_view_get_title(win->kit), URI(win)))
	Z("bookmarkbreak", addlink(win, NULL, NULL))
	Z("bookmarklinkor",
		if (win->link)
			addlink(win, win->linklabel, win->link);
		else if (arg)
			addlink(win, NULL, arg);
		else
			return run(win, "bookmark", NULL);
	)

	Z("quit"        , gtk_widget_destroy(win->winw); return false)
	Z("quitall"     , quitif(true))

	bool arrow = getsetbool(win, "hjkl2allowkeys");
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
				win->mode = Mselect;
			else
				showmsg(win, "No other window");
	)

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
			gtk_entry_set_text(win->ent, URI(win)))
	Z("opennew"     , win->mode = Mopennew)
	Z("editurinew"  ,
			win->mode = Mopennew;
			gtk_entry_set_text(win->ent, URI(win)))

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

			showmsg(win, action);
	)
	Z("edit"        , openconf(win, false))
	Z("editconf"    , openconf(win, true))
	Z("openconfigdir",
			gchar *dir = g_path_get_dirname(confpath);
			command(win, confcstr("diropener"), dir);
			g_free(dir);
	)

	Z("setscript"   , return run(win, "set", "script"))
	Z("setimage"    , return run(win, "set", "image"))
	Z("unset"       , return run(win, "set", NULL))
	Z("set"         ,
			gchar *set;
			if (g_strcmp0(win->overset, arg) == 0)
				set = NULL;
			else
				set = arg ? g_strdup(arg) : NULL;

			g_free(win->overset);
			win->overset = set;

			resetconf(win, true);
	)

	Z("addwhitelist", send(win, Cwhite, "white"))
	Z("addblacklist", send(win, Cwhite, "black"))

	Z("showmsg"     , showmsg(win, arg))
	Z("sourcecallback",
		WebKitWebResource *res = webkit_web_view_get_main_resource(win->kit);
		webkit_web_resource_get_data(res, NULL, resourcecb, g_strdup(arg));
	)
//	Z("headercallback",)

	if (win->mode == Minsert)
		Z("editor", )//todo

	D(Not Yet! %s, action)
	return false;

#undef Z
out:
	update(win);
	return true;
}


//@win and cbs:

//static void eventname(GdkEvent *e, gchar *str)
//{
//	static int motion = 0;
//	if (e->type == GDK_MOTION_NOTIFY)
//		motion++;
//	else
//		motion = 0;
//
//	if (motion > 2) return;
//
//	D("%s", str)
//
//	DENUM(e->type, GDK_NOTHING                     )
//	DENUM(e->type, GDK_DELETE                      )
//	DENUM(e->type, GDK_DESTROY                     )
//	DENUM(e->type, GDK_EXPOSE                      )
//	DENUM(e->type, GDK_MOTION_NOTIFY               )
//	DENUM(e->type, GDK_BUTTON_PRESS                )
//	DENUM(e->type, GDK_2BUTTON_PRESS               )
//	DENUM(e->type, GDK_DOUBLE_BUTTON_PRESS         )
//	DENUM(e->type, GDK_3BUTTON_PRESS               )
//	DENUM(e->type, GDK_TRIPLE_BUTTON_PRESS         )
//	DENUM(e->type, GDK_BUTTON_RELEASE              )
//	DENUM(e->type, GDK_KEY_PRESS                   )
//	DENUM(e->type, GDK_KEY_RELEASE                 )
//	DENUM(e->type, GDK_ENTER_NOTIFY                )
//	DENUM(e->type, GDK_LEAVE_NOTIFY                )
//	DENUM(e->type, GDK_FOCUS_CHANGE                )
//	DENUM(e->type, GDK_CONFIGURE                   )
//	DENUM(e->type, GDK_MAP                         )
//	DENUM(e->type, GDK_UNMAP                       )
//	DENUM(e->type, GDK_PROPERTY_NOTIFY             )
//	DENUM(e->type, GDK_SELECTION_CLEAR             )
//	DENUM(e->type, GDK_SELECTION_REQUEST           )
//	DENUM(e->type, GDK_SELECTION_NOTIFY            )
//	DENUM(e->type, GDK_PROXIMITY_IN                )
//	DENUM(e->type, GDK_PROXIMITY_OUT               )
//	DENUM(e->type, GDK_DRAG_ENTER                  )
//	DENUM(e->type, GDK_DRAG_LEAVE                  )
//	DENUM(e->type, GDK_DRAG_MOTION                 )
//	DENUM(e->type, GDK_DRAG_STATUS                 )
//	DENUM(e->type, GDK_DROP_START                  )
//	DENUM(e->type, GDK_DROP_FINISHED               )
//	DENUM(e->type, GDK_CLIENT_EVENT                )
//	DENUM(e->type, GDK_VISIBILITY_NOTIFY           )
//	DENUM(e->type, GDK_SCROLL                      )
//	DENUM(e->type, GDK_WINDOW_STATE                )
//	DENUM(e->type, GDK_SETTING                     )
//	DENUM(e->type, GDK_OWNER_CHANGE                )
//	DENUM(e->type, GDK_GRAB_BROKEN                 )
//	DENUM(e->type, GDK_DAMAGE                      )
//	DENUM(e->type, GDK_TOUCH_BEGIN                 )
//	DENUM(e->type, GDK_TOUCH_UPDATE                )
//	DENUM(e->type, GDK_TOUCH_END                   )
//	DENUM(e->type, GDK_TOUCH_CANCEL                )
//	DENUM(e->type, GDK_TOUCHPAD_SWIPE              )
//	DENUM(e->type, GDK_TOUCHPAD_PINCH              )
//	DENUM(e->type, GDK_PAD_BUTTON_PRESS            )
//	DENUM(e->type, GDK_PAD_BUTTON_RELEASE          )
//	DENUM(e->type, GDK_PAD_RING                    )
//	DENUM(e->type, GDK_PAD_STRIP                   )
//	DENUM(e->type, GDK_PAD_GROUP_MODE              )
//	DENUM(e->type, GDK_EVENT_LAST                  )
//
//}
//static gboolean eventwcb(GtkWidget *w, GdkEvent *e, Win *win)
//{ eventname(e, "w"); return false; }
//static gboolean eventcb(GtkWidget *w, GdkEvent *e, Win *win)
//{ eventname(e, "k"); return false; }


static bool focuscb(Win *win)
{
	g_ptr_array_remove(wins, win);
	g_ptr_array_insert(wins, 0, win);
	checkconf(false);
	if (!webkit_web_view_is_loading(win->kit))
		addhistory(win);
	return false;
}
static bool focusoutcb(Win *win)
{
	if (win->mode == Mselect)
		tonormal(win);
	return false;
}
static bool drawcb(GtkWidget *ww, cairo_t *cr, Win *win)
{
	static guint csize = 0;
	if (!csize) csize = gdk_display_get_default_cursor_size(
					gtk_widget_get_display(win->winw));

	if (win->lastx || win->lastx)
	{
		gdouble x = win->lastx, y = win->lasty, size = csize / 6;
		cairo_set_source_rgba(cr, .9, .0, .0, .3);
		cairo_set_line_width(cr, 2);
		cairo_move_to(cr, x - size, y - size);
		cairo_line_to(cr, x + size, y + size);
		cairo_move_to(cr, x - size, y + size);
		cairo_line_to(cr, x + size, y - size);
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
static void dldestroycb(DLWin *win)
{
	if (!win->finished)
	{
		win->finished = true;
		webkit_download_cancel(win->dl);
	}

	g_ptr_array_remove(dlwins, win);

	g_free(win->name);
	g_free(win);

	quitif(false);
}
static bool dlclosecb(DLWin *win)
{
	if (isin(dlwins, win))
		gtk_widget_destroy(win->winw);

	return false;
}
static void dlfincb(DLWin *win)
{
	if (!isin(dlwins, win)) return;

	win->finished = true;
	g_timeout_add(confint("dlwinclosemsec"), (GSourceFunc)dlclosecb, win);

	gchar *title;
	if (win->res)
	{
		title = g_strdup_printf("DL: Finished: %s", win->dispname);
		gtk_progress_bar_set_fraction(win->prog, 1);
	}
	else
		title = g_strdup_printf("DL: Failed: %s", win->dispname);

	gtk_window_set_title(win->win, title);
	g_free(title);
}
static void dlfailcb(DLWin *win)
{
	if (win->finished) return; //cancelled

	win->finished = true;

	gchar *title;
	title = g_strdup_printf("DL: Failed: %d%%", win->dispname);
	gtk_window_set_title(win->win, title);
	g_free(title);
}
static void dldatacb(DLWin *win)
{
	//gdouble webkit_download_get_elapsed_time (WebKitDownload *download);

	gdouble p = webkit_download_get_estimated_progress(win->dl);
	gtk_progress_bar_set_fraction(win->prog, p);

	gchar *title = g_strdup_printf(
			"DL: %.2f%%: %s ", (p * 100), win->dispname);
	gtk_window_set_title(win->win, title);
	g_free(title);
}
static void addlabel(DLWin *win, const gchar *str)
{
	GtkWidget *lbl = gtk_label_new(str);
	gtk_label_set_selectable((GtkLabel *)lbl, true);
	gtk_box_pack_start(win->box, lbl, true, true, 0);
	gtk_label_set_ellipsize((GtkLabel *)lbl, PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_show_all(lbl);
}
//static void dlrescb(DLWin *win) {}
static void dldestcb(DLWin *win)
{
	gchar *fn = g_filename_from_uri(
			webkit_download_get_destination(win->dl), NULL, NULL);
	gchar *pathstr = g_strdup_printf("=>  %s", fn);

	addlabel(win, pathstr);

	g_free(fn);
	g_free(pathstr);
}
static bool dldecidecb(WebKitDownload *pdl, gchar *name, DLWin *win)
{
	const gchar *base = dldir();
	gchar *path = g_build_filename(base, name, NULL);

	gchar *check = g_path_get_dirname(path);
	if (strcmp(base, check) != 0)
	{
		g_free(path);
		path = g_build_filename(base, name = "noname", NULL);
	}
	g_free(check);

	gchar *back = g_strdup(path);
	for (int i = 2; g_file_test(path, G_FILE_TEST_EXISTS); i++)
	{
		g_free(path);
		path = g_strdup_printf("%s.%d", back, i);
	}
	g_free(back);

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
static bool dlkeycb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (GDK_KEY_q == ek->keyval) gtk_widget_destroy(w);
	return false;
}
static bool accept_focus(GtkWindow *w)
{
	gtk_window_set_accept_focus(w, true);
	return false;
}
static void downloadcb(WebKitWebContext *ctx, WebKitDownload *pdl)
{
	DLWin *win = g_new0(DLWin, 1);
	win->dl    = pdl;

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
	SIGW(o, "failed"             , dlfailcb  , win);
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
		g_timeout_add(200, (GSourceFunc)accept_focus, win->win);
	} else {
		gtk_widget_show_all(win->winw);
	}

	addlabel(win, webkit_uri_request_get_uri(webkit_download_get_request(pdl)));
	g_ptr_array_insert(dlwins, 0, win);
}


//@uri scheme
gchar *schemedata(WebKitWebView *kit, const gchar *path)
{
	Win *win = g_object_get_data(G_OBJECT(kit), "win");
	win->scheme = true;

	gchar *data = NULL;

	if (g_str_has_prefix(path, "main")) {
		preparemd();

		gchar *cmd = g_strdup_printf(confcstr("generator"), mdpath);
		g_spawn_command_line_sync(cmd, &data, NULL, NULL, NULL);
		g_free(cmd);
	}
	if (g_str_has_prefix(path, "history")) {
		gchar *last;

		data = g_strdup(
			"<meta charset=utf8>\n"
			"<style>\n"
			"th {\n"
			" font-weight: normal; font-family: monospace; vertical-align: top;\n"
			" padding-right: .3em;\n"
			"}\n"
			"td {padding-bottom: .4em;}\n"
			"a {font-size: 100%; color:black; text-decoration: none;}\n"
			"span {\n"
			" font-size: 79%;\n"
			" color: #43a;\n"
			"}\n"
			"</style>\n"
			"<table>\n"
			);

		//log
		historycb(NULL);
		GSList *hist = NULL;
		gint start = 0;
		gint num = 0;
		__time_t mtime = 0;
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
				gchar **stra = g_strsplit(line, " ", 3);
				gchar *escpd = g_markup_escape_text(stra[2] ?: stra[1], -1);

				hist = g_slist_prepend(hist, g_strdup_printf(
							"<tr><th>%.11s</th>"
							"<td><a href=%s>%s<br><span>%s</span></a>\n",
							stra[0], stra[1], escpd, stra[1]));
				num++;

				g_free(escpd);
				g_strfreev(stra);
				g_free(line);
			}
			g_io_channel_unref(io);
			g_free(path);
		}

		if (num)
		{
			gchar *sv[num + 1];
			sv[num] = NULL;
			int i = 0;
			for (GSList *next = hist; next; next = next->next)
				sv[i++] = next->data;

			gchar *allhist = g_strjoinv("", sv);

			g_slist_free_full(hist, g_free);

			last = data;
			data = g_strconcat(data, allhist, NULL);
			g_free(last);
			g_free(allhist);
		} else {
			last = data;
			data = g_strconcat(data, "<p>No Data</p>", NULL);
			g_free(last);
		}

//			"<p>Back List<p>\n\n"
//		WebKitBackForwardList *list =
//			webkit_web_view_get_back_forward_list(kit);
//
//		GList *bl = webkit_back_forward_list_get_back_list(list);
//
//		if (bl)
//			for (GList *n = bl; n; n = n->next)
//			{
//				gchar *tmp = g_strdup_printf(
//					"<p>%s<br><a href=\"%s\">%s</a><p>\n",
//					webkit_back_forward_list_item_get_title(n->data) ?: "-",
//					webkit_back_forward_list_item_get_uri  (n->data),
//					webkit_back_forward_list_item_get_uri  (n->data));
//
//				last = data;
//				data = g_strconcat(data, tmp, NULL);
//				g_free(last);
//				g_free(tmp);
//			}
//		else
//		{
//			last = data;
//			data = g_strconcat(data, "<b>-- first page --</b>", NULL);
//			g_free(last);
//		}
//		g_list_free(bl);

	}
	if (g_str_has_prefix(path, "help")) {
		data = g_strdup_printf(
			"<pre style=\"font-size: large\">\n"
			"mouse:\n"
			"  rocker gesture:\n"
			"    left press and       -        right: back\n"
			"    left press and move right and right: forward\n"
			"    left press and move up    and right: raise bottom window and close\n"
			"    left press and move down  and right: raise next   window and close\n"
			"  middle button:\n"
			"    on a link           : new background window\n"
			"    on free space       : raise bottom window / show window list\n"
			"    press and move left : raise bottom window / show window list\n"
			"                                              / if mdlbtn2winlist: true\n"
			"    press and move right: raise next   window\n"
			"    press and move up   : go to top\n"
			"    press and move down : go to bottom\n"
			"\n"
			"context-menu:\n"
			"  You can add your own script to context-menu. See 'menu' dir in\n"
			"  the config dir, or click 'addMenu' in the context-menu. SUFFIX,\n"
			"  ISCALLBACK, WINSLEN, WINID, URI, TITLE, PRIMARY/SELECTION,\n"
			"  SECONDARY, CLIPBORAD, LINK, LINK_OR_URI, LINKLABEL, MEDIA, IMAGE,\n"
			"  and MEDIA_IMAGE_LINK are set as environment variables. Available\n"
			"  actions are in 'key:' section below. Of course it supports dir\n"
			"  and '.'. '.' hides it from menu but still available in the accels.\n"
			"accels:\n"
			"  You can add your own keys to access context-menu items we added.\n"
			"  To add Ctrl-Z to GtkAccelMap, insert '&lt;Primary&gt;&lt;Shift&gt;z' to the\n"
			"  last \"\" in the file 'accels' in the conf directory assigned 'c'\n"
			"  key, and remeve the ';' at the beginning of line. alt is &lt;Alt&gt;.\n"
			"\n"
			"key:\n"
			"#%d - is ctrl\n"
			"#(null) is only for script\n"
			, GDK_CONTROL_MASK);

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
	}

	if (!data)
		data = g_strdup("<h1>Empty</h1>");

	return data;
}
static void schemecb(WebKitURISchemeRequest *req, gpointer p)
{
	const gchar *path = webkit_uri_scheme_request_get_path(req);
	WebKitWebView *kit = webkit_uri_scheme_request_get_web_view(req);

	gchar *data = schemedata(kit, path);

	gsize len = strlen(data);
	GInputStream *st = g_memory_input_stream_new_from_data(data, len, g_free);

	webkit_uri_scheme_request_finish(req, st, len, "text/html");
	g_object_unref(st);
}


//@kit's cbs
static void destroycb(Win *win)
{
	g_ptr_array_remove(wins, win);

	quitif(false);

#if SHARED
	send(win, Cfree, NULL);
#endif

	g_free(win->pageid);
	g_free(win->lasturiconf);
	g_free(win->overset);
	g_free(win->msg);
	g_free(win->link);
	g_free(win->linklabel);
	g_free(win->image);
	g_free(win->media);
	g_free(win->spawn);
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
}
static void favclearcb(Win *win)
{ gtk_window_set_icon(win->win, NULL); }

//static void soupMessageHeadersForeachFunc(const char *name,
//                                  const char *value,
//                                  gpointer user_data)
//{
//	D(head %s %s, name, value)
//}
//void resloadcb(WebKitWebView     *web_view,
//               WebKitWebResource *resource,
//               WebKitURIRequest  *request,
//               Win *win)
//{
//return;
//DD(resloadcb)
//SoupMessageHeaders *head =
//	webkit_uri_request_get_http_headers(request);
//if (head)
//soup_message_headers_foreach (head,
//                              soupMessageHeadersForeachFunc,
//                              NULL);
//
//	D(resload uri:%s,webkit_web_resource_get_uri (resource))
//}


static bool keycb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (ek->is_modifier) return false;

	gchar *action = ke2name(ek);

	if (action && strcmp(action, "tonormal") == 0)
	{
		if (win->mode == Mnormal)
		{
			send(win, Cblur, NULL);
			webkit_find_controller_search_finish(win->findct);
			return false;
		}

		tonormal(win);
	}


	if (win->mode == Minsert)
		return false;

	if (win->mode & Mhint)
	{
		gchar *arg = g_strdup_printf("%c%s", ek->keyval, confcstr("hintkeys"));
		send(win, Ckey, arg);
		g_free(arg);
		return true;
	}

	if (win->mode == Mselect)
	{
#define Z(str, func) if (action && strcmp(action, str) == 0) {func;}
		Z("scrolldown"  , winlist(win, GDK_KEY_Down , NULL))
		Z("scrollup"    , winlist(win, GDK_KEY_Up   , NULL))
		Z("scrollleft"  , winlist(win, GDK_KEY_Left , NULL))
		Z("scrollright" , winlist(win, GDK_KEY_Right, NULL))
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
static void setresult(Win *win, WebKitHitTestResult *htr)
{
	g_free(win->image);
	g_free(win->media);
	g_free(win->link);
	g_free(win->linklabel);

	win->image = win->media = win->link = win->linklabel = NULL;
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
static void targetcb(
		WebKitWebView *w,
		WebKitHitTestResult *htr,
		guint m,
		Win *win)
{
	setresult(win, htr);
	update(win);
}
static bool cancelcontext = false;
static bool cancelbtn1r = false;
static GdkEvent *pendingmiddlee = NULL;
static bool btncb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	static bool block = false;
	win->userreq = true;

	if (e->type != GDK_BUTTON_PRESS) return false;

	if (win->mode == Mselect)
	{
		win->cursorx = win->cursory = 0;
		if (e->button == 1 && winlist(win, 0, NULL))
			return winlist(win, 1, NULL);

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
		//workaround
		//for lacking of target change event when btn event happens with focus in;
		//now this is also for back from mselect mode may be
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
			gdouble
				deltax = (e->x - win->lastx) ,
				deltay = e->y - win->lasty;

			if (MAX(abs(deltax), abs(deltay)) < DIST * 3)
			{ //default
				run(win, "back", NULL);
			}
			else if (abs(deltax) > abs(deltay)) {
				if (deltax < 0) //left
					run(win, "back", NULL);
				else //right
					run(win, "forward", NULL);
			} else {
				if (inwins(win, NULL, true) < 1)
				{
					if (strcmp(APP":main", URI(win)) == 0)
						return run(win, "quit", NULL);
					else {
						run(win, "showmainpage", NULL);
						showmsg(win, "Last Window");
					}
				}
				else if (deltay < 0) //up
				{
					run(win, "prevwin", NULL);
					return run(win, "quit", NULL);
				}
				else //down
				{
					run(win, "nextwin", NULL);
					return run(win, "quit", NULL);
				}
			}
			cancelcontext = true;
			cancelbtn1r = true;
		}
		break;
	}

	return false;
}
static bool btnrcb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	//D(release button %d, e->button)

	switch (e->button) {
	case 1:
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->winw);

		if (cancelbtn1r) {
			cancelbtn1r = false;
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

		if (MAX(abs(deltax), abs(deltay)) < DIST)
		{ //default
			if (win->oneditable)
			{
				((GdkEventButton *)pendingmiddlee)->send_event = true;
				gtk_widget_event(win->kitw, pendingmiddlee);
				gdk_event_free(pendingmiddlee);
				pendingmiddlee = NULL;
			}
			else if (win->link)
			{
				run(win,
					getset(win, "mdlbtnlinkaction"),
					win->link);
			}
			else if (gtk_window_is_active(win->win))
			{
				if (getsetbool(win, "mdlbtn2winlist"))
					run(win, "winlist", NULL);
				else
					run(win, "prevwin", NULL);
			}
		}
		else if (abs(deltax) > abs(deltay)) {
			if (deltax < 0) //left
				if (getsetbool(win, "mdlbtn2winlist"))
					run(win, "winlist", NULL);
				else
					run(win, "prevwin", NULL);
			else //right
				run(win, "nextwin", NULL);
		} else {
			if (deltay < 0) //up
				run(win, "top", NULL);
			else //down
				run(win, "bottom", NULL);
		}

		gtk_widget_queue_draw(win->winw);

		return true;
	}
	}

	update(win);

	return false;
}
static bool entercb(GtkWidget *w, GdkEventCrossing *e, Win *win)
{ //for checking drag end with button1
	if (
			!(e->state & GDK_BUTTON1_MASK) &&
			win->lastx + win->lasty)
	{
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->winw);
	}
	update(win);
}
static bool motioncb(GtkWidget *w, GdkEventMotion *e, Win *win)
{
	if (win->mode == Mselect)
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
static bool policycb(
		WebKitWebView *v,
		WebKitPolicyDecision *dec,
		WebKitPolicyDecisionType type,
		Win *win)
{
	if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return false;

	if (webkit_response_policy_decision_is_mime_type_supported(
				(WebKitResponsePolicyDecision *)dec))
		webkit_policy_decision_use(dec);
	else
		webkit_policy_decision_download(dec);
	return true;
}
static GtkWidget *createcb(Win *win)
{
	gchar *handle = getset(win, "newwinhandle");
	Win *new = NULL;

	if      (strcmp(handle, "notnew") == 0)
		return win->kitw;
	else if (strcmp(handle, "ignore") == 0)
		return NULL;
	else if (strcmp(handle, "back") == 0)
		new = newwin(NULL, win, win, true);
	else
		new = newwin(NULL, win, win, false);

	return new->kitw;
}
static void closecb(Win *win)
{
	gtk_widget_destroy(win->winw);
}
static bool sdialogcb(Win *win)
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
static void loadcb(WebKitWebView *k, WebKitLoadEvent event, Win *win)
{
	win->crashed = false;
	switch (event) {
	case WEBKIT_LOAD_STARTED:
		//D(WEBKIT_LOAD_STARTED %s, URI(win))
		win->scheme = false;
		setresult(win, NULL);
		tonormal(win);
		if (win->userreq) {
			win->userreq = false; //currently not used
		}
		resetconf(win, false);
		sendstart(win);

		break;
	case WEBKIT_LOAD_REDIRECTED:
		//D(WEBKIT_LOAD_REDIRECTED %s, URI(win))
		resetconf(win, false);
		sendstart(win);

		break;
	case WEBKIT_LOAD_COMMITTED:
		//D(WEBKIT_LOAD_COMMITED %s, URI(win))
		if (!win->scheme && g_str_has_prefix(URI(win), APP":"))
			webkit_web_view_reload(win->kit);

		send(win, Con, NULL);

		if (!webkit_web_view_get_tls_info(win->kit, NULL, &win->tlserr))
			win->tlserr = 0;

		break;
	case WEBKIT_LOAD_FINISHED:
		//DD(WEBKIT_LOAD_FINISHED)
		favcb(win);
		addhistory(win);
		break;
	}
}

static bool loadfailtlcb(Win *win)
{
	DD(load fail tls)
	return false;
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
	spawnwithenv(LASTWIN, ai->path, true);
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
		fprintf(f, script, dir);
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
		addscript(dir, ".openNewSrcURI"      ,
				APP" \"$SUFFIX\" tohintcallback "
				"'bash -c \""APP" \\\"$SUFFIX\\\" opennew \\\"$MEDIA_IMAGE_LINK\\\"\"'");
		addscript(dir, "0addMenu"         , "mimeopen -n %s");
		addscript(dir, "0bookmark"        , APP" \"$SUFFIX\" bookmarklinkor \"\"");
		addscript(dir, "0duplicate"       , APP" \"$SUFFIX\" opennew $URI");
		addscript(dir, "0history"         , APP" \"$SUFFIX\" showhistory \"\"");
		addscript(dir, "0windowList"      , APP" \"$SUFFIX\" winlist \"\"");
		addscript(dir, "1main"            , APP" \"$SUFFIX\" open "APP":main");
		addscript(dir, "3---"             , "");
		addscript(dir, "3openClipboard"   , APP" \"$SUFFIX\" open \"$CLIPBOARD\"");
		addscript(dir, "3openClipboardNew", APP" \"$SUFFIX\" opennew \"$CLIPBOARD\"");
		addscript(dir, "3openSelection"   , APP" \"$SUFFIX\" open \"$PRIMARY\"");
		addscript(dir, "3openSelectionNew", APP" \"$SUFFIX\" opennew \"$PRIMARY\"");
		addscript(dir, "6searchDictionary", APP" \"$SUFFIX\" open \"u $PRIMARY\"");
		addscript(dir, "9---"             , "");

		gchar *tmp = g_strdup_printf(APP" \"$SUFFIX\" sourcecallback "
				"\"bash -c \\\"echo -e \\\\\\\"%%%%s\\\\\\\" >> \\\"%s/"
				APP"-source\\\"\\\"\"",
				dldir());
		addscript(dir, "9saveHTMLSource2DLdir", tmp);
		g_free(tmp);

		addscript(dir, "z---"             , "");
		addscript(dir, "zchromium"        , "chromium $LINK_OR_URI");
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
static bool contextcb(WebKitWebView *web_view,
		WebKitContextMenu   *menu,
		GdkEvent            *e,
		WebKitHitTestResult *htr,
		Win                 *win
		)
{
	if (cancelcontext) {
		cancelcontext = false;
		return true;
	}

	setresult(win, htr);
	makemenu(menu);

	//GtkAction * webkit_context_menu_item_get_action(WebKitContextMenuItem *item);
	//GtkWidget * gtk_action_create_menu_item(GtkAction *action);
	return false;
}


//@entry
static bool focusincb(Win *win)
{
	if (gtk_widget_get_visible(win->entw))
		tonormal(win);
	return false;
}
static bool entkeycb(GtkWidget *w, GdkEventKey *ke, Win *win)
{
	switch (ke->keyval) {
	case GDK_KEY_m:
		if (!(ke->state & GDK_CONTROL_MASK))
			return false;

	case GDK_KEY_KP_Enter:
	case GDK_KEY_Return:
		{
			const gchar *text = gtk_entry_get_text(win->ent);
			gchar *action = NULL;
			switch (win->mode) {
			case Mfind:
				if (!win->lastfind || strcmp(win->lastfind, text) != 0)
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
	default:
		return false;
	}
	return true;
}
static bool textcb(Win *win)
{
	if (win->mode == Mfind && gtk_widget_get_visible(win->entw)) {
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
}
static void foundcb(Win *win)
{
	_showmsg(win, NULL, false); //clear
}


Win *newwin(const gchar *uri, Win *cbwin, Win *relwin, bool back)
{
	Win *win = g_new0(Win, 1);
	win->userreq = true;

	win->winw = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gint w, h;
	if (relwin)
		gtk_window_get_size(relwin->win, &w, &h);
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

	WebKitUserContentManager *cmgr =
		webkit_user_content_manager_new();

	if (cbwin)
		win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
			"related-view", cbwin->kit, "user-content-manager", cmgr, NULL);
	else {
		if (!ctx) {
			gchar *data =
				g_build_filename(g_get_user_data_dir(), fullname, NULL);
			gchar *cache =
				g_build_filename(g_get_user_cache_dir(), fullname, NULL);

			WebKitWebsiteDataManager * mgr = webkit_website_data_manager_new(
					"base-data-directory", data,
					"base-cache-directory", cache,
					NULL);


			g_free(data);
			g_free(cache);

			ctx = webkit_web_context_new_with_website_data_manager(mgr);

			//cookie  //have to be after ctx are made
			WebKitCookieManager *cookiemgr =
				webkit_website_data_manager_get_cookie_manager(mgr);

			//we assume cookies are conf
			gchar *cookiefile = path2conf("cookies");
			webkit_cookie_manager_set_persistent_storage(cookiemgr,
					cookiefile, WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
			g_free(cookiefile);

			webkit_cookie_manager_set_accept_policy(cookiemgr,
					WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);

#if ! SHARED
			webkit_web_context_set_process_model(ctx,
					WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);
#endif

			gchar **argv = g_key_file_get_string_list(
					conf, "boot", "extensionargs", NULL, NULL);
			gchar *args = g_strjoinv(";", argv);
			g_strfreev(argv);
			gchar *udata = g_strconcat(args, ";", fullname, NULL);
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

			webkit_web_context_register_uri_scheme(
					ctx, APP, schemecb, NULL, NULL);

			if (g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL))
			{
				gchar *favdir =
					g_build_filename(g_get_user_cache_dir(), fullname, "favicon", NULL);
				webkit_web_context_set_favicon_database_directory(ctx, favdir);
				g_free(favdir);
			}
		}

		//win->kitw = webkit_web_view_new_with_context(ctx);
		win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
			"web-context", ctx, "user-content-manager", cmgr, NULL);
	}
	g_object_set_data(win->kito, "win", win); //for context event

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
	{
		SIGW(o, "notify::favicon"      , favcb     , win);
		SIGW(o, "notify::uri"          , favclearcb, win);
	}
//	SIG( o, "resource-load-started", resloadcb, win);

	SIG( o, "key-press-event"      , keycb     , win);
	SIG( o, "mouse-target-changed" , targetcb  , win);
	SIG( o, "button-press-event"   , btncb     , win);
	SIG( o, "button-release-event" , btnrcb    , win);
	SIG( o, "enter-notify-event"   , entercb   , win);
	SIG( o, "motion-notify-event"  , motioncb  , win);

	SIG( o, "decide-policy"        , policycb  , win);
	SIGW(o, "create"               , createcb  , win);
	SIGW(o, "close"                , closecb   , win);
	SIGW(o, "script-dialog"        , sdialogcb , win);
	SIG( o, "load-changed"         , loadcb    , win);
	SIGW(o, "load-failed-with-tls-errors", loadfailtlcb, win);

	SIG( o, "context-menu"         , contextcb , win);

	//for entry
	SIGW(o, "focus-in-event"       , focusincb , win);

	win->findct = webkit_web_view_get_find_controller(win->kit);
	SIGW(win->findct, "failed-to-find-text", findfailedcb, win);
	SIGW(win->findct, "found-text"         , foundcb     , win);

//	SIG( o, "event"               , eventcb   , win);
//	SIG( win->wino, "event"       , eventwcb   , win);

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
		"progressbar *{min-height: 0.6em;}", -1, NULL); //bigger
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


	gtk_widget_grab_focus(win->kitw);

	gtk_widget_show_all(win->winw);
	gtk_widget_hide(win->entw);
	gtk_widget_hide(win->progw);

	gtk_window_present(
			back && LASTWIN ? LASTWIN->win : win->win);

	win->pageid = g_strdup_printf("%lu", webkit_web_view_get_page_id(win->kit));
	g_ptr_array_add(wins, win);

	if (!cbwin)
		openuri(win, uri);

	return win;
}


//@main
void ipccb(const gchar *line)
{
	gchar **args = g_strsplit(line, ":", 3);

	gchar *arg = args[2];
	if (*arg == '\0') arg = NULL;

	if (strcmp(args[0], "0") == 0)
		run(LASTWIN, args[1], arg);
	else
	{
		Win *win = winbyid(args[0]);
		if (win)
			run(win, args[1], arg);
	}

	g_strfreev(args);
}
int main(int argc, char **argv)
{
#if DEBUG
	g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
#endif

	if (
		argc > 4 || (
			argc == 2 && (
				strcmp(argv[1], "-h") == 0 ||
				strcmp(argv[1], "--help") == 0
			)
		)
	) {
		g_print(usage);
		exit(0);
	}
	if (argc == 4)
		suffix = argv[1];
	fullname = g_strconcat(APPNAME, suffix, NULL);

	gchar *action = argc > 2 ? argv[argc - 2] : "new";
	gchar *uri    = argc > 1 ? argv[argc - 1] : NULL;

	if (*action == '\0') action = "new";
	if (uri && *uri == '\0') uri = NULL;

	gchar *sendstr = g_strconcat("0:", action, ":", uri, NULL);
	if (ipcsend("main", sendstr)) exit(0);
	g_free(sendstr);

	//start main
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

	if (run(NULL, action, uri))
		gtk_main();
	else
		exit(1);
	exit(0);
}
