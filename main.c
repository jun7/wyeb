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
#include <regex.h>
#include <gdk/gdkx.h>

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
	Mhintspawn = 2 + 128, //for script //not used

	Msource    = 256, //not used
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
	gchar  *starturi;
	gchar  *lasturiconf;
	gchar  *overset;

	//draw
	gdouble lastx;
	gdouble lasty;
	gchar  *msg;

	//hittestresult
	gchar  *link;
	gchar  *linklabel;
	bool    oneditable;

	//misc
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
} Conf;
Conf dconf[] = {
	//{"main"    , "editor"       , "xterm -e nano %s"},
	{"main"    , "editor"       , MIMEOPEN},
	{"main"    , "mdeditor"     , ""},
	{"main"    , "diropener"    , MIMEOPEN},
	{"main"    , "generator"    , "markdown %s"},

	{"main"    , "hintkeys"     , HINTKEYS},
	{"main"    , "keybindswaps" , ""},

	{"main"    , "winwidth"     , "1000"},
	{"main"    , "winheight"    , "1000"},
	{"main"    , "zoom"         , "1.000"},

	{"main"    , "dlwinlower"   , "true"},
	{"main"    , "dlwinclosemsec","3000"},
	{"main"    , "msgmsec"      , "400"},

	{"search"  , "d"            , "https://duckduckgo.com/?q=%s"},
	{"search"  , "g"            , "https://www.google.com/search?q=%s"},
	{"search"  , "u"            , "http://www.urbandictionary.com/define.php?term=%s"},

	{"set:script", "enable-javascript", "true"},
	{"set:image" , "auto-load-images" , "true"},

	{DSET      , "search"           , "https://www.google.com/search?q=%s"},
	{DSET      , "usercss"          , "user.css"},
//	{DSET      , "loadsightedimages", "false"},
	{DSET      , "mdlbtnlinkaction" , "openback"},
	{DSET      , "newwinhandle"     , "normal"},
	{DSET      , "linkformat"       , "[%.40s](%s)"},

	//changes
	//{DSET      , "auto-load-images" , "false"},
	//{DSET      , "enable-plugins"   , "false"},
	//{DSET      , "enable-java"      , "false"},
	//{DSET      , "enable-fullscreen", "false"},

//	{"main"    , "nostorewebcontext", "false"}, //ephemeral
};
static bool confbool(gchar *key)
{ return g_key_file_get_boolean(conf, "main", key, NULL); }
static gint confint(gchar *key)
{ return g_key_file_get_integer(conf, "main", key, NULL); }
static gdouble confdouble(gchar *key)
{ return g_key_file_get_double(conf, "main", key, NULL); }
static gchar *confcstr(gchar *key)
{//return is static string
	static gchar *str = NULL;
	g_free(str);
	str = g_key_file_get_string(conf, "main", key, NULL);
	return str;
}
static gchar *getset(Win *win, gchar *key)
{
	return  g_object_get_data(win->seto, key);
}

static gchar *usage =
	"Usage: "APP" [[[suffix] action|\"\"] uri|\"\"]\n"
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
"If you haven't any gui editor or filer, set they like 'xterm -e nano %s'.\n"
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
"[WYEBrowser](https://github.com/jun7/wyeb/wiki)\n"
"[Arch Linux](https://www.archlinux.org/)\n"
"[dwb - ArchWiki](https://wiki.archlinux.org/index.php/dwb)\n"
;


//@misc
static bool getctime(gchar *path, __time_t *ctime)
{
	struct stat info;
	g_assert((stat(path, &info) == 0));

	if (*ctime == info.st_ctime) return false;
	*ctime = info.st_ctime;
	return true;
}

static gchar *path2conf(const gchar *name)
{
	return g_build_filename(
			g_get_user_config_dir(), fullname, name, NULL);
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
static bool history(gchar *str)
{
#define MAXSIZE 33333
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

	if (!str) return false;
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

	g_free(str);
	return false;
}
static void removehistory()
{
	history(NULL);
	for (gchar **file = logs; *file; file++)
	{
		gchar *tmp = g_build_filename(logdir, *file, NULL);
		remove(tmp);
		g_free(tmp);
	}
	g_free(logdir);
	logdir = NULL;
}

static bool clearmsgcb(Win *win)
{
	if (isin(wins, win))
		{
			g_free(win->msg);
			win->msg = NULL;
			gtk_widget_queue_draw(win->winw);
		}
	return false;
}
static void showmsg(Win *win, gchar *msg) //msg is freed
{
	g_free(win->msg);
	win->msg = msg;
	g_timeout_add(confint("msgmsec"), (GSourceFunc)clearmsgcb, win);
	gtk_widget_queue_draw(win->winw);
}

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
	s->win = win;
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


//@conf
static void checkconf(bool monitor); //declaration
static void monitorcb(
		GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e)
{
	if (e == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
		checkconf(true);
}
static void monitor(gchar *path)
{
	GFile *gf = g_file_new_for_path(path);
	GFileMonitor *gm = g_file_monitor_file(
			gf, G_FILE_MONITOR_NONE, NULL, NULL);
	SIG(gm, "changed", monitorcb, NULL);

	g_object_unref(gf);
}
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
	}
}
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

		monitor(path);
	}
	if (!exists)
	{
		g_free(path);
		path = NULL;
	}
	return path;
}
static void setcss(Win *win, gchar *name)
{
	WebKitUserContentManager *cmgr =
		webkit_web_view_get_user_content_manager(win->kit);
	webkit_user_content_manager_remove_all_style_sheets(cmgr);

	gchar *path = addcss(name);
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
static void getprops(GObject *obj, GKeyFile *kf, gchar *group)
{
	_kitprops(false, obj, kf, group);
}
static bool _seturiconf(Win *win, const gchar* uri)
{
	if (uri == NULL || strlen(uri) == 0) return false;
	
	bool ret = false;
	gsize len;
	gchar **groups = g_key_file_get_groups(conf, &len);

	for (int i = 0; i < len; i++)
	{
		gchar *g = groups[i];
		if (!g_str_has_prefix(g, "uri:")) continue;

		gchar *tofree = NULL;
		if (g_key_file_has_key(conf, g, "reg", NULL))
		{
			g = tofree = g_key_file_get_string(conf, g, "reg", NULL);
		} else {
			g += 4;
		}

		regex_t reg;
		regcomp(&reg, g, REG_EXTENDED | REG_NOSUB);

		if (regexec(&reg, uri, 0, NULL, 0) == 0) {
			setprops(win, conf, groups[i]);
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
static bool seturiconf(Win *win)
{
	return _seturiconf(win, URI(win));
}
static void resetconf(Win *win, bool force)
{
	if (win->lasturiconf || force)
	{
		g_free(win->lasturiconf);
		win->lasturiconf = NULL;
		setprops(win, conf, DSET);
	}

	if (seturiconf(win))
		_seturiconf(win, win->starturi);

	if (win->overset) {
		gchar *setstr = g_strdup_printf("set:%s", win->overset);
		setprops(win, conf, setstr);
		g_free(setstr);
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
	}

	if (!isnew) return;

	//sample and comment
	g_key_file_set_comment(conf, "main", "keybindswaps",
			"keybindswaps=Xx;ZZ;zZ ->if typed x: x to X, if Z: Z to Z"
			, NULL);

	g_key_file_set_comment(conf, "set;", NULL, "Default of 'set's.", NULL);

	g_key_file_set_comment(conf, "set;", "newwinhandle",
			"newwinhandle=notnew | ignore | back | normal" , NULL);



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
		getprops(LASTWIN->seto, kf, DSET);
	else {
		WebKitSettings *set = webkit_settings_new();
		getprops((GObject *)set, kf, DSET);
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
		monitor(confpath);
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

				if (!exists || strcmp(us, name) == 0)
					setcss(lw, name);
			}
		}
		g_free(path);
	}
}
static void preparemd()
{
	bool first = false;
	if (!mdpath)
	{
		first = true;
		mdpath = path2conf("mainpage.md");
		monitor(mdpath);
	}

	if (g_file_test(mdpath, G_FILE_TEST_EXISTS))
	{
		if (first) getctime(mdpath, &mdtime);
		return;
	}

	GFile *gf = g_file_new_for_path(mdpath);

	GFileOutputStream *o  = g_file_create(
			gf, G_FILE_CREATE_PRIVATE, NULL, NULL);
	g_output_stream_write((GOutputStream *)o,
			mainmdstr, strlen(mainmdstr), NULL, NULL);
	g_object_unref(o);

	g_object_unref(gf);

	getctime(mdpath, &mdtime);
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

	case Minsert:
		break;

	case Mhint     :
	case Mhintopen :
	case Mhintnew  :
	case Mhintback :
	case Mhintdl   :
	case Mhintbkmrk:
	case Mhintspawn: //todo not used
		send(win, Crm, NULL);
		break;
	}

	Coms com = 0;
	//into
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

	case Mhint     :
		com = Cclick;
	case Mhintopen :
	case Mhintnew  :
	case Mhintback :
		if (!com) com = Clink;
	case Mhintdl   :
	case Mhintbkmrk:
	case Mhintspawn:
		if (!com) com = Curi;

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
	case Minsert:
		settitle(win, "-- INSERT MODE --");
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

	if (win->mode != Mnormal) return;

	settitle(win, NULL);
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
		gsize len;
		gchar **kv = g_key_file_get_keys(conf, "search", &len, NULL);
		for (int i = 0; i < len; i++) {
			gchar *key = kv[i];

			if (strcmp(stra[0], key) == 0) {
				uri = g_strdup_printf(
					g_key_file_get_string(conf, "search", key, NULL),
					stra[1]);

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
		uri = g_strdup_printf(dsearch, str);

		g_free(win->lastfind);
		win->lastfind = g_strdup(str);
	}

	if (!uri) uri = g_strdup(str);
out:
	g_strfreev(stra);

	webkit_web_view_load_uri(win->kit, uri);
	g_free(uri);
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
	showmsg(win, g_strdup_printf("Run '%s' with '%s'", cmd, arg));

	gchar *str = g_strdup_printf(cmd, arg);
	GError *err = NULL;
	if (!g_spawn_command_line_async(str, &err))
	{
		alert(err->message);
		g_error_free(err);
	}
	g_free(str);
}
static void openeditor(Win *win, bool shift)
{
	gchar *path;
	const gchar *editor = NULL;

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
		showmsg(win, g_strdup("No config"));
		return;
	} else {
		path = confpath;
		if (!shift)
		{
			gchar *name = g_strdup_printf("uri:^%s", uri);
			if (!g_key_file_has_group(conf, name))
			{
				gchar *str = g_strdup_printf("\n[%s]", name);
				append(path, str);
				g_free(str);
			}
			g_free(name);
		}
	}

	if (!editor || *editor == '\0')
		editor = confcstr("editor");
	if (*editor == '\0')
		editor = MIMEOPEN;

	command(win, editor, path);
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
		showmsg(win, g_strdup("No other window"));
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

	showmsg(win, g_strdup("added"));
	checkconf(false);
}


//@actions
typedef struct {
	gchar *name;
	guint key;
	guint mask;
} Keybind;
static Keybind dkeys[]= {
//every mode
	{"tonormal"      , GDK_KEY_Escape, 0},
	{"tonormal"      , '[', GDK_CONTROL_MASK},

//normal /'zpJK' are left
	{"toinsert"      , 'i', 0},
	{"toinsertinput" , 'I', 0},

	{"tohint"        , 'f', 0},
	{"tohintopen"    , 'F', GDK_CONTROL_MASK},
	{"tohintnew"     , 'F', 0},
	{"tohintback"    , 'f', GDK_CONTROL_MASK},
	{"tohintdl"      , 'd', 0},
	{"tohintbookmark", 'B', 0},

	{"yankuri"       , 'y', 0},
	{"bookmark"      , 'b', 0},
	{"bookmarkbreak" , 'b', GDK_CONTROL_MASK},

	{"quit"          , 'q', 0},
	{"quitall"       , 'Q', 0},
//	{"quit"          , 'Z', 0},

	{"scrolldown"    , 'j', 0},
	{"scrollup"      , 'k', 0},
	{"scrollleft"    , 'h', 0},
	{"scrollright"   , 'l', 0},
	//{"arrowdown"     , 'j', GDK_CONTROL_MASK},
	//{"arrowup"       , 'k', GDK_CONTROL_MASK},
	//{"arrowleft"     , 'h', GDK_CONTROL_MASK}, //history
	//{"arrowright"    , 'l', GDK_CONTROL_MASK},

	{"top"           , 'g', 0},
	{"bottom"        , 'G', 0},
	{"zoomin"        , '+', 0},
	{"zoomout"       , '-', 0},
	{"zoomreset"     , '=', 0},

	//tab
	{"nextwin"       , 'J', 0},
	{"prevwin"       , 'K', 0},

	{"back"          , 'H', 0},
	{"forward"       , 'L', 0},
	{"stop"          , 's', 0},
	{"reload"        , 'r', 0},
	{"reloadbypass"  , 'R', 0},

	{"find"          , '/', 0},
	{"findnext"      , 'n', 0},
	{"findprev"      , 'N', 0},

	{"open"          , 'o', 0},
	{"opennew"       , 'w', 0},
	{"edituri"       , 'O', 0},
	{"editurinew"    , 'W', 0},

//	{"showsource"    , 'S', 0}, //not good
	{"showhelp"      , ':', 0},
	{"showhistory"   , 'h', GDK_CONTROL_MASK},
	{"showmainpage"  , 'm', 0},

	{"clearallwebsitedata", 'C', GDK_CONTROL_MASK},
	{"edit"          , 'e', 0},//normaly conf, if in main edit mainpage.
	{"editconf"      , 'E', 0},
	{"openconfigdir" , 'c', 0},

	{"setscript"     , 's', GDK_CONTROL_MASK},
	{"setimage"      , 'i', GDK_CONTROL_MASK},
	{"unset"         , 'u', 0},

//insert
//	{"editor"        , 'e', GDK_CONTROL_MASK},

//nokey
	{"set"           , 0, 0},
	{"new"           , 0, 0},
	{"openback"      , 0, 0},
	{"bookmarklinkor", 0, 0},

//todo pagelist
//	{"windowimage"   , 0, 0}, //pageid
//	{"windowlist"    , 0, 0}, //=>pageid uri title
//	{"present"       , 0, 0}, //pageid

//test
	{"test"          , 't', 0},
};
static gchar *ke2name(GdkEventKey *ke)
{
	guint key = ke->keyval;

	gchar **swaps = g_key_file_get_string_list(
			conf, "main", "keybindswaps", NULL, NULL);

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
#define Z(str, func) if (!strcmp(action, str)) {func; goto out;}
	if (action == NULL) return false;
	//nokey nowin
	Z("new", win = newwin(arg, NULL, NULL, false))

	if (win == NULL) return false;

	//internal
	if (!strcmp(action, "hintret"))
	{
		switch (win->mode) {
		case Mhintnew  :
			action = "opennew"     ; break;
		case Mhintback :
			action = "openback"    ; break;
		case Mhintdl   :
			action = "download"    ; break;
		case Mhintbkmrk:
			action = "bookmarkthis"; break;
		case Mhintopen :
			action = "open"        ; break;

		//todo not used
		case Mhintspawn:
		case Mhint     :
			break;
		}
		win->mode = Mnormal;
	}


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
	Z("tohintspawn" , win->mode = Mhintspawn) //nokey
	Z("tohintbookmark", win->mode = Mhintbkmrk)

	Z("yankuri"     ,
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), URI(win), -1);
		showmsg(win, g_strdup("uri is yanked to clipboard"))
	)
	Z("bookmarkthis", addlink(win, NULL, arg)) //internal
	Z("bookmark"    , addlink(win, webkit_web_view_get_title(win->kit), URI(win)))
	Z("bookmarkbreak", addlink(win, NULL, NULL))
	Z("bookmarklinkor",
		if (win->link)
			addlink(win, win->linklabel, win->link);
		else
			run(win, "bookmark", NULL);
	)

	Z("quit"        , gtk_widget_destroy(win->winw); return false)
	Z("quitall"     , quitif(true))

	Z("scrolldown"  , scroll(win, 0, 1))
	Z("scrollup"    , scroll(win, 0, -1))
	Z("scrollleft"  , scroll(win, -1, 0))
	Z("scrollright" , scroll(win, 1, 0))
	//Z("arrowdown"   , sendkey(win, GDK_KEY_Down))
	//Z("arrowup"     , sendkey(win, GDK_KEY_Up))
	//Z("arrowleft"   , sendkey(win, GDK_KEY_Left))
	//Z("arrowright"  , sendkey(win, GDK_KEY_Right))

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

	Z("back"        ,
			if (webkit_web_view_can_go_back(win->kit))
			webkit_web_view_go_back(win->kit);
			else
			showmsg(win, g_strdup("No Previous Page"))
	)
	Z("forward"     ,
			if (webkit_web_view_can_go_forward(win->kit))
			webkit_web_view_go_forward(win->kit);
			else
			showmsg(win, g_strdup("No Next Page"))
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

			showmsg(win, g_strdup(action));
	)
	Z("edit"        , openeditor(win, false))
	Z("editconf"    , openeditor(win, true))
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

	Z("test"  , )

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


static bool focuscb(Win *win) {
	checkconf(false);
	g_ptr_array_remove(wins, win);
	g_ptr_array_insert(wins, 0, win);
	return false;
}
static bool drawcb(GtkWidget *w, cairo_t *cr, Win *win)
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

		cairo_set_font_size(cr, csize * .8);

		cairo_set_source_rgba(cr, .9, .9, .9, .7);
		cairo_move_to(cr, csize, h - csize);
		cairo_show_text(cr, win->msg);

		cairo_set_source_rgba(cr, .9, .0, .9, .7);
		cairo_move_to(cr, csize + csize / 30, h - csize);
		cairo_show_text(cr, win->msg);
	}

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
	const gchar *base =
		g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD) ?: g_get_home_dir();
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
	if (GDK_KEY_m == ek->keyval) gtk_widget_destroy(w);
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

	if (confbool("dlwinlower") && LASTWIN &&
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
		history(NULL);
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
			"    left press and       -        right: back.\n"
			"    left press and move right and right: forward.\n"
			"    left press and move up    and right: raise bottom window and close.\n"
			"    left press and move down  and right: raise next   window and close.\n"
			"  middle button:\n"
			"    on a link           : new background window.\n"
			"    on free space       : raise bottom window.\n"
			"    press and move left : raise bottom window.\n"
			"    press and move right: raise next   window.\n"
			"    press and move up   : go to top.\n"
			"    press and move down : go to bottom.\n"
			"\n"
			"accels:\n"
			"  You can add your own keys to access context-menu items we added.\n"
			"  To add Ctrl-Z to GtkAccelMap, insert '&lt;Primary&gt;&lt;Shift&gt;z' to the\n"
			"  last \"\" in the file 'accels' in the conf directory assigned 'c' key,\n"
			"  and remeve the ';' of line head.\n"
			"\n"
			"key:\n"
			"#%d - is ctrl\n"
			, GDK_CONTROL_MASK);

		for (int i = 0; i < sizeof(dkeys) / sizeof(*dkeys); i++)
		{
			gchar *tmp = g_strdup_printf("%d - %-11s: %s\n",
					dkeys[i].mask, gdk_keyval_name(dkeys[i].key), dkeys[i].name);
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
	g_free(win->starturi);
	g_free(win->lasturiconf);
	g_free(win->overset);
	g_free(win->msg);
	g_free(win->link);
	g_free(win->linklabel);
	g_free(win->lastfind);
	g_free(win);
}
static void crashcb(Win *win)
{
	win->crashed = true;
	win->mode = Mnormal;
	update(win);
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
static void targetcb(
		WebKitWebView *w,
		WebKitHitTestResult *htr,
		guint m,
		Win *win)
{
	g_free(win->link);
	win->link = NULL;
	win->oneditable = webkit_hit_test_result_context_is_editable(htr);

	if (webkit_hit_test_result_context_is_link(htr))
	{
		win->link =
			g_strdup(webkit_hit_test_result_get_link_uri(htr));
		gchar *str = g_strconcat("Link: ", win->link, NULL);
		settitle(win, str);
		g_free(str);
	}
	else
		update(win);
}
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

		win->mode = Mnormal;
		update(win);
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


	win->userreq = true;

	if (!action)
		return false;

	run(win, action, NULL);

	return true;
}
static bool cancelcontext = false;
static bool cancelbtn1r = false;
static GdkEvent *pendingmiddlee = NULL;
static bool btncb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	static bool block = false;
	win->userreq = true;

	if (e->type != GDK_BUTTON_PRESS) return FALSE;

	if (win->oneditable)
		win->mode = Minsert;
	else
		win->mode = Mnormal;

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
				if (wins->len < 2)
					showmsg(win, g_strdup("Last Window"));
				else if (deltay < 0) //up
				{
					run(win, "prevwin", NULL);
					run(win, "quit", NULL);
				}
				else //down
				{
					run(win, "nextwin", NULL);
					run(win, "quit", NULL);
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
				run(win, "prevwin", NULL);
			}
		}
		else if (abs(deltax) > abs(deltay)) {
			if (deltax < 0) //left
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
static GtkWidget *closecb(Win *win)
{
	gtk_widget_destroy(win->winw);
}
static void loadcb(WebKitWebView *k, WebKitLoadEvent event, Win *win)
{
	win->crashed = false;
	switch (event) {
	case WEBKIT_LOAD_STARTED:
		//D(WEBKIT_LOAD_STARTED %s, URI(win))
		win->scheme = false;

		win->mode = Mnormal;
		update(win);
		if (win->userreq) {
			win->userreq = false;
			g_free(win->starturi);
			win->starturi = NULL;
			resetconf(win, false);
			//has priority
			win->starturi = g_strdup(URI(win));
		} else {
			seturiconf(win);
		}
		break;
	case WEBKIT_LOAD_REDIRECTED:
		//D(WEBKIT_LOAD_REDIRECTED %s, URI(win))
		seturiconf(win);
		break;
	case WEBKIT_LOAD_COMMITTED:
		//D(WEBKIT_LOAD_COMMITED %s, URI(win))

		if (!win->scheme && g_str_has_prefix(URI(win), APP":"))
			webkit_web_view_reload(win->kit);

		if (win->lasturiconf &&
				strcmp(win->lasturiconf, win->starturi) != 0)
			_seturiconf(win, win->starturi);

		send(win, Con, NULL);

		if (!webkit_web_view_get_tls_info(win->kit, NULL, &win->tlserr))
			win->tlserr = 0;

		break;
	case WEBKIT_LOAD_FINISHED:
		//DD(WEBKIT_LOAD_FINISHED)


		if (!g_str_has_prefix(URI(win), APP":"))
		{
			gchar tstr[99];
			time_t t = time(NULL);

			strftime(tstr, sizeof(tstr), "%T/%d/%b/%y", localtime(&t));
			g_idle_add((GSourceFunc)history, g_strdup_printf("%s %s %s",
						tstr, URI(win), webkit_web_view_get_title(win->kit) ?: ""));
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
	gchar *dir = g_path_get_dirname(ai->path);
	Win *win = LASTWIN;

	gchar *argv[1] = {
		ai->path
	};
	gchar **envp = g_get_environ();
	envp = g_environ_setenv(envp, "SUFFIX", suffix, true);
	envp = g_environ_setenv(envp, "WINID" , win->pageid, true);
	envp = g_environ_setenv(envp, "URI"   , URI(win), true);
	envp = g_environ_setenv(envp, "LINK_OR_URI", URI(win), true);

	const gchar *title = webkit_web_view_get_title(win->kit);
	if (!title) title = URI(win);
	envp = g_environ_setenv(envp, "TITLE" , title, true);
	envp = g_environ_setenv(envp, "LINKLABEL_OR_TITLE" , title, true);

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
		envp = g_environ_setenv(envp, "LINKLABEL_OR_TITLE" , win->linklabel, true);
	}

	GPid child_pid;
	GError *err = NULL;
	if (!g_spawn_async(
				dir, argv, envp,
				G_SPAWN_DEFAULT,
				NULL, NULL, &child_pid, &err))
	{
		alert(err->message);
		g_error_free(err);
	}
	g_spawn_close_pid(child_pid);

	g_strfreev(envp);
	g_free(dir);
}
static guint menuhash = 0;
static void addhash(gchar *str)
{
	while (*str++)
		menuhash = menuhash * 33 + *str;
}
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
		if (*dn == '.') continue;
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
			if (menu)
				sub = webkit_context_menu_new();

			ai->actions = dirmenu(sub, path, accel);
			if (!ai->actions)
				nodata = true;
			else if (menu)
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new_with_submenu(name, sub));

			g_free(path);
		} else {
			ai->action = gtk_action_new(name, name, NULL, NULL);
			ai->path = path;
			addhash(path);
			SIG(ai->action, "activate", actioncb, ai);

			gtk_action_set_accel_group(ai->action, accelg);
			gtk_action_set_accel_path(ai->action, accel);
			gtk_action_connect_accelerator(ai->action);

			if (menu)
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
		addscript(dir, "0addMenu"         , "mimeopen -n %s");
		addscript(dir, "0bookmark"        , APP" \"$SUFFIX\" bookmarklinkor \"\"");
		addscript(dir, "0duplicate"       , APP" \"$SUFFIX\" opennew $URI");
		addscript(dir, "0history"         , APP" \"$SUFFIX\" showhistory \"\"");
		addscript(dir, "0main"            , APP" \"$SUFFIX\" open "APP":main");
		addscript(dir, "3---"             , "");
		addscript(dir, "3openClipboard"   , APP" \"$SUFFIX\" open \"$CLIPBOARD\"");
		addscript(dir, "3openClipboardNew", APP" \"$SUFFIX\" opennew \"$CLIPBOARD\"");
		addscript(dir, "3openSelection"   , APP" \"$SUFFIX\" open \"$PRIMARY\"");
		addscript(dir, "3openSelectionNew", APP" \"$SUFFIX\" opennew \"$PRIMARY\"");
		addscript(dir, "6searchDictionary", APP" \"$SUFFIX\" open \"u $PRIMARY\"");
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
		monitor(accelp);
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
	menuhash = 5381;

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
	win->oneditable = webkit_hit_test_result_context_is_editable(htr);

	g_free(win->linklabel);
	const gchar *label = webkit_hit_test_result_get_link_label(htr);
	if (!label)
		label = webkit_hit_test_result_get_link_title(htr);
	win->linklabel = label ? g_strdup(label): NULL;

	g_free(win->link);
	const gchar *uri =
		webkit_hit_test_result_get_link_uri (htr) ?:
		webkit_hit_test_result_get_image_uri(htr) ?:
		webkit_hit_test_result_get_media_uri(htr) ;

	win->link = uri ? g_strdup(uri): NULL;

	makemenu(menu);

	//GtkAction * webkit_context_menu_item_get_action(WebKitContextMenuItem *item);
	//GtkWidget * gtk_action_create_menu_item(GtkAction *action);
	return false;
}


//@entry
static bool focusincb(Win *win)
{
	if (gtk_widget_get_visible(win->entw)) {
		win->mode = Mnormal;
		update(win);
	}
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

				send(win, Cfocus, NULL);
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
			win->mode = Mnormal;
		}
		break;
	case GDK_KEY_Escape:
		if (win->mode == Mfind) {
			webkit_find_controller_search_finish(win->findct);
		}
		win->mode = Mnormal;
		break;
	default:
		return false;
	}
	update(win);
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

			//we assume cookies are confs
			gchar *cookiefile = path2conf("cookie");
			webkit_cookie_manager_set_persistent_storage(cookiemgr,
					cookiefile, WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
			g_free(cookiefile);

			webkit_cookie_manager_set_accept_policy(cookiemgr,
					WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);

#if ! SHARED
			webkit_web_context_set_process_model(ctx,
					WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);
#endif

			webkit_web_context_set_web_extensions_initialization_user_data(
					ctx, g_variant_new_string(fullname));

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
		}

		//win->kitw = webkit_web_view_new_with_context(ctx);
		win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
			"web-context", ctx, "user-content-manager", cmgr, NULL);
	}
	g_object_set_data(win->kito, "win", win); //for context event

	win->findct = webkit_web_view_get_find_controller(win->kit);

	win->set = webkit_settings_new();
	setprops(win, conf, DSET);
	webkit_web_view_set_settings(win->kit, win->set);
	webkit_web_view_set_zoom_level(win->kit, confdouble("zoom"));

	GObject *o = win->kito;
	SIGW(o, "destroy"             , destroycb , win);
	SIGW(o, "web-process-crashed" , crashcb   , win);
	SIGW(o, "notify::title"       , notifycb  , win);
	SIGW(o, "notify::uri"         , notifycb  , win);
	SIGW(o, "notify::estimated-load-progress", progcb, win);
	SIG( o, "mouse-target-changed", targetcb  , win);


	SIG( o, "decide-policy"       , policycb  , win);
	SIG( o, "key-press-event"     , keycb     , win);
	SIG( o, "button-press-event"  , btncb     , win);
	SIG( o, "button-release-event", btnrcb    , win);
	SIG( o, "enter-notify-event"  , entercb   , win);

	SIGW(o, "create"              , createcb  , win);
	SIGW(o, "close"               , closecb   , win);
	SIG( o, "load-changed"        , loadcb    , win);

	SIG( o, "context-menu"        , contextcb , win);

	//for entry
	SIGW(o, "focus-in-event"      , focusincb , win);

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
	g_ptr_array_insert(wins, 0, win);

	if (!cbwin)
		openuri(win, uri);

	return win;
}


//@main
void ipccb(const gchar *line)
{
	gchar **args = g_strsplit(line, ":", 3);

	if (strcmp(args[0], "0") == 0)
		run(LASTWIN, args[1], args[2]);
	else
	{
		Win *win = winbyid(args[0]);
		if (win)
			run(win, args[1], args[2]);
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
		GList *icon_list = g_list_append(NULL, pix);
		gtk_window_set_default_icon_list(icon_list);
		g_list_free(icon_list);

		g_object_unref(G_OBJECT(pix));
	}

	wins = g_ptr_array_new();
	dlwins = g_ptr_array_new();

	if (run(NULL, action, uri))
		gtk_main();
	else
		exit(1);
	exit(0);
}
