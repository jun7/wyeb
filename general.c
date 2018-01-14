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

#include <math.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <regex.h>

#define OLDNAME  "wyebrowser"
#define DIRNAME  "wyeb."
#define APP      "wyeb"

#define DSET "set;"
#define MIMEOPEN "mimeopen -n %s"
#define HINTKEYS "fsedagwrvxqcz"
//bt324"

#if DEBUG
# define D(f, ...) g_print(#f"\n", __VA_ARGS__);
# define DNN(f, ...) g_print(#f, __VA_ARGS__);
# define DD(a) g_print(#a"\n");
# define DENUM(v, e) if (v == e) D(%s:%3d is %s, #v, v, #e);
#else
# define D(...) ;
# define DNN(...) ;
# define DD(a) ;
# define DENUM(v, e) ;
#endif


#if WEBKIT_MAJOR_VERSION > 2 || WEBKIT_MINOR_VERSION > 16
# define NEWV 1
#else
# define NEWV 0
#endif


#define SIG(o, n, c, u) \
	g_signal_connect(o, n, G_CALLBACK(c), u)
#define SIGA(o, n, c, u) \
	g_signal_connect_after(o, n, G_CALLBACK(c), u)
#define SIGW(o, n, c, u) \
	g_signal_connect_swapped(o, n, G_CALLBACK(c), u)
#define GFA(p, v) {g_free(p); p = v;}

static gchar *fullname = "";
static bool shared = true;
static GKeyFile *conf = NULL;
static gchar *confpath = NULL;

typedef struct _WP WP;

typedef enum {
	Cload   = 'L',
	Coverset= 'O',
	Cstart  = 's',
	Con     = 'o',

	Ckey    = 'k',
	Cclick  = 'c',
	Clink   = 'l',
	Curi    = 'u',
	Ctext   = 't',
	Cspawn  = 'S',
	Crange  = 'r',

	Cmode   = 'm',
	Cfocus  = 'f',
	Cblur   = 'b',
	Crm     = 'R',
	Cwhite  = 'w',
	Ctlset  = 'T',
	Ctlget  = 'g',

	Cfree   = 'F',
} Coms;


//@conf
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
	{"boot"  , "ephemeral"    , "false"},

	{"search", "b"            , "https://bing.com/?q=%s"},
	{"search", "g"            , "https://www.google.com/search?q=%s"},
	{"search", "f"            , "https://www.google.com/search?q=%s&btnI=I"},
	{"search", "u"            , "http://www.urbandictionary.com/define.php?term=%s"},

	{"set:v"     , "enable-caret-browsing", "true"},
	{"set:script", "enable-javascript"    , "false"},
	{"set:image" , "auto-load-images"     , "true"},
	{"set:image" , "linkformat"   , "[![](%s) %.40s](%s)"},
	{"set:image" , "linkdata"     , "ftu"},
	{"set:image" , "hintstyle"    ,
		"font-size:medium !important;-webkit-transform:rotate(-9deg)"},

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
	{DSET    , "linkdata"         , "tu", "t: title, u: uri, f: favicon"},
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
	{DSET    , "hintstyle"        , ""},
	{DSET    , "stdoutheaders"    , "false"},
	{DSET    , "removeheaders"    , "",
		"removeheaders=Upgrade-Insecure-Requests;Referer;"},

	//changes
	//{DSET      , "auto-load-images" , "false"},
	//{DSET      , "enable-plugins"   , "false"},
	//{DSET      , "enable-java"      , "false"},
	//{DSET      , "enable-fullscreen", "false"},
};
#ifdef MAINC
static bool confbool(gchar *key)
{ return g_key_file_get_boolean(conf, "all", key, NULL); }
static gint confint(gchar *key)
{ return g_key_file_get_integer(conf, "all", key, NULL); }
static gdouble confdouble(gchar *key)
{ return g_key_file_get_double(conf, "all", key, NULL); }
#endif
static gchar *confcstr(gchar *key)
{//return is static string
	static gchar *str = NULL;
	GFA(str, g_key_file_get_string(conf, "all", key, NULL))
	return str;
}
static gchar *getset(WP *wp, gchar *key)
{//return is static string
	static gchar *ret = NULL;
	if (!wp)
	{
		GFA(ret, g_key_file_get_string(conf, DSET, key, NULL))
		return ret;
	}
	return g_object_get_data(wp->seto, key);
}
static bool getsetbool(WP *wp, gchar *key)
{ return !g_strcmp0(getset(wp, key), "true"); }
static int getsetint(WP *wp, gchar *key)
{ return atoi(getset(wp, key) ?: "0"); }


static void _mkdirif(gchar *path, bool isfile)
{
	gchar *dir;
	if (isfile)
		dir = g_path_get_dirname(path);
	else
		dir = path;

	if (!g_file_test(dir, G_FILE_TEST_EXISTS))
		g_mkdir_with_parents(dir, 0700);

	g_assert(g_file_test(dir, G_FILE_TEST_IS_DIR));

	if (isfile)
		g_free(dir);
}
static void mkdirif(gchar *path)
{
	_mkdirif(path, true);
}

static gchar *path2conf(const gchar *name)
{
	return g_build_filename(
			g_get_user_config_dir(), fullname, name, NULL);
}

static GSList *mqueue   = NULL;
static GSList *mqueuedo = NULL;
static gboolean mqueuecb(gpointer func)
{
	mqueue = g_slist_remove(mqueue, func);
	if (g_slist_find(mqueuedo, func))
	{
		mqueuedo = g_slist_remove(mqueuedo, func);
		((void (*)(bool))func)(true);
	}
	return false;
}
static void monitorcb(
		GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e, void (*func)(bool))
{
	if (e != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) return;
	if (g_slist_find(mqueue, func))
	{
		if (!g_slist_find(mqueuedo, func))
			mqueuedo = g_slist_prepend(mqueuedo, func);
		return;
	}

	func(true);
	mqueue = g_slist_prepend(mqueue, func);
	g_idle_add(mqueuecb, func);
}
static void monitor(gchar *path, void (*func)(bool))
{
	GFile *gf = g_file_new_for_path(path);
	GFileMonitor *gm = g_file_monitor_file(
			gf, G_FILE_MONITOR_NONE, NULL, NULL);
	SIG(gm, "changed", monitorcb, func);

	g_object_unref(gf);
}

static bool getctime(gchar *path, __time_t *ctime)
{
	struct stat info;
	g_assert((stat(path, &info) == 0));

	if (*ctime == info.st_ctime) return false;
	*ctime = info.st_ctime;
	return true;
}

static void prepareif(
		gchar **path, __time_t *ctime,
		gchar *name, gchar *initstr, void (*monitorcb)(bool))
{
	bool first = false;
	if (!*path)
	{
		first = true;
		*path = path2conf(name);
	}

	if (g_file_test(*path, G_FILE_TEST_EXISTS))
	{
		if (first) goto outtime;
		goto out;
	}

	GFile *gf = g_file_new_for_path(*path);

	GFileOutputStream *o = g_file_create(
			gf, G_FILE_CREATE_PRIVATE, NULL, NULL);
	g_output_stream_write((GOutputStream *)o,
			initstr, strlen(initstr), NULL, NULL);
	g_object_unref(o);

	g_object_unref(gf);

outtime:
	if (ctime) getctime(*path, ctime);

out:
	if (first)
		monitor(*path, monitorcb);
}

static bool setprop(WP *wp, GKeyFile *kf, gchar *group, gchar *key)
{
	if (!g_key_file_has_key(kf, group, key, NULL)) return false;
	gchar *val = g_key_file_get_string(kf, group, key, NULL);
#ifdef MAINC
	if (!strcmp(key, "usercss") &&
		g_strcmp0(g_object_get_data(wp->seto, key), val))
	{
		setcss(wp, val);
	}
#endif
	g_object_set_data_full(wp->seto, key, *val ? val : NULL, g_free);
	return true;
}
static void setprops(WP *wp, GKeyFile *kf, gchar *group)
{
	//sets
	static int deps = 0;
	if (deps > 99) return;
	gchar **sets = g_key_file_get_string_list(kf, group, "sets", NULL, NULL);
	for (gchar **set = sets; set && *set; set++) {
		gchar *setstr = g_strdup_printf("set:%s", *set);
		deps++;
		setprops(wp, kf, setstr);
		deps--;
		g_free(setstr);
	}
	g_strfreev(sets);

	//D(set props group: %s, group)
#ifdef MAINC
	_kitprops(true, wp->seto, kf, group);
#else
	if (setprop(wp, kf, group, "user-agent") && strcmp(group, DSET))
		wp->setagent = true;
#endif

	//non webkit settings
	int len = sizeof(dconf) / sizeof(*dconf);
	for (int i = 0; i < len; i++)
		if (!strcmp(dconf[i].group, DSET))
			setprop(wp, kf, group, dconf[i].key);
}

static bool _seturiconf(WP *wp, const gchar* uri)
{
	bool ret = false;
	GFA(wp->lastreset, g_strdup(uri))

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

		if (regexec(&reg, uri ?: "", 0, NULL, 0) == 0) {
			setprops(wp, conf, *gl);
			GFA(wp->lasturiconf, g_strdup(uri))
			ret = true;
		}

		regfree(&reg);
		g_free(tofree);
	}

	g_strfreev(groups);
	return ret;
}

static void _resetconf(WP *wp, const gchar *uri, bool force)
{
#ifndef MAINC
	wp->setagentprev = wp->setagent && !force;
	wp->setagent = false;
#endif

	if (wp->lasturiconf || force)
	{
		GFA(wp->lasturiconf, NULL)
		setprops(wp, conf, DSET);
	}

	_seturiconf(wp, uri);

	if (wp->overset) {
		gchar *setstr = g_strdup_printf("set:%s", wp->overset);
		setprops(wp, conf, setstr);
		g_free(setstr);
	}
}
static void initconf(GKeyFile *kf)
{
	if (conf) g_key_file_free(conf);
	conf = kf ?: g_key_file_new();

	gint len = sizeof(dconf) / sizeof(*dconf);
	for (int i = 0; i < len; i++)
	{
		Conf c = dconf[i];

		if (g_key_file_has_key(conf, c.group, c.key, NULL)) continue;
		if (kf)
		{
			if (!strcmp(c.group, "search")) continue;
			if (g_str_has_prefix(c.group, "set:")) continue;
		}

		g_key_file_set_value(conf, c.group, c.key, c.val);
		if (c.desc)
			g_key_file_set_comment(conf, c.group, c.key, c.desc, NULL);
	}

#ifdef MAINC
	//fill vals not set
	if (LASTWIN)
		_kitprops(false, LASTWIN->seto, conf, DSET);
	else {
		WebKitSettings *set = webkit_settings_new();
		_kitprops(false, (GObject *)set, conf, DSET);
		g_object_unref(set);
	}

	if (kf) return;

	//sample and comment
	g_key_file_set_comment(conf, DSET, NULL, "Default of 'set's.", NULL);

	const gchar *sample = "uri:^https?://(www\\.)?foo\\.bar/.*";

	g_key_file_set_boolean(conf, sample, "enable-javascript", true);
	g_key_file_set_comment(conf, sample, NULL,
			"After 'uri:' is regular expressions for the setting set.\n"
			"preferential order of sections: Last > First > '"DSET"'"
			, NULL);

	sample = "uri:^foo|a-zA-Z0-9|*";

	g_key_file_set_string(conf, sample, "reg", "^foo[^a-zA-Z0-9]*$");
	g_key_file_set_comment(conf, sample, "reg",
			"Use reg if the regular expression has []."
			, NULL);

	g_key_file_set_string(conf, sample, "sets", "image;script");
	g_key_file_set_comment(conf, sample, "sets",
			"include other sets." , NULL);
#endif
}


//@misc
static gchar *escape(const gchar *str)
{
	gulong len = 0;
	gchar *esc = ".?+";
	for (const gchar *c = str; *c; c++)
	{
		len++;
		for (gchar *e = esc; *e; e++)
			if (*e == *c)
			{
				len++;
				break;
			}
	}
	gchar ret[len + 1];
	ret[len] = '\0';

	gulong i = 0;
	for (const gchar *c = str; *c; c++)
	{
		for (gchar *e = esc; *e; e++)
			if (*e == *c)
			{
				ret[i++] = '\\';
				break;
			}

		ret[i++] = *c;
	}

	return g_strdup(ret);
}


//@ipc
static gchar *ipcpath(gchar *name)
{
	gchar *path = g_build_filename(g_get_user_runtime_dir(), fullname, name, NULL);
	mkdirif(path);
	return path;
}

static void ipccb(const gchar *line);
static gboolean ipcgencb(GIOChannel *ch, GIOCondition c, gpointer p)
{
	gchar *line;
//	GError *err = NULL;
	g_io_channel_read_line(ch, &line, NULL, NULL, NULL);
//	if (err)
//	{
//		D("ioerr: ", err->message);
//		g_error_free(err);
//	}
	if (!line) return true;
	g_strchomp(line);

	//D(receive %s, line)
	gchar *unesc = g_strcompress(line);
	ipccb(unesc);
	g_free(unesc);
	g_free(line);
	return true;
}

static bool ipcsend(gchar *name, gchar *str) {
	gchar *path = ipcpath(name);
	bool ret = false;
	int cpipe = 0;
	if (
		(g_file_test(path, G_FILE_TEST_EXISTS)) &&
		(cpipe = open(path, O_WRONLY | O_NONBLOCK)))
	{
		//D(send start %s %s, name, str)
		ret = true;
		char *esc = g_strescape(str, "");
		gchar *send = g_strconcat(esc, "\n", NULL);
		ret = write(cpipe, send, strlen(send)) != -1;
		g_free(send);
		g_free(esc);
		close(cpipe);

		//D(send -end- %s %s, name, str)
	}
	g_free(path);
	return ret;
}
static GSource *_ipcwatch(gchar *name, GMainContext *ctx) {
	gchar *path = ipcpath(name);

	if (!g_file_test(path, G_FILE_TEST_EXISTS))
		mkfifo(path, 0600);

	GIOChannel *io = g_io_channel_new_file(path, "r+", NULL);
	GSource *watch = g_io_create_watch(io, G_IO_IN);
	g_io_channel_unref(io);
	g_source_set_callback(watch, (GSourceFunc)ipcgencb, NULL, NULL);
	g_source_attach(watch, ctx);

	g_free(path);
	return watch;
}
static void ipcwatch(gchar *name)
{
	_ipcwatch(name, g_main_context_default());
}
