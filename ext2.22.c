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

#include <ctype.h>
#include <webkit2/webkit-web-extension.h>

#define let JSCValue *

typedef struct _WP {
	WebKitWebPage *kit;
	guint64        id;

	WebKitFrame   *mf;

	let            apnode;
	let            apchild;
	gchar         *apkeys;

	gchar          lasttype;
	gchar         *lasthintkeys;
	let            rangestart; //not ref
	bool           script;
	GSList        *black;
	GSList        *white;
	GSList        *emitters;

	gint           pagereq;
	bool           redirected;
	gchar        **refreq;

	//conf
	GObject       *seto;
	gchar         *lasturiconf;
	gchar         *lastreset;
	gchar         *overset;
	bool           setagent;
	bool           setagentprev;
	GMainLoop     *sync;
} Page;

#include "general.c"
static void loadconf()
{
	if (!confpath)
		confpath = path2conf("main.conf");

	GKeyFile *new = g_key_file_new();
	g_key_file_load_from_file(new, confpath,G_KEY_FILE_NONE, NULL);
	initconf(new);
}
static void resetconf(Page *page, const gchar *uri, bool force)
{
	page->setagentprev = page->setagent && !force;
	page->setagent = false;

	_resetconf(page, uri, force);

	g_object_set_data(G_OBJECT(page->kit), "adblock",
			GINT_TO_POINTER(getsetbool(page, "adblock") ? 'y' : 'n'));
}


static GPtrArray *pages = NULL;

static void freepage(Page *page)
{
	g_free(page->apkeys);

	g_free(page->lasthintkeys);
	g_slist_free_full(page->black, g_free);
	g_slist_free_full(page->white, g_free);
	g_slist_free_full(page->emitters, g_object_unref);
	g_strfreev(page->refreq);

	g_object_unref(page->seto);
	g_free(page->lasturiconf);
	g_free(page->lastreset);
	g_free(page->overset);

	g_ptr_array_remove(pages, page);
	g_free(page);
}

typedef struct {
	bool ok;
	bool insight;
	let  elm;
	double fx;
	double fy;
	double x;
	double y;
	double w;
	double h;
	double zi;
} Elm;



/*
static JSCValue *tojsv(Page *page, void *dom)
{
	return webkit_frame_get_js_value_for_dom_object(page->mf, dom);
}
*/

static JSCValue *pagejsv(Page *page, gchar *name)
{
//	static WebKitScriptWorld *world = NULL;
//	if (!world)
//		world = webkit_script_world_new();

	return jsc_context_get_value(
			webkit_frame_get_js_context(page->mf),
//			webkit_frame_get_js_context_for_script_world(page->mf, world),
			name);
}
static JSCValue *sdoc(Page *page)
{
	static JSCValue *s = NULL;
	if (s) g_object_unref(s);
	return s = pagejsv(page, "document");
//	return s = tojsv(page, webkit_web_page_get_dom_document(page->kit));
}
static JSCValue *swin(Page *page)
{
	static JSCValue *s = NULL;
	if (s) g_object_unref(s);
	return s = pagejsv(page, "window");
}


#define invoker(...) jsc_value_object_invoke_method(__VA_ARGS__, G_TYPE_NONE)
#define invoke(...) g_object_unref(invoker(__VA_ARGS__))
#define isdef(v) (!jsc_value_is_undefined(v) && !jsc_value_is_null(v))

#define aB(s) G_TYPE_BOOLEAN, s
#define aL(s) G_TYPE_LONG, s
#define aD(s) G_TYPE_DOUBLE, s
#define aS(s) G_TYPE_STRING, s
#define aJ(s) JSC_TYPE_VALUE, s


static let prop(let v, gchar *name)
{
	let retv = jsc_value_object_get_property(v, name);
	if (isdef(retv))
		return retv;
	g_object_unref(retv);
	return NULL;
}
static let propunref(let v, gchar *name)
{
	let retv = prop(v, name);
	g_object_unref(v);
	return retv;
}
static double propd(let v, gchar *name)
{
	let retv = jsc_value_object_get_property(v, name);
	double ret = jsc_value_to_double(retv);
	g_object_unref(retv);
	return ret;
}
/*
static int propi(let v, gchar *name)
{
	let retv = jsc_value_object_get_property(v, name);
	int ret = jsc_value_to_int32(retv);
	g_object_unref(retv);
	return ret;
}
static bool propb(let v, gchar *name)
{
	let retv = jsc_value_object_get_property(v, name);
	bool ret = jsc_value_to_boolean(retv);
	g_object_unref(retv);
	return ret;
}
*/
static gchar *props(let v, gchar *name)
{
	gchar *ret = NULL;
	let retv = jsc_value_object_get_property(v, name);
	if (isdef(retv))
		ret = jsc_value_to_string(retv);

	g_object_unref(retv);
	return ret;
}
static void setprop_s(let v, gchar *name, gchar *data)
{
	let dv = jsc_value_new_string(jsc_value_get_context(v), data);
	jsc_value_object_set_property(v, name, dv);
	g_object_unref(dv);
}
static gchar *attr(let v, gchar *name)
{
	let retv = invoker(v, "getAttribute", aS(name));
	gchar *ret = jsc_value_is_null(retv) ? NULL : jsc_value_to_string(retv);
	g_object_unref(retv);
	return ret;
}
static bool attrb(let v, gchar *name)
{
	gchar *str = attr(v, name);
	bool ret = !g_strcmp0(str, "true");
	g_free(str);
	return ret;
}
static bool hasattr(let v, gchar *name)
{
	gchar *str = attr(v, name);
	g_free(str);
	return str;
}

static let idx(let cl, int i)
{
	gchar buf[9];
	snprintf(buf, 9, "%d", i);
	return prop(cl, buf);

//	let retv = jsc_value_object_get_property_at_index(cl, i);
//	if (isdef(retv))
//		return retv;
//	g_object_unref(retv);
//	return NULL;
}

static void addlistener(let doc, gchar *name, void *func, void *data)
{
//this is disabled when javascript disabled
//also data is only sent once
//	let f = jsc_value_new_function(jsc_value_get_context(doc), NULL,
//			func, data, NULL,
//			G_TYPE_NONE, 1, JSC_TYPE_VALUE, JSC_TYPE_VALUE);
//	invoke(doc, "addEventListener", aS(name), aJ(f));
//	g_object_unref(f);

	webkit_dom_event_target_add_event_listener(
		(void *)webkit_dom_node_for_js_value(doc), name, func, false, data);
}


static void __attribute__ ((unused)) proplist(JSCValue *v)
{
	if (jsc_value_is_undefined(v))
	{
		DD(undefined value)
		return;
	}

	gchar **ps = jsc_value_object_enumerate_properties(v);
	if (ps)
		for (gchar **pr = ps; *pr; pr++)
			D(p %s, *pr)
	else
		DD(no props)
	g_strfreev(ps);
}



static void clearelm(Elm *elm)
{
	if (elm->elm)
		g_object_unref(elm->elm);
}

static const gchar *clicktags[] = {
	"INPUT", "TEXTAREA", "SELECT", "BUTTON", "A", "AREA", NULL};
static const gchar *linktags[] = {
	"A", "AREA", NULL};
static const gchar *uritags[] = {
	"A", "AREA", "IMG", "VIDEO", "AUDIO", NULL};

static const gchar *texttags[] = {
	"INPUT", "TEXTAREA", NULL};

static const gchar *inputtags[] = {
	"INPUT", "TEXTAREA", "SELECT", NULL};

// input types
/*
static const gchar *itext[] = { //no name is too
	"search", "text", "url", "email",  "password", "tel", NULL
};
static const gchar *ilimitedtext[] = {
	"month",  "number", "time", "week", "date", "datetime-local", NULL
};
*/
static const gchar *inottext[] = {
	"color", "file", "radio", "range", "checkbox", "button", "reset", "submit",

	// unknown
	"image", //to be submit
	// not focus
	"hidden",
	NULL
};

//@misc
static void send(Page *page, gchar *action, const gchar *arg)
{
	gchar *ss = g_strdup_printf("%"G_GUINT64_FORMAT":%s:%s",
			page->id, action, arg ?: "");
	//D(send to main %s, ss)
	ipcsend("main", ss);
	g_free(ss);
}
static bool isins(const gchar **ary, gchar *val)
{
	if (!val) return false;
	for (;*ary; ary++)
		if (!strcmp(val, *ary)) return true;
	return false;
}
static bool isinput(let te)
{
	bool ret = false;
	gchar *tag = props(te, "tagName");
	if (isins(inputtags, tag))
	{
		if (strcmp(tag, "INPUT"))
		{
			g_free(tag);
			return true;
		}

		gchar *type = attr(te, "TYPE");
		if (!type || !isins(inottext, type))
			ret = true;
		g_free(type);
	}
	g_free(tag);

	return ret;
}
static gchar *tofull(let te, gchar *uri)
{
	if (!te || !uri) return NULL;
	gchar *bases = props(te, "baseURI");
	SoupURI *base = soup_uri_new(bases);
	SoupURI *full = soup_uri_new_with_base(base, uri);

	gchar *ret = soup_uri_to_string(full, false);

	g_free(bases);
	soup_uri_free(base);
	soup_uri_free(full);
	return ret;
}


//@whiteblack
typedef struct {
	int white;
	regex_t reg;
} Wb;
static void clearwb(Wb *wb)
{
	regfree(&wb->reg);
	g_free(wb);
}
static GSList *wblist = NULL;
static gchar  *wbpath = NULL;
static void setwblist(bool reload)
{
	if (wblist)
		g_slist_free_full(wblist, (GDestroyNotify)clearwb);
	wblist = NULL;

	if (!g_file_test(wbpath, G_FILE_TEST_EXISTS)) return;

	GIOChannel *io = g_io_channel_new_file(wbpath, "r", NULL);
	gchar *line;
	while (g_io_channel_read_line(io, &line, NULL, NULL, NULL)
			== G_IO_STATUS_NORMAL)
	{
		if (*line == 'w' || *line =='b')
		{
			g_strchomp(line);
			Wb *wb = g_new0(Wb, 1);
			if (regcomp(&wb->reg, line + 1, REG_EXTENDED | REG_NOSUB))
			{
				g_free(line);
				g_free(wb);
				continue;
			}
			wb->white = *line == 'w' ? 1 : 0;
			wblist = g_slist_prepend(wblist, wb);
		}
		g_free(line);
	}
	g_io_channel_unref(io);

	if (reload)
		send(*pages->pdata, "_reloadlast", NULL);
}
static int checkwb(const gchar *uri) // -1 no result, 0 black, 1 white;
{
	if (!wblist) return -1;

	for (GSList *next = wblist; next; next = next->next)
	{
		Wb *wb = next->data;
		if (regexec(&wb->reg, uri, 0, NULL, 0) == 0)
			return wb->white;
	}

	return -1;
}
static void addwhite(Page *page, const gchar *uri)
{
	//D(blocked %s, uri)
	if (getsetbool(page, "showblocked"))
		send(page, "_blocked", uri);
	page->white = g_slist_prepend(page->white, g_strdup(uri));
}
static void addblack(Page *page, const gchar *uri)
{
	page->black = g_slist_prepend(page->black, g_strdup(uri));
}
static void showwhite(Page *page, bool white)
{
	GSList *list = white ? page->white : page->black;
	if (!list)
	{
		send(page, "showmsg", "No List");
		return;
	}

	FILE *f = fopen(wbpath, "a");
	if (!f) return;

	if (white)
		send(page, "wbnoreload", NULL);

	gchar pre = white ? 'w' : 'b';
	fprintf(f, "\n# %s in %s\n",
			white ? "blocked" : "loaded",
			webkit_web_page_get_uri(page->kit));

	list = g_slist_reverse(g_slist_copy(list));
	for (GSList *next = list; next; next = next->next)
	{
		gchar *esc = regesc(next->data);
		gchar *line = g_strdup_printf("%c^%s\n", pre, esc);
		g_free(esc);

		fputs(line, f);

		g_free(line);
	}
	g_slist_free(list);

	fclose(f);

	send(page, "openeditor", wbpath);
}


//@textlink
static let tldocelm;
static let tlelm;
static void textlinkset(Page *page, gchar *path)
{
	let doc = sdoc(page);
	let docelm = prop(doc, "documentElement");
	g_object_unref(docelm);
	if (tldocelm != docelm) return;

	GIOChannel *io = g_io_channel_new_file(path, "r", NULL);
	gchar *text;
	g_io_channel_read_to_end(io, &text, NULL, NULL);
	g_io_channel_unref(io);

	setprop_s(tlelm, "value", text);
	g_free(text);
}
static void textlinkget(Page *page, gchar *path)
{
	let doc = sdoc(page);
	let te = prop(doc, "activeElement");
	gchar *tag = props(te, "tagName");
	bool ista = !strcmp(tag, "TEXTAREA");
	g_free(tag);

	if (tlelm) g_object_unref(tlelm);
	tlelm = NULL;
	if (tldocelm) g_object_unref(tldocelm);
	tldocelm = NULL;

	if (ista || isinput(te))
		tlelm = te;
	else
	{
		send(page, "showmsg", "Not a text");
		return;
	}
	tldocelm = prop(doc, "documentElement");

	gchar *text = props(tlelm, "value");

	GIOChannel *io = g_io_channel_new_file(path, "w", NULL);
	g_io_channel_write_chars(io, text ?: "", -1, NULL, NULL);
	g_io_channel_unref(io);
	g_free(text);

	send(page, "_textlinkon", NULL);
}


//@hinting
static gchar *getstyleval(let style, gchar *name)
{
	gchar *ret = NULL;
	let retv = invoker(style, "getPropertyValue", aS(name));
	ret = jsc_value_to_string(retv);
	g_object_unref(retv);
	return ret;
}
static bool styleis(let dec, gchar* name, gchar *pval)
{
	gchar *val = getstyleval(dec, name);
	bool ret = (val && !strcmp(pval, val));
	g_free(val);

	return ret;
}

static Elm getrect(let te)
{
	Elm elm = {0};
	let rect  = invoker(te, "getBoundingClientRect");
	elm.x = propd(rect, "left");
	elm.y = propd(rect, "top");
	elm.w = propd(rect, "width");
	elm.h = propd(rect, "height");
	g_object_unref(rect);

	return elm;
}

static void _trim(double *tx, double *tw, double *px, double *pw)
{
	double right = *tx + *tw;
	double pr    = *px + *pw;
	if (pr < right)
		*tw -= right - pr;

	if (*px > *tx)
	{
		*tw -= *px - *tx;
		*tx = *px;
	}
}

static char *_makehintelm(Page *page,
		bool center ,int y, int x, int h, int w,
		char *uri, const char* text, int len, bool head)
{
	//ret
	GString *str = g_string_new(NULL);
	g_string_printf(str,
			"<div title=%s style="
			"position:absolute;" //somehow if fixed web page crashes
			"overflow:visible;"
			"display:block;"
			"visibility:visible;"
			"padding:0;"
			"margin:0;"
			"opacity:1;"
			"top:%dpx;"
			"left:%dpx;"
			"height:%dpx;"
			"width:%dpx;"
			"text-align:center;"
			">"
			, uri ?: "-"
			, y, x, h, w);

	//area
	g_string_append(str, "<div style="
			"position:absolute;"
			"z-index:2147483647;"
			"background-color:#a6f;"
			"opacity:.1;"
			"border-radius:.4em;"
			"height:100%;"
			"width:100%;"
			"></div>");

	if (!text) goto out;

	//hint
	char *ht = g_strdup_printf("%s", text + len);
	const double offset = 6;

	g_string_append_printf(str,
			"<span style='"
			"position: relative;"
			"z-index: 2147483647;"
			"font-size: small !important;"
			"font-family: \"DejaVu Sans Mono\", monospace !important;"
			"font-weight: bold;"
			"background: linear-gradient(%s);"
			"color: white;"
			"border-radius: .3em;"
			"opacity: 0.%s;"
			"display:inline-block;"
			"padding: .1em %spx 0;"
			"line-height: 1em;"
			"top: %fem;"
			"%s;" //user setting
			"%s" //center
			"'>%s</span>"
			, head ? "#649, #203" : "#203, #203"
			, head ? "9" : "4"
			, strlen(ht) == 1 ? "2" : "1"
			, center ? offset / 10 : (y > offset ? offset : y) / -10
			, getset(page, "hintstyle") ?: ""
			, center ? "background: linear-gradient(darkorange, red);" : ""
			, ht
			);

	g_free(ht);

out:
	g_string_append(str, "</div>");

	char *ret = str->str;
	g_string_free(str, false);
	return ret;
}
static char *makehintelm(Page *page, Elm *elm,
		const char* text, int len, double pagex, double pagey)
{
	char *tag = props(elm->elm, "tagName");
	bool center = isins(uritags, tag) && !isins(linktags, tag);
	g_free(tag);

	char *uri =
		attr(elm->elm, "ONCLICK") ?:
		attr(elm->elm, "HREF") ?:
		attr(elm->elm, "SRC");

	GString *str = g_string_new(NULL);

	let rects = invoker(elm->elm, "getClientRects");
	let rect;
	for (int j = 0; (rect = idx(rects, j)); j++)
	{
		double x = propd(rect, "left");
		double y = propd(rect, "top");
		double w = propd(rect, "width");
		double h = propd(rect, "height");
		g_object_unref(rect);

		_trim(&x, &w, &elm->x, &elm->w);
		_trim(&y, &h, &elm->y, &elm->h);

		char *hint = _makehintelm(page, center,
				y + elm->fy + pagey,
				x + elm->fx + pagex,
				h,
				w,
				uri, text, len, j == 0);

		g_string_append(str, hint);
		g_free(hint);
	}
	g_object_unref(rects);

	char *ret = g_strdup_printf(
			"<DIV style="
			"position:absolute;"
			"overflow:visible;"
			"top:0px;"
			"left:0px;"
			"height:10px;"
			"width:10px;"
			">%s</DIV>"
			, str->str);

	g_string_free(str, false);
	g_free(uri);

	return ret;
}


static gint getdigit(gint len, gint num)
{
	gint tmp = num;
	gint digit = 1;
	while ((tmp = tmp / len)) digit++;
	return digit;
}

static gchar *makekey(gchar *keys, gint len, gint max, gint tnum, gint digit)
{
	gchar ret[digit + 1];
	ret[digit] = '\0';

	gint llen = len;
	while (llen--)
		if (pow(llen, digit) < max) break;

	llen++;

	gint tmp = tnum;
	for (int i = digit - 1; i >= 0; i--)
	{
		ret[i] = toupper(keys[tmp % llen]);
		tmp = tmp / llen;
	}

	return g_strdup(ret);
}

static void rmhint(Page *page)
{
	if (!page->apnode) return;

	let doc = sdoc(page);
	let docelm = prop(doc, "documentElement");
	g_object_unref(docelm);

	if (page->apnode == docelm && page->apchild)
		invoke(page->apnode, "removeChild", aJ(page->apchild));

	if (page->apchild)
		g_object_unref(page->apchild);
	g_object_unref(page->apnode);
	g_free(page->apkeys);
	page->apchild = NULL;
	page->apnode = NULL;
	page->apkeys = NULL;
}

static void trim(Elm *te, Elm *prect)
{
	_trim(&te->x, &te->w, &prect->x, &prect->w);
	_trim(&te->y, &te->h, &prect->y, &prect->h);
}
static Elm checkelm(
		let win, Elm *frect, Elm *prect, let te, bool js, bool notttag)
{
	let dec = NULL;
	Elm ret = getrect(te);

	double bottom = ret.y + ret.h;
	double right  = ret.x + ret.w;

	if (
		(ret.y < 0        && bottom < 0       ) ||
		(ret.y > frect->h && bottom > frect->h) ||
		(ret.x < 0        && right  < 0       ) ||
		(ret.x > frect->w && right  > frect->w)
	)
		goto retfalse;

	ret.insight = true;


	//elms visibility hidden have size also opacity
	dec = invoker(win, "getComputedStyle", aJ(te));

	static gchar *check[][2] = {
		{"visibility", "hidden"},
		{"opacity"   , "0"},
		{"display"   , "none"},
	};
	for (int k = 0; k < sizeof(check) / sizeof(*check); k++)
		if (styleis(dec, check[k][0], check[k][1]))
			goto retfalse;


	gchar *zc = getstyleval(dec, "z-index");
	ret.zi = atoi(zc);
	g_free(zc);

	if (ret.zi > prect->zi || styleis(dec, "position", "absolute"))
		trim(&ret, frect);
	else
		trim(&ret, prect);

	if (js && (ret.h < 1 || ret.w < 1))
		goto retfalse;

	if (js && notttag && !styleis(dec, "cursor", "pointer"))
		goto retfalse;

	g_object_unref(dec);

	ret.elm = g_object_ref(te);
	ret.fx = frect->fx;
	ret.fy = frect->fy;
	ret.ok = true;

	return ret;

retfalse:
	clearelm(&ret);
	if (dec) g_object_unref(dec);
	return ret;
}

static bool addelm(Elm *pelm, GSList **elms)
{
	if (!pelm->ok) return false;
	Elm *elm = g_new(Elm, 1);
	*elm = *pelm;
	if (*elms) for (GSList *next = *elms; next; next = next->next)
	{
		if (elm->zi >= ((Elm *)next->data)->zi)
		{
			*elms = g_slist_insert_before(*elms, next, elm);
			break;
		}

		if (!next->next)
		{
			*elms = g_slist_append(*elms, elm);
			break;
		}
	}
	else
		*elms = g_slist_append(*elms, elm);

	return true;
}

static bool eachclick(let win, let cl,
		Coms type, GSList **elms, Elm *frect, Elm *prect)
{
	bool ret = false;

	let te;
	for (int j = 0; (te = idx(cl, j)); j++)
	{
		bool div = false;
		gchar *tag = props(te, "tagName");

		if (isins(clicktags, tag))
		{
			Elm elm = checkelm(win, frect, prect, te, true, false);
			if (elm.ok)
				addelm(&elm, elms);

			g_free(tag);
			g_object_unref(te);
			continue;
		} else if (!strcmp(tag, "DIV"))
			div = true; //div is random

		g_free(tag);

		Elm elm = checkelm(win, frect, prect, te, true, true);
		if (!elm.insight && (!div || elm.y > 1))
		{
			g_object_unref(te);
			continue;
		}

		let ccl = prop(te, "children");
		Elm *crect = prect;
		let dec = invoker(win, "getComputedStyle", aJ(te));

		g_object_unref(te);

		if (
				elm.zi > prect->zi ||
				styleis(dec, "overflow", "hidden") ||
				styleis(dec, "overflow", "scroll") ||
				styleis(dec, "overflow", "auto")
		)
			crect = &elm;

		g_object_unref(dec);

		if (eachclick(win, ccl, type, elms, frect, crect))
		{
			ret = true;
			g_object_unref(ccl);
			clearelm(&elm);
			continue;
		}
		g_object_unref(ccl);

		if (elm.ok)
		{
			ret = true;
			addelm(&elm, elms);
		}
	}
	return ret;
}
static GSList *_makelist(Page *page, let doc, let win,
		Coms type, GSList *elms, Elm *frect, Elm *prect)
{
	const gchar **taglist = clicktags; //Cclick
	if (type == Clink ) taglist = linktags;
	if (type == Curi  ) taglist = uritags;
	if (type == Cspawn) taglist = uritags;
	if (type == Crange) taglist = uritags;
	if (type == Ctext ) taglist = texttags;

	if (type == Cclick && page->script)
	{
		let body = prop(doc , "body");
		let cl = prop(body, "children");
		g_object_unref(body);
		eachclick(win, cl, type, &elms, frect, prect);
		g_object_unref(cl);
	}
	else for (const gchar **tag = taglist; *tag; tag++)
	{
		let cl = invoker(doc, "getElementsByTagName", aS(*tag));
		let te;

		for (int j = 0; (te = idx(cl, j)); j++)
		{
			Elm elm = checkelm(win, frect, prect, te, false, false);
			g_object_unref(te);

			if (elm.ok)
			{
				if (type == Ctext)
				{
					if (!isinput(elm.elm))
					{
						clearelm(&elm);
						continue;
					}

					invoke(elm.elm, "focus");
					clearelm(&elm);

					g_object_unref(win);
					g_object_unref(cl);
					return NULL;
				}

				addelm(&elm, &elms);
			}
		}
		g_object_unref(cl);
	}
	return elms;
}

static GSList *makelist(Page *page, let doc, let win,
		Coms type, Elm *frect, GSList *elms)
{
	Elm frectr = {0};
	if (!frect)
	{
		frectr.w = propd(win, "innerWidth");
		frectr.h = propd(win, "innerHeight");
		frect = &frectr;
	}
	Elm prect = *frect;
	prect.x = prect.y = 0;

	//D(rect %d %d %d %d, rect.y, rect.x, rect.h, rect.w)
	elms = _makelist(page, doc, win, type, elms, frect, &prect);

	let cl = invoker(doc, "getElementsByTagName", aS("IFRAME"));
	let te;
	for (int j = 0; (te = idx(cl, j)); j++)
	{
//some times can't get content
//		let fdoc = prop(te, "contentDocument");
//		if (!fdoc) continue;
		WebKitDOMHTMLIFrameElement *tfe = (void *)webkit_dom_node_for_js_value(te);
		let fdoc = webkit_frame_get_js_value_for_dom_object(page->mf,
			(void *)webkit_dom_html_iframe_element_get_content_document(tfe));

//fwin can't get style vals
//		let fwin = prop(fdoc, "defaultView");
//		let fwin = prop(te, "contentWindow");

//		Elm cfrect = checkelm(fwin, frect, &prect, te, false, false);
		Elm cfrect = checkelm(win, frect, &prect, te, false, false);
		if (cfrect.ok)
		{
			double cx = propd(te, "clientLeft");
			double cy = propd(te, "clientTop");
			double cw = propd(te, "clientWidth");
			double ch = propd(te, "clientHeight");

			cfrect.w = MIN(cfrect.w - cx, cw);
			cfrect.h = MIN(cfrect.h - cy, ch);

			cfrect.fx += cfrect.x + cx;
			cfrect.fy += cfrect.y + cy;
			cfrect.x = cfrect.y = 0;
//			elms = makelist(page, fdoc, fwin, type, &cfrect, elms);
			elms = makelist(page, fdoc, win, type, &cfrect, elms);
		}

		clearelm(&cfrect);
//		g_object_unref(fwin);
		g_object_unref(fdoc);
		g_object_unref(te);
	}
	g_object_unref(cl);

	return elms;
}

static void hintret(Page *page, Coms type, let te, bool hasnext)
{
	gchar uritype = 'l';
	gchar *uri = NULL;
	gchar *label = NULL;
	if (type == Curi || type == Cspawn || type == Crange)
	{
		uri = attr(te, "SRC");

		if (!uri)
		{
			let cl = prop(te, "children");
			let le;
			for (int j = 0; (le = idx(cl, j)); j++)
			{
				gchar *tag = props(le, "tagName");
				if (!g_strcmp0(tag, "SOURCE"))
					uri = attr(le, "SRC");

				g_object_unref(le);
				g_free(tag);

				if (uri) break;
			}

			g_object_unref(cl);
		}

		if (uri && (type == Cspawn || type == Crange))
		{
			gchar *tag = props(te, "tagName");
			if (!strcmp(tag, "IMG"))
				uritype = 'i';
			else
				uritype = 'm';

			g_free(tag);
		}
	}

	if (!uri)
		uri = attr(te, "HREF");

	if (!uri)
		uri = g_strdup("about:blank");

	label = props(te, "innerText") ?: attr(te, "ALT") ?: attr(te, "TITLE");

	let odoc = prop(te, "ownerDocument");
	gchar *ouri = props(odoc, "documentURI");
	g_object_unref(odoc);

	gchar *suri = tofull(te, uri);
	gchar *retstr = g_strdup_printf("%c%d%s %s %s", uritype, hasnext, ouri, suri, label);
	send(page, "_hintret", retstr);

	g_free(uri);
	g_free(label);
	g_free(ouri);
	g_free(suri);
	g_free(retstr);
}

static bool makehint(Page *page, Coms type, gchar *hintkeys, gchar *ipkeys)
{
	let doc = sdoc(page);
	page->lasttype = type;

	if (type != Cclick)
	{
		let dtype = prop(doc, "doctype");
		if (dtype)
		{
			gchar *name = props(dtype, "name");
			g_object_unref(dtype);
			if (name && strcmp("html", name))
			{
				g_free(name);

				//no elms may be;P
				gchar *retstr =
					g_strdup_printf("l0%s ", webkit_web_page_get_uri(page->kit));
				send(page, "_hintret", retstr);
				g_free(retstr);

				g_free(ipkeys);
				return false;
			}
			g_free(name);
		}
	}

	if (hintkeys)
	{
		g_free(page->lasthintkeys);
		page->lasthintkeys = g_strdup(hintkeys);
	}
	else
		hintkeys = page->lasthintkeys;
	if (strlen(hintkeys) < 3) hintkeys = HINTKEYS;

	rmhint(page);
	page->apkeys = ipkeys;

	let win = swin(page);
	double pagex = propd(win, "scrollX");
	double pagey = propd(win, "scrollY");

	GSList *elms = makelist(page, doc, win, type, NULL, NULL);
	guint tnum = g_slist_length(elms);

	page->apnode = prop(doc, "documentElement");
	GString *hintstr = g_string_new(NULL);

	gint keylen = strlen(hintkeys);
	gint iplen = ipkeys ? strlen(ipkeys) : 0;
	gint digit = getdigit(keylen, tnum);
	bool last = iplen == digit;
	elms = g_slist_reverse(elms);
	gint i = -1;
	bool ret = false;

	bool rangein = false;
	gint rangeleft = getsetint(page, "hintrangemax");
	let rangeend = NULL;

	//tab key
	bool focused = false;
	bool dofocus = ipkeys && ipkeys[strlen(ipkeys) - 1] == 9;
	if (dofocus)
		ipkeys[strlen(ipkeys) - 1] = '\0';

	gchar enterkey[2] = {0};
	*enterkey = (gchar)GDK_KEY_Return;
	bool enter = page->rangestart && !g_strcmp0(enterkey, ipkeys);
	if (type == Crange && (last || enter))
		for (GSList *next = elms; next; next = next->next)
	{
		Elm *elm = (Elm *)next->data;
		let te = elm->elm;

		rangein |= te == page->rangestart;

		i++;
		if (page->rangestart && !rangein)
			continue;

		if (enter)
		{
			rangeend = te;
			if (--rangeleft < 0) break;
			continue;
		}

		gchar *key = makekey(hintkeys, keylen, tnum, i, digit);
		if (!strcmp(key, ipkeys))
		{
			ipkeys = NULL;
			iplen = 0;
			g_free(page->apkeys);
			page->apkeys = NULL;

			if (!page->rangestart)
				page->rangestart = te;
			else
				rangeend = te;

			g_free(key);
			break;
		}
		g_free(key);
	}

	GSList *rangeelms = NULL;
	i = -1;
	rangein = false;
	rangeleft = getsetint(page, "hintrangemax");
	for (GSList *next = elms; next; next = next->next)
	{
		Elm *elm = (Elm *)next->data;
		let te = elm->elm;

		rangein |= te == page->rangestart;

		i++;
		gchar *key = makekey(hintkeys, keylen, tnum, i, digit);

		if (dofocus)
		{
			if (!focused &&
					(type != Crange || !page->rangestart || rangein) &&
					g_str_has_prefix(key, ipkeys ?: ""))
			{
				invoke(te, "focus");
				focused = true;
			}
		}
		else if (last && type != Crange)
		{
			if (!ret && !strcmp(key, ipkeys))
			{
				ret = true;

				invoke(te, "focus");
				if (type == Cclick)
				{
					bool isi = isinput(te);

					if (page->script && !isi)
					{
						let rects = invoker(elm->elm, "getClientRects");
						let rect = idx(rects, 0);
						double x = propd(rect, "left");
						double y = propd(rect, "top");
						double w = propd(rect, "width");
						double h = propd(rect, "height");
						g_object_unref(rect);
						g_object_unref(rects);

						gchar *arg = g_strdup_printf("%f:%f",
							x + elm->fx + w / 2.0 + 1.0,
							y + elm->fy + h / 2.0 + 1.0
						);
						send(page, "click", arg);
						g_free(arg);
					}
					else
					{
						if (!getsetbool(page,
									"javascript-can-open-windows-automatically")
								&& hasattr(te, "TARGET"))
							send(page, "showmsg", "The element has target, may have to type the enter key.");

						let ce = invoker(doc, "createEvent", aS("MouseEvent"));
						invoke(ce, "initEvent", aB(true), aB(true));
						invoke(te, "dispatchEvent", aJ(ce));
						g_object_unref(ce);
					}

					if (isi)
						send(page, "toinsert", NULL);
					else
						send(page, "tonormal", NULL);

				}
				else
				{
					hintret(page, type, te, false);
					send(page, "tonormal", NULL);
				}
			}
		}
		else if (rangeend && rangein)
		{
			rangeelms = g_slist_prepend(rangeelms, te);
		}
		else if (!page->rangestart || (rangein && !rangeend))
		{
			bool has = g_str_has_prefix(key, ipkeys ?: "");
			if (has || rangein)
			{
				char *ne = makehintelm(page,
						elm, has ? key : NULL, iplen, pagex, pagey);
				g_string_append(hintstr, ne);
				g_free(ne);
				ret |= has;
			}
		}

		g_free(key);
		clearelm(elm);
		g_free(elm);

		if (rangein)
			rangein = --rangeleft > 0 && rangeend != te;
	}

	if (hintstr->len)
	{
		page->apchild = invoker(doc, "createElement", aS("DIV"));
		setprop_s(page->apchild, "innerHTML", hintstr->str);
		invoke(page->apnode, "appendChild", aJ(page->apchild));
	}
	g_string_free(hintstr, true);

	for (GSList *next = rangeelms; next; next = next->next)
	{
		hintret(page, type, next->data, next->next);
		g_usleep(getsetint(page, "rangeloopusec"));
	}
	g_slist_free(rangeelms);

	g_slist_free(elms);

	return ret;
}


//@context
static void domfocusincb(let w, let e, Page *page)
{
	let doc = sdoc(page);
	let te = prop(doc, "activeElement");
	if (!te) return;

	gchar *href = attr(te, "HREF");
	gchar *uri = tofull(te, href);
	g_free(href);

	send(page, "_focusuri", uri);
	g_free(uri);
	g_object_unref(te);
}
static void domfocusoutcb(let w, let e, Page *page)
{ send(page, "_focusuri", NULL); }
//static void domactivatecb(Page *page)
//{ DD(domactivate!) }

static void rmtags(let doc, gchar *name)
{
	let cl = invoker(doc, "getElementsByTagName", aS(name));

	GSList *rms = NULL;
	let te;
	for (int i = 0; (te = idx(cl, i)); i++)
		rms = g_slist_prepend(rms, te);

	for (GSList *next = rms; next; next = next->next)
	{
		let pn = prop(next->data, "parentNode");
		invoke(pn, "removeChiled", aJ(next->data));
		g_object_unref(pn);
		g_object_unref(next->data);
	}

	g_slist_free(rms);
	g_object_unref(cl);
}
static void domloadcb(let w, let e, let doc)
{
	rmtags(doc, "NOSCRIPT");
}
static void hintcb(let w, let e, Page *page)
{
	if (page->apnode)
		makehint(page, page->lasttype, NULL, NULL);
}
static void unloadcb(let w, let e, Page *page)
{
	rmhint(page);
}
static void pagestart(Page *page)
{
	g_slist_free_full(page->black, g_free);
	g_slist_free_full(page->white, g_free);
	page->black = NULL;
	page->white = NULL;
}
static void frameon(Page *page, let win)
{
	let doc = prop(win, "document");
	if (!doc) return;

	page->emitters = g_slist_prepend(page->emitters, doc);

	if (getsetbool(page, "rmnoscripttag"))
	{
		rmtags(doc, "NOSCRIPT");
		//have to monitor DOMNodeInserted or?
		addlistener(doc, "DOMContentLoaded", domloadcb, doc);
	}

	addlistener(doc, "DOMFocusIn"  , domfocusincb , page);
	addlistener(doc, "DOMFocusOut" , domfocusoutcb, page);
//	addlistener(doc, "DOMActivate" , domactivatecb, page);

	//for hint
	addlistener(doc, "resize"      , hintcb  , page);
	addlistener(doc, "scroll"      , hintcb  , page);
	addlistener(doc, "beforeunload", unloadcb, page);
//may be heavy
//	addlistener(doc, "DOMSubtreeModified", hintcb, page);

	let cl = invoker(doc, "getElementsByTagName", aS("IFRAME"));
	let te;
	for (int i = 0; (te = idx(cl, i)); i++)
	{
		let fwin = prop(te, "contentWindow");
		frameon(page, fwin);

		g_object_unref(fwin);
		g_object_unref(te);
	}
	g_object_unref(cl);
}
static void pageon(Page *page, bool finished)
{
	g_slist_free_full(page->emitters, g_object_unref);
	page->emitters = NULL;
	let win = swin(page);
	frameon(page, win);


	if (!finished
		|| !g_str_has_prefix(webkit_web_page_get_uri(page->kit), APP":main")
		|| !g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL)
	)
		return;

	let doc = sdoc(page);
	let cl = invoker(doc, "getElementsByTagName", aS("IMG"));
	let te;
	for (int i = 0; (te = idx(cl, i)); i++)
	{
		gchar *uri = attr(te, "SRC");
		if (!g_strcmp0(uri, APP":F"))
		{
			let pe = prop(te, "parentElement");
			g_free(uri);
			uri = attr(pe, "HREF");
			gchar *esc = g_uri_escape_string(uri, NULL, true);
			gchar *f = g_strdup_printf(APP":f/%s", esc);
			g_free(esc);
			invoke(te, "setAttribute", aS("SRC"), aS(f));
			g_free(f);

			g_object_unref(pe);
		}
		g_free(uri);
		g_object_unref(te);
	}
	g_object_unref(cl);
}


//@misc com funcs
static void mode(Page *page)
{
	let doc = sdoc(page);
	let te = prop(doc, "activeElement");

	if (te && (isinput(te) || attrb(te, "contenteditable")))
		send(page, "toinsert", NULL);
	else
		send(page, "tonormal", NULL);

	g_object_unref(te);
}

static void focus(Page *page)
{
	let win = swin(page);
	let selection = invoker(win, "getSelection");

	let an =
		   prop(selection, "anchorNode")
		?: prop(selection, "focusNode" )
		?: prop(selection, "baseNode"  )
		?: prop(selection, "extentNode");

	if (an) do
	{
		let pe = prop(an, "parentElement");
		if (!pe) continue;

		gchar *tag = props(pe, "tagName");
		if (isins(clicktags , tag))
		{
			invoke(pe, "focus");
			g_free(tag);
			break;
		}
		g_free(tag);
	} while ((an = propunref(an, "parentNode")));

	g_object_unref(selection);
}

static void blur(Page *page)
{
	let doc = sdoc(page);
	let te = prop(doc, "activeElement");
	if (te)
		invoke(te, "blur");
	g_object_unref(te);

	let win = swin(page);
	let selection = invoker(win, "getSelection");
	invoke(selection, "empty");
	g_object_unref(selection);
}

static void halfscroll(Page *page, bool d)
{
	let win = swin(page); //static

	double h = propd(win, "innerHeight");
	invoke(win, "scrollTo",
			aD(propd(win, "scrollX")),
			aD(propd(win, "scrollY") + (d ? h/2 : - h/2)));
}

//@ipccb
void ipccb(const gchar *line)
{
	gchar **args = g_strsplit(line, ":", 3);

	Page *page = NULL;
	long lid = atol(args[0]);
	for (int i = 0; i < pages->len; i++)
		if (((Page *)pages->pdata[i])->id == lid)
			page = pages->pdata[i];

	if (!page) return;

	Coms type = *args[1];
	gchar *arg = args[2];

	gchar *ipkeys = NULL;
	switch (type) {
	case Cload:
		loadconf();
		break;
	case Coverset:
		GFA(page->overset, g_strdup(*arg ? arg : NULL))
		resetconf(page, webkit_web_page_get_uri(page->kit), true);
		if (page->apnode)
			makehint(page, page->lasttype, NULL, g_strdup(page->apkeys));
		break;
	case Cstart:
		pagestart(page);
		break;
	case Con:
		g_strfreev(page->refreq);
		page->refreq = NULL;

		pageon(page, *arg == 'f');
		break;

	case Ckey:
	{
		gchar key[2] = {0};
		key[0] = toupper(arg[0]);
		ipkeys = page->apkeys ?
			g_strconcat(page->apkeys, key, NULL) : g_strdup(key);

		type = page->lasttype;
		arg = NULL;
	}
	case Cclick:
	case Clink:
	case Curi:
	case Cspawn:
	case Crange:
		if (arg)
		{
			page->rangestart = NULL;
			page->script = *arg == 'y';
		}

gint64 start = g_get_monotonic_time();
		if (!makehint(page, type, confcstr("hintkeys"), ipkeys))
		{
			send(page, "showmsg", "No hint");
			send(page, "tonormal", NULL);
		}
D(time %f, (g_get_monotonic_time() - start) / 1000000.0)
		break;
	case Ctext:
	{
		makelist(page, sdoc(page), swin(page), Ctext, NULL, NULL);
		break;
	}
	case Crm:
		rmhint(page);
		break;

	case Cmode:
		mode(page);
		break;

	case Cfocus:
		focus(page);
		break;

	case Cblur:
		blur(page);
		break;

	case Cwhite:
		if (*arg == 'r') setwblist(true);
		if (*arg == 'n') setwblist(false);
		if (*arg == 'w') showwhite(page, true);
		if (*arg == 'b') showwhite(page, false);
		break;

	case Ctlget:
		textlinkget(page, arg);
		break;
	case Ctlset:
		textlinkset(page, arg);
		break;

	case Cwithref:
		g_strfreev(page->refreq);
		page->refreq = g_strsplit(arg, " ", 2);
		break;

	case Cscroll:
		halfscroll(page, *arg == 'd');
		break;
	}

	g_strfreev(args);

	if (page && page->sync)
		g_main_loop_quit(page->sync);
}


//@page cbs
static void headerout(const char *name, const char *value, gpointer p)
{
	g_print("%s : %s\n", name, value);
}
static gboolean reqcb(
		WebKitWebPage *p,
		WebKitURIRequest *req,
		WebKitURIResponse *res,
		Page *page)
{
	page->pagereq++;
	const gchar *reqstr = webkit_uri_request_get_uri(req);
	if (g_str_has_prefix(reqstr, APP":"))
	{
		if (!strcmp(reqstr, APP":F"))
			return true;
		return false;
	}
	const gchar *pagestr = webkit_web_page_get_uri(page->kit);
	SoupMessageHeaders *head = webkit_uri_request_get_http_headers(req);

	bool ret = false;
	int check = checkwb(reqstr);
	if (check == 0)
		ret = true;
	else if (check == -1 && getsetbool(page, "adblock"))
	{
		bool (*checkf)(const char *, const char *) =
			g_object_get_data(G_OBJECT(page->kit), APP"check");
		if (checkf)
			ret = !checkf(reqstr, pagestr);
	}
	if (ret) goto out;

	if (res && page->pagereq == 2)
	{//redirect. pagereq == 2 means it is a top level request
		//in redirection we don't get yet current uri
		resetconf(page, reqstr, false);
		page->pagereq = 1;
		page->redirected = true;
		goto out;
	}

	if (check == 1 //white
		|| page->pagereq == 1 //open page request
		|| !head
		|| !getsetbool(page, "reldomaindataonly")
		|| !soup_message_headers_get_list(head, "Referer")
	) goto out;

	//reldomainonly
	SoupURI *puri = soup_uri_new(pagestr);
	const gchar *phost = soup_uri_get_host(puri);
	if (phost)
	{
		gchar **cuts = g_strsplit(
				getset(page, "reldomaincutheads") ?: "", ";", -1);
		for (gchar **cut = cuts; *cut; cut++)
			if (g_str_has_prefix(phost, *cut))
			{
				phost += strlen(*cut);
				break;
			}
		g_strfreev(cuts);

		SoupURI *ruri = soup_uri_new(reqstr);
		const gchar *rhost = soup_uri_get_host(ruri);

		ret = rhost && !g_str_has_suffix(rhost, phost);

		soup_uri_free(ruri);
	}
	soup_uri_free(puri);

out:
	if (ret)
		addwhite(page, reqstr);
	else
		addblack(page, reqstr);

	if (!ret && head)
	{
		if (page->pagereq == 1 && (page->setagent || page->setagentprev))
			soup_message_headers_replace(head, "User-Agent",
					getset(page, "user-agent") ?: "");

		gchar *rmhdrs = getset(page, "removeheaders");
		if (rmhdrs)
		{
			gchar **rms = g_strsplit(rmhdrs, ";", -1);
			for (gchar **rm = rms; *rm; rm++)
				soup_message_headers_remove(head, *rm);
			g_strfreev(rms);
		}
	}

	if (page->refreq && !g_strcmp0(page->refreq[1], reqstr))
	{
		if (!ret && head)
			soup_message_headers_append(head, "Referer", page->refreq[0]);
		g_strfreev(page->refreq);
		page->refreq = NULL;
	}

	if (!ret && getsetbool(page, "stdoutheaders"))
	{
		if (res)
		{
			g_print("RESPONSE: %s\n", webkit_uri_response_get_uri(res));
			soup_message_headers_foreach(
					webkit_uri_response_get_http_headers(res), headerout, NULL);
			g_print("\n");
		}
		g_print("REQUEST: %s\n", reqstr);
		if (head)
			soup_message_headers_foreach(head, headerout, NULL);
		g_print("\n");
	}

	return ret;
}
//static void formcb(WebKitWebPage *page, GPtrArray *elms, gpointer p) {}
//static void loadcb(WebKitWebPage *kp, Page *page) {}
static void uricb(Page* page)
{
	//workaround: when in redirect change uri delays
	if (page->redirected)
		page->pagereq = 1;
	else
	{
		page->pagereq = 0;
		resetconf(page, webkit_web_page_get_uri(page->kit), false);
	}
	page->redirected = false;
}

static void initpage(WebKitWebExtension *ex, WebKitWebPage *kp)
{
	Page *page = g_new0(Page, 1);
	g_object_weak_ref(G_OBJECT(kp), (GWeakNotify)freepage, page);
	page->kit = kp;
	page->mf = webkit_web_page_get_main_frame(kp);

	page->id = webkit_web_page_get_id(kp);
	page->seto = g_object_new(G_TYPE_OBJECT, NULL);
	g_ptr_array_add(pages, page);

	wbpath = path2conf("whiteblack.conf");
	setwblist(false);

	gchar *name = NULL;
	if (!shared)
	{
		name = g_strdup_printf("%"G_GUINT64_FORMAT, page->id);
		ipcwatch(name);
	}
	loadconf();
	send(page, "_setreq", NULL);

	GMainContext *ctx = g_main_context_new();
	page->sync = g_main_loop_new(ctx, true);
	GSource *watch = _ipcwatch(shared ? "ext" : name, ctx);
	g_free(name);

	g_main_loop_run(page->sync);

	g_source_unref(watch);
	g_main_context_unref(ctx);
	g_main_loop_unref(page->sync);
	page->sync = NULL;

//	SIG( page->kit, "context-menu"            , contextcb, NULL);
	SIG( page->kit, "send-request"            , reqcb    , page);
//	SIG( page->kit, "document-loaded"         , loadcb   , page);
	SIGW(page->kit, "notify::uri"             , uricb    , page);
//	SIG( page->kit, "form-controls-associated", formcb   , NULL);
}
G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(
		WebKitWebExtension *ex, const GVariant *v)
{
	const gchar *str = g_variant_get_string((GVariant *)v, NULL);
	fullname = g_strdup(g_strrstr(str, ";") + 1);
	shared = fullname[0] == 's';
	fullname = fullname + 1;

	if (shared)
		ipcwatch("ext");

	pages = g_ptr_array_new();

	SIG(ex, "page-created", initpage, NULL);
}

