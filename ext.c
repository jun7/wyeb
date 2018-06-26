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


//Make sure JSC is 4 times slower and lacks features we using
//So even JSC is true, there are the DOM funcs left and slow

#if ! JSC + 0
#undef JSC
#define JSC 0
#endif

#if JSC
#define let JSCValue *
#else
#define let void *
#endif

#include <ctype.h>
#include <webkit2/webkit-web-extension.h>

typedef struct _WP {
	WebKitWebPage *kit;
	guint64        id;
#if JSC
	WebKitFrame   *mf;
#endif
	let            apnode;
	let            apchild;
	char          *apkeys;

	char           lasttype;
	char          *lasthintkeys;
	char          *rangestart;
	bool           script;
	GSList        *black;
	GSList        *white;
	GSList        *emitters;

	int            pagereq;
	bool           redirected;
	char         **refreq;

	//conf
	GObject       *seto;
	char          *lasturiconf;
	char          *lastreset;
	char          *overset;
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
static void resetconf(Page *page, const char *uri, bool force)
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
	g_free(page->rangestart);

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

static const char *clicktags[] = {
	"INPUT", "TEXTAREA", "SELECT", "BUTTON", "A", "AREA", NULL};
static const char *linktags[] = {
	"A", "AREA", NULL};
static const char *uritags[] = {
	"A", "AREA", "IMG", "VIDEO", "AUDIO", NULL};

static const char *texttags[] = {
	"INPUT", "TEXTAREA", NULL};

static const char *inputtags[] = {
	"INPUT", "TEXTAREA", "SELECT", NULL};

// input types
/*
static const char *itext[] = { //no name is too
	"search", "text", "url", "email",  "password", "tel", NULL
};
static const char *ilimitedtext[] = {
	"month",  "number", "time", "week", "date", "datetime-local", NULL
};
*/
static const char *inottext[] = {
	"color", "file", "radio", "range", "checkbox", "button", "reset", "submit",

	// unknown
	"image", //to be submit
	// not focus
	"hidden",
	NULL
};


#if JSC
static void __attribute__((constructor)) ext22()
{ DD("this is ext22\n"); }


static JSCValue *pagejsv(Page *page, char *name)
{
	return jsc_context_get_value(webkit_frame_get_js_context(page->mf), name);
}
static JSCValue *sdoc(Page *page)
{
	static JSCValue *s = NULL;
	if (s) g_object_unref(s);
	return s = pagejsv(page, "document");
}

#define invoker(...) jsc_value_object_invoke_method(__VA_ARGS__, G_TYPE_NONE)
#define invoke(...) g_object_unref(invoker(__VA_ARGS__))
#define isdef(v) (!jsc_value_is_undefined(v) && !jsc_value_is_null(v))

#define aB(s) G_TYPE_BOOLEAN, s
#define aL(s) G_TYPE_LONG, s
#define aD(s) G_TYPE_DOUBLE, s
#define aS(s) G_TYPE_STRING, s
#define aJ(s) JSC_TYPE_VALUE, s

static let prop(let v, char *name)
{
	let retv = jsc_value_object_get_property(v, name);
	if (isdef(retv)) return retv;
	g_object_unref(retv);
	return NULL;
}
static double propd(let v, char *name)
{
	let retv = jsc_value_object_get_property(v, name);
	double ret = jsc_value_to_double(retv);
	g_object_unref(retv);
	return ret;
}
static char *props(let v, char *name)
{
	let retv = jsc_value_object_get_property(v, name);
	char *ret = isdef(retv) ? jsc_value_to_string(retv) : NULL;
	g_object_unref(retv);
	return ret;
}
static void setprop_s(let v, char *name, char *data)
{
	let dv = jsc_value_new_string(jsc_value_get_context(v), data);
	jsc_value_object_set_property(v, name, dv);
	g_object_unref(dv);
}
static char *attr(let v, char *name)
{
	let retv = invoker(v, "getAttribute", aS(name));
	char *ret = isdef(retv) ? jsc_value_to_string(retv) : NULL;
	g_object_unref(retv);
	return ret;
}


static void __attribute__((unused)) proplist(JSCValue *v)
{
	if (jsc_value_is_undefined(v))
	{
		DD(undefined value)
		return;
	}

	char **ps = jsc_value_object_enumerate_properties(v);
	if (ps)
		for (char **pr = ps; *pr; pr++)
			D(p %s, *pr)
	else
		DD(no props)
	g_strfreev(ps);
}

#define jscunref(v) if (v) g_object_unref(v)
#define docelm(v) prop(v, "documentElement")
#define focuselm(t) invoke(t, "focus")
#define getelms(doc, name) invoker(doc, "getElementsByTagName", aS(name))
#define defaultview(doc) prop(doc, "defaultView")


//JSC
#else
//DOM


#define defaultview(doc) webkit_dom_document_get_default_view(doc)
#define getelms(doc, name) \
	webkit_dom_document_get_elements_by_tag_name_as_html_collection(doc, name)
#define focuselm(t) webkit_dom_element_focus(t)
#define docelm(v) webkit_dom_document_get_document_element(v)
#define jscunref(v) ;

#define attr webkit_dom_element_get_attribute
#define sdoc(v) webkit_web_page_get_dom_document(v->kit)

#endif

static void clearelm(Elm *elm)
{
	if (elm->elm) g_object_unref(elm->elm);
}

static char *stag(let elm)
{
	if (!elm) return NULL;
	static char *name = NULL;
	g_free(name);
#if JSC
	name = props(elm, "tagName");
#else
	name = webkit_dom_element_get_tag_name(elm);
#endif
	return name;
}
static bool attrb(let v, char *name)
{
	return !g_strcmp0(sfree(attr(v, name)), "true");
}
static let idx(let cl, int i)
{
#if JSC
	char buf[9];
	snprintf(buf, 9, "%d", i);
	return prop(cl, buf);
#else
	return webkit_dom_html_collection_item(cl, i);
#endif

//	let retv = jsc_value_object_get_property_at_index(cl, i);
//	if (isdef(retv))
//		return retv;
//	g_object_unref(retv);
//	return NULL;
}

static let activeelm(let doc)
{
#if JSC
	let te = prop(doc, "activeElement");
#else
	let te = webkit_dom_document_get_active_element(doc);
#endif

	if (te && (!g_strcmp0(stag(te), "IFRAME") || !g_strcmp0(stag(te), "BODY")))
	{
		jscunref(te);
		return NULL;
	}
	return te;
}

static void recttovals(let rect, double *x, double *y, double *w, double *h)
{
#if JSC
	*x = propd(rect, "left");
	*y = propd(rect, "top");
	*w = propd(rect, "width");
	*h = propd(rect, "height");
#else
	*x = webkit_dom_client_rect_get_left(rect);
	*y = webkit_dom_client_rect_get_top(rect);
	*w = webkit_dom_client_rect_get_width(rect);
	*h = webkit_dom_client_rect_get_height(rect);
#endif
}



//@misc
static void send(Page *page, char *action, const char *arg)
{
	//D(send to main %s, ss)
	ipcsend("main", g_strdup_printf(
		"%"G_GUINT64_FORMAT":%s:%s", page->id, action, arg ?: ""));
}
static bool isins(const char **ary, char *val)
{
	if (!val) return false;
	for (;*ary; ary++)
		if (!strcmp(val, *ary)) return true;
	return false;
}
static bool isinput(let te)
{
	char *tag = stag(te);
	if (isins(inputtags, tag))
	{
		if (strcmp(tag, "INPUT"))
			return true;
		else if (!isins(inottext, sfree(attr(te, "TYPE"))))
			return true;
	}
	return false;
}
static char *tofull(let te, char *uri)
{
	if (!te || !uri) return NULL;
#if JSC
	char *bases = props(te, "baseURI");
#else
	char *bases = webkit_dom_node_get_base_uri((WebKitDOMNode *)te);
#endif
	SoupURI *base = soup_uri_new(bases);
	SoupURI *full = soup_uri_new_with_base(base, uri);

	char *ret = soup_uri_to_string(full, false);

	g_free(bases);
	soup_uri_free(base);
	soup_uri_free(full);
	return ret;
}

//func is void *(*func)(let doc, Page *page)
static void *_eachframes(let doc, Page *page, void *func)
{
	void *ret;
	if ((ret = ((void *(*)(let doc, Page *page))func)(doc, page))) return ret;

	let cl = getelms(doc, "IFRAME");
	let te;
	for (int i = 0; (te = idx(cl, i)); i++)
	{
#if JSC
		WebKitDOMHTMLIFrameElement *tfe = (void *)webkit_dom_node_for_js_value(te);
		let fdoc = webkit_frame_get_js_value_for_dom_object(page->mf,
			(void *)webkit_dom_html_iframe_element_get_content_document(tfe));
#else
		let fdoc = webkit_dom_html_iframe_element_get_content_document(te);
#endif

		ret = _eachframes(fdoc, page, func);

		jscunref(fdoc);
		jscunref(te);
		if (ret) break;
	}
	g_object_unref(cl);

	return ret;
}
static void *eachframes(Page *page, void *func)
{
	return _eachframes(sdoc(page), page, func);
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
static char   *wbpath = NULL;
static void setwblist(bool reload)
{
	if (wblist)
		g_slist_free_full(wblist, (GDestroyNotify)clearwb);
	wblist = NULL;

	if (!g_file_test(wbpath, G_FILE_TEST_EXISTS)) return;

	GIOChannel *io = g_io_channel_new_file(wbpath, "r", NULL);
	char *line;
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
static int checkwb(const char *uri) // -1 no result, 0 black, 1 white;
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
static void addwhite(Page *page, const char *uri)
{
	//D(blocked %s, uri)
	if (getsetbool(page, "showblocked"))
		send(page, "_blocked", uri);
	page->white = g_slist_prepend(page->white, g_strdup(uri));
}
static void addblack(Page *page, const char *uri)
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

	char pre = white ? 'w' : 'b';
	fprintf(f, "\n# %s in %s\n",
			white ? "blocked" : "loaded",
			webkit_web_page_get_uri(page->kit));

	list = g_slist_reverse(g_slist_copy(list));
	for (GSList *next = list; next; next = next->next)
		fputs(sfree(g_strdup_printf(
			"%c^%s\n", pre, sfree(regesc(next->data)))), f);

	g_slist_free(list);

	fclose(f);

	send(page, "openeditor", wbpath);
}


//@textlink
static let tldoc;
#if JSC
static let tlelm;
#else
static WebKitDOMHTMLTextAreaElement *tlelm;
static WebKitDOMHTMLInputElement *tlielm;
#endif
static void textlinkset(Page *page, char *path)
{
	let doc = sdoc(page);
	let cdoc = docelm(doc);
	jscunref(cdoc);
	if (tldoc != cdoc) return;

	GIOChannel *io = g_io_channel_new_file(path, "r", NULL);
	char *text;
	g_io_channel_read_to_end(io, &text, NULL, NULL);
	g_io_channel_unref(io);

#if JSC
	setprop_s(tlelm, "value", text);
#else
	if (tlelm)
		webkit_dom_html_text_area_element_set_value(tlelm, text);
	else
		webkit_dom_html_input_element_set_value(tlielm, text);
#endif
	g_free(text);
}
static void textlinkget(Page *page, char *path)
{
	let te = eachframes(page, activeelm);

#if JSC
	if (tlelm) g_object_unref(tlelm);
	if (tldoc) g_object_unref(tldoc);
	tlelm = NULL;
	tldoc = NULL;

	if (isinput(te))
		tlelm = te;
#else

	tlelm = NULL;
	tlielm = NULL;

	if (!strcmp(stag(te), "TEXTAREA"))
		tlelm = (WebKitDOMHTMLTextAreaElement *)te;
	else if (isinput(te))
		tlielm = (WebKitDOMHTMLInputElement *)te;
#endif
	else
	{
		send(page, "showmsg", "Not a text");
		return;
	}

	let doc = sdoc(page);
	tldoc = docelm(doc);
	char *text =
#if JSC
		props(tlelm, "value");
#else
		tlelm ?
		webkit_dom_html_text_area_element_get_value(tlelm) :
		webkit_dom_html_input_element_get_value(tlielm);
#endif

	GIOChannel *io = g_io_channel_new_file(path, "w", NULL);
	g_io_channel_write_chars(io, text ?: "", -1, NULL, NULL);
	g_io_channel_unref(io);
	g_free(text);

	send(page, "_textlinkon", NULL);
}


//@hinting
#if JSC
static char *getstyleval(let style, char *name)
{
	char *ret = NULL;
	let retv = invoker(style, "getPropertyValue", aS(name));
	ret = jsc_value_to_string(retv);
	g_object_unref(retv);
	return ret;
}
#else
#define getstyleval webkit_dom_css_style_declaration_get_property_value
#endif
static bool styleis(let dec, char* name, char *pval)
{
	char *val = getstyleval(dec, name);
	bool ret = (val && !strcmp(pval, val));
	g_free(val);

	return ret;
}

static Elm getrect(let te)
{
	Elm elm = {0};

#if V18
#if JSC
	let rect = invoker(te, "getBoundingClientRect");
#else
	WebKitDOMClientRect *rect =
		webkit_dom_element_get_bounding_client_rect(te);
#endif
	recttovals(rect, &elm.x, &elm.y, &elm.w, &elm.h);
	g_object_unref(rect);

#else
	elm.h = webkit_dom_element_get_offset_height(te);
	elm.w = webkit_dom_element_get_offset_width(te);

	for (WebKitDOMElement *le = te; le;
			le = webkit_dom_element_get_offset_parent(le))
	{
		elm.y +=
			webkit_dom_element_get_offset_top(le) -
			webkit_dom_element_get_scroll_top(le);
		elm.x +=
			webkit_dom_element_get_offset_left(le) -
			webkit_dom_element_get_scroll_left(le);
	}
#endif

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

	return g_string_free(str, false);
}
static char *makehintelm(Page *page, Elm *elm,
		const char* text, int len, double pagex, double pagey)
{
	char *tag = stag(elm->elm);
	bool center = isins(uritags, tag) && !isins(linktags, tag);

	char *uri =
		attr(elm->elm, "ONCLICK") ?:
		attr(elm->elm, "HREF") ?:
		attr(elm->elm, "SRC");

#if V18
	GString *str = g_string_new(NULL);

#if JSC
	let rects = invoker(elm->elm, "getClientRects");
	let rect;
	for (int i = 0; (rect = idx(rects, i)); i++)
	{
#else
	WebKitDOMClientRectList *rects = webkit_dom_element_get_client_rects(elm->elm);
	gulong l = webkit_dom_client_rect_list_get_length(rects);
	for (gulong i = 0; i < l; i++)
	{
		WebKitDOMClientRect *rect =
			webkit_dom_client_rect_list_item(rects, i);
#endif
		double x, y, w, h;
		recttovals(rect, &x, &y, &w, &h);
		jscunref(rect);

		_trim(&x, &w, &elm->x, &elm->w);
		_trim(&y, &h, &elm->y, &elm->h);


		g_string_append(str, sfree(
			_makehintelm(page, center,
					y + elm->fy + pagey,
					x + elm->fx + pagex,
					h,
					w,
					uri, text, len, i == 0)
		));
	}
	g_object_unref(rects);

	char *ret = g_string_free(str, false);

#else
	char *ret = _makehintelm(page, doc, center,
			elm->y + elm->fy + pagey,
			elm->x + elm->fx + pagex, elm->h, elm->w, uri, text, len, true);

#endif
	g_free(uri);

	return ret;
}


static int getdigit(int len, int num)
{
	int tmp = num - 1;
	int digit = 1;
	while ((tmp = tmp / len)) digit++;
	return digit;
}

static char *makekey(char *keys, int len, int max, int tnum, int digit)
{
	char ret[digit + 1];
	ret[digit] = '\0';

	int llen = len;
	while (llen--)
		if (pow(llen, digit) < max) break;

	llen++;

	int tmp = tnum;
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
	let cdoc = docelm(doc);
	jscunref(cdoc);

	if (page->apnode == cdoc && page->apchild)
#if JSC
		invoke(page->apnode, "removeChild", aJ(page->apchild));
#else
		webkit_dom_node_remove_child(page->apnode, page->apchild, NULL);
#endif

	jscunref(page->apnode);
	jscunref(page->apchild);
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
static Elm checkelm(let win, Elm *frect, Elm *prect, let te,
		bool js, bool notttag)
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
#if JSC
	dec = invoker(win, "getComputedStyle", aJ(te));
#else
	dec = webkit_dom_dom_window_get_computed_style(win, te, NULL);
#endif

	static char *check[][2] = {
		{"visibility", "hidden"},
		{"opacity"   , "0"},
		{"display"   , "none"},
	};
	for (int k = 0; k < sizeof(check) / sizeof(*check); k++)
		if (styleis(dec, check[k][0], check[k][1]))
			goto retfalse;


#if ! V18
	if (styleis(dec, "display", "inline"))
	{
		WebKitDOMElement *le = te;
		while ((le = webkit_dom_node_get_parent_element((WebKitDOMNode *)le)))
		{
			WebKitDOMCSSStyleDeclaration *decp =
				webkit_dom_dom_window_get_computed_style(win, le, NULL);
			if (!styleis(decp, "display", "inline"))
			{
				Elm rectp = getrect(le);
				double nr = MIN(right, rectp.x + rectp.w);
				ret.w += nr - right;
				right = nr;
				break;
			}
			g_object_unref(decp);
		}
	}
#endif

	ret.zi = atoi(sfree(getstyleval(dec, "z-index")));

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
		char *tag = stag(te);
		if (isins(clicktags, tag))
		{
			Elm elm = checkelm(win, frect, prect, te, true, false);
			if (elm.ok)
				addelm(&elm, elms);

			jscunref(te);
			continue;
		} else if (!strcmp(tag, "DIV"))
			div = true; //div is random

		Elm elm = checkelm(win, frect, prect, te, true, true);
		if (!elm.insight && (!div || elm.y > 1))
		{
			jscunref(te);
			continue;
		}

		Elm *crect = prect;
#if JSC
		let ccl = prop(te, "children");
		let dec = invoker(win, "getComputedStyle", aJ(te));
#else
		WebKitDOMHTMLCollection *ccl = webkit_dom_element_get_children(te);
		WebKitDOMCSSStyleDeclaration *dec =
			webkit_dom_dom_window_get_computed_style(win, te, NULL);
#endif
		jscunref(te);
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
	const char **taglist = clicktags; //Cclick
	if (type == Clink ) taglist = linktags;
	if (type == Curi  ) taglist = uritags;
	if (type == Cspawn) taglist = uritags;
	if (type == Crange) taglist = uritags;
	if (type == Ctext ) taglist = texttags;

	if (type == Cclick && page->script)
	{
#if JSC
		let body = prop(doc , "body");
		let cl = prop(body, "children");
		g_object_unref(body);
#else
		WebKitDOMHTMLCollection *cl = webkit_dom_element_get_children(
				(WebKitDOMElement *)webkit_dom_document_get_body(doc));
#endif
		eachclick(win, cl, type, &elms, frect, prect);
		g_object_unref(cl);
	}
	else for (const char **tag = taglist; *tag; tag++)
	{
		let cl = getelms(doc, *tag);
		let te;
		for (int j = 0; (te = idx(cl, j)); j++)
		{
			Elm elm = checkelm(win, frect, prect, te, false, false);
			jscunref(te);
			if (elm.ok)
			{
				if (type == Ctext)
				{
					if (!isinput(elm.elm))
					{
						clearelm(&elm);
						continue;
					}

					focuselm(elm.elm);
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
#if JSC
		frectr.w = propd(win, "innerWidth");
		frectr.h = propd(win, "innerHeight");
#else
		frectr.w = webkit_dom_dom_window_get_inner_width(win);
		frectr.h = webkit_dom_dom_window_get_inner_height(win);
#endif
		frect = &frectr;
	}
	Elm prect = *frect;
	prect.x = prect.y = 0;

	//D(rect %d %d %d %d, rect.y, rect.x, rect.h, rect.w)
	elms = _makelist(page, doc, win, type, elms, frect, &prect);

	let cl = getelms(doc, "IFRAME");
	let te;
	for (int j = 0; (te = idx(cl, j)); j++)
	{
		Elm cfrect = checkelm(win, frect, &prect, te, false, false);
		if (cfrect.ok)
		{
#if JSC
			double cx = propd(te, "clientLeft");
			double cy = propd(te, "clientTop");
			double cw = propd(te, "clientWidth");
			double ch = propd(te, "clientHeight");
#else
			double cx = webkit_dom_element_get_client_left(te);
			double cy = webkit_dom_element_get_client_top(te);
			double cw = webkit_dom_element_get_client_width(te);
			double ch = webkit_dom_element_get_client_height(te);
#endif

			cfrect.w = MIN(cfrect.w - cx, cw);
			cfrect.h = MIN(cfrect.h - cy, ch);

			cfrect.fx += cfrect.x + cx;
			cfrect.fy += cfrect.y + cy;
			cfrect.x = cfrect.y = 0;

#if JSC
			//some times can't get content
			//let fdoc = prop(te, "contentDocument");
			//if (!fdoc) continue;
			WebKitDOMHTMLIFrameElement *tfe =
				(void *)webkit_dom_node_for_js_value(te);
			let fdoc = webkit_frame_get_js_value_for_dom_object(page->mf,
				(void *)webkit_dom_html_iframe_element_get_content_document(tfe));

			//fwin can't get style vals
			//let fwin = prop(fdoc, "defaultView");
			//et fwin = prop(te, "contentWindow");
			let fwin = g_object_ref(win);
#else
			WebKitDOMDocument *fdoc =
				webkit_dom_html_iframe_element_get_content_document(te);
			WebKitDOMDOMWindow *fwin = defaultview(fdoc);
#endif

			elms = makelist(page, fdoc, win, type, &cfrect, elms);
			g_object_unref(fwin);
			jscunref(fdoc);
		}

		jscunref(te);
		clearelm(&cfrect);
	}
	g_object_unref(cl);

	return elms;
}

static void hintret(Page *page, Coms type, let te, bool hasnext)
{
	char uritype = 'l';
	char *uri = NULL;
	char *label = NULL;
	if (type == Curi || type == Cspawn || type == Crange)
	{
		uri = attr(te, "SRC");

		if (!uri)
		{
#if JSC
			let cl = prop(te, "children");
#else
			WebKitDOMHTMLCollection *cl = webkit_dom_element_get_children(te);
#endif
			let le;
			for (int j = 0; (le = idx(cl, j)); j++)
			{
				if (!g_strcmp0(stag(le), "SOURCE"))
					uri = attr(le, "SRC");

				jscunref(le);
				if (uri) break;
			}

			g_object_unref(cl);
		}

		if (uri && (type == Cspawn || type == Crange))
		{
			if (!strcmp(stag(te), "IMG"))
				uritype = 'i';
			else
				uritype = 'm';
		}
	}

	if (!uri)
		uri = attr(te, "HREF");

	if (!uri)
		uri = g_strdup("about:blank");

	label =
#if JSC
		props(te, "innerText") ?:
#else
		webkit_dom_html_element_get_inner_text((WebKitDOMHTMLElement *)te) ?:
#endif
		attr(te, "ALT") ?:
		attr(te, "TITLE");

#if JSC
	let odoc = prop(te, "ownerDocument");
	char *ouri = props(odoc, "documentURI");
	g_object_unref(odoc);
#else
	WebKitDOMDocument *odoc = webkit_dom_node_get_owner_document((void *)te);
	char *ouri = webkit_dom_document_get_document_uri(odoc);
#endif

	char *suri = tofull(te, uri);
	char *retstr = g_strdup_printf("%c%d%s %s %s", uritype, hasnext, ouri, suri, label);
	send(page, "_hintret", retstr);

	g_free(uri);
	g_free(label);
	g_free(ouri);
	g_free(suri);
	g_free(retstr);
}

static bool makehint(Page *page, Coms type, char *hintkeys, char *ipkeys)
{
	let doc = sdoc(page);
	page->lasttype = type;

	if (type != Cclick)
	{
#if JSC
		let dtype = prop(doc, "doctype");
		char *name = NULL;
		if (dtype)
		{
			name = props(dtype, "name");
			g_object_unref(dtype);
		}
		if (name && strcmp("html", name))
		{
			g_free(name);
#else
		WebKitDOMDocumentType *dtype = webkit_dom_document_get_doctype(doc);
		if (dtype && strcmp("html", webkit_dom_document_type_get_name(dtype)))
		{
#endif
			//no elms may be;P
			send(page, "_hintret", sfree(g_strdup_printf(
							"l0%s ", webkit_web_page_get_uri(page->kit))));

			g_free(ipkeys);
			return false;
		}
#if JSC
		g_free(name);
#endif
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

	let win = defaultview(doc);
#if JSC
	double pagex = propd(win, "scrollX");
	double pagey = propd(win, "scrollY");
#else
	double pagex = webkit_dom_dom_window_get_scroll_x(win);
	double pagey = webkit_dom_dom_window_get_scroll_y(win);
#endif
	GSList *elms = makelist(page, doc, win, type, NULL, NULL);
	g_object_unref(win);

	guint tnum = g_slist_length(elms);

	page->apnode = docelm(doc);
	GString *hintstr = g_string_new(NULL);

	int  keylen = strlen(hintkeys);
	int  iplen = ipkeys ? strlen(ipkeys) : 0;
	int  digit = getdigit(keylen, tnum);
	bool last = iplen == digit;
	elms = g_slist_reverse(elms);
	int  i = -1;
	bool ret = false;

	bool rangein = false;
	int  rangeleft = getsetint(page, "hintrangemax");
	let  rangeend = NULL;

	//tab key
	bool focused = false;
	bool dofocus = ipkeys && ipkeys[strlen(ipkeys) - 1] == 9;
	if (dofocus)
		ipkeys[strlen(ipkeys) - 1] = '\0';

	char enterkey[2] = {0};
	*enterkey = (char)GDK_KEY_Return;
	bool enter = page->rangestart && !g_strcmp0(enterkey, ipkeys);
	if (type == Crange && (last || enter))
		for (GSList *next = elms; next; next = next->next)
	{
		Elm *elm = (Elm *)next->data;
		let te = elm->elm;
		i++;
		char *key = sfree(makekey(hintkeys, keylen, tnum, i, digit));
		rangein |= !g_strcmp0(key, page->rangestart);

		if (page->rangestart && !rangein)
			continue;

		if (enter)
		{
			rangeend = te;
			if (--rangeleft < 0) break;
			continue;
		}

		if (!strcmp(key, ipkeys))
		{
			ipkeys = NULL;
			iplen = 0;
			g_free(page->apkeys);
			page->apkeys = NULL;

			if (!page->rangestart)
				page->rangestart = g_strdup(key);
			else
				rangeend = te;

			break;
		}
	}

	GSList *rangeelms = NULL;
	i = -1;
	rangein = false;
	rangeleft = getsetint(page, "hintrangemax");
	for (GSList *next = elms; next; next = next->next)
	{
		Elm *elm = (Elm *)next->data;
		let te = elm->elm;
		i++;
		char *key = makekey(hintkeys, keylen, tnum, i, digit);
		rangein |= !g_strcmp0(key, page->rangestart);

		if (dofocus)
		{
			if (!focused &&
					(type != Crange || !page->rangestart || rangein) &&
					g_str_has_prefix(key, ipkeys ?: ""))
			{
				focuselm(te);
				focused = true;
			}
		}
		else if (last && type != Crange)
		{
			if (!ret && !strcmp(key, ipkeys))
			{
				ret = true;
				focuselm(te);
				if (type == Cclick)
				{
					bool isi = isinput(te);
#if V18
					if (page->script && !isi)
					{
#if JSC
						let rects = invoker(elm->elm, "getClientRects");
						let rect = idx(rects, 0);
#else
						WebKitDOMClientRectList *rects =
							webkit_dom_element_get_client_rects(elm->elm);
						WebKitDOMClientRect *rect =
							webkit_dom_client_rect_list_item(rects, 0);
#endif
						double x, y, w, h;
						recttovals(rect, &x, &y, &w, &h);
						jscunref(rect);
						g_object_unref(rects);

						char *arg = g_strdup_printf("%f:%f",
							x + elm->fx + w / 2.0 + 1.0,
							y + elm->fy + h / 2.0 + 1.0
						);
#else
					bool isa = !strcmp(stag(te), "A");

					if (page->script && !isi && !isa)
					{
						char *arg = g_strdup_printf("%ld:%ld",
								elm->x + elm->fx + elm->w / 2,
								elm->y + elm->fy + 1);
#endif
						send(page, "click", arg);
						g_free(arg);
					}
					else
					{
						if (!getsetbool(page,
									"javascript-can-open-windows-automatically")
								&& sfree(attr(te, "TARGET")))
							send(page, "showmsg", "The element has target, may have to type the enter key.");

#if JSC
						let ce = invoker(doc, "createEvent", aS("MouseEvent"));
						invoke(ce, "initEvent", aB(true), aB(true));
						invoke(te, "dispatchEvent", aJ(ce));
#else
						WebKitDOMEvent *ce =
							webkit_dom_document_create_event(doc, "MouseEvent", NULL);
						webkit_dom_event_init_event(ce, "click", true, true);
						webkit_dom_event_target_dispatch_event(
							(WebKitDOMEventTarget *)te, ce, NULL);
#endif
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
			rangeelms = g_slist_prepend(rangeelms, g_object_ref(te));
		}
		else if (!page->rangestart || (rangein && !rangeend))
		{
			bool has = g_str_has_prefix(key, ipkeys ?: "");
			ret |= has;
			if (has || rangein)
				g_string_append(hintstr, sfree(makehintelm(page,
						elm, has ? key : NULL, iplen, pagex, pagey)));
		}

		g_free(key);
		clearelm(elm);
		g_free(elm);

		if (rangein)
			rangein = --rangeleft > 0 && rangeend != te;
	}

	if (hintstr->len)
	{
#if JSC
		page->apchild = invoker(doc, "createElement", aS("DIV"));
		setprop_s(page->apchild, "innerHTML", hintstr->str);
		invoke(page->apnode, "appendChild", aJ(page->apchild));
#else
		page->apchild = (void *)webkit_dom_document_create_element(doc, "div", NULL);
		webkit_dom_element_set_inner_html((void *)page->apchild, hintstr->str, NULL);
		webkit_dom_node_append_child(page->apnode, page->apchild, NULL);
#endif
	}
	g_string_free(hintstr, true);

	for (GSList *next = rangeelms; next; next = next->next)
	{
		hintret(page, type, next->data, next->next);
		g_usleep(getsetint(page, "rangeloopusec"));
	}
	g_slist_free_full(rangeelms, g_object_unref);

	g_slist_free(elms);

	return ret;
}


//@context

static void domfocusincb(let w, let e, Page *page)
{
	let te = eachframes(page, activeelm);
	send(page, "_focusuri",
			sfree(tofull(te, te ? sfree(attr(te, "HREF")) : NULL)));
	jscunref(te);
}
static void domfocusoutcb(let w, let e, Page *page)
{ send(page, "_focusuri", NULL); }
//static void domactivatecb(WebKitDOMDOMWindow *w, WebKitDOMEvent *ev, Page *page)
//{ DD(domactivate!) }
static void rmtags(let doc, char *name)
{
	let cl = getelms(doc, name);

	GSList *rms = NULL;
	let te;
	for (int i = 0; (te = idx(cl, i)); i++)
		rms = g_slist_prepend(rms, te);

	for (GSList *next = rms; next; next = next->next)
	{
#if JSC
		let pn = prop(next->data, "parentNode");
		invoke(pn, "removeChiled", aJ(next->data));
		g_object_unref(pn);
		g_object_unref(next->data);
#else
		webkit_dom_node_remove_child(
			webkit_dom_node_get_parent_node(next->data), next->data, NULL);
#endif
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

static void addlistener(void *emt, char *name, void *func, void *data)
{
#if JSC
//this is disabled when javascript disabled
//also data is only sent once
//	let f = jsc_value_new_function(jsc_value_get_context(emt), NULL,
//			func, data, NULL,
//			G_TYPE_NONE, 1, JSC_TYPE_VALUE, JSC_TYPE_VALUE);
//	invoke(emt, "addEventListener", aS(name), aJ(f));
//	g_object_unref(f);

//	webkit_dom_event_target_add_event_listener(
//		(void *)webkit_dom_node_for_js_value(emt), name, func, false, data);
#else
#endif
	webkit_dom_event_target_add_event_listener(emt, name, func, false, data);
}

static void *frameon(let doc, Page *page)
{
	if (!doc) return NULL;
	void *emt =
		//have to be a view not a doc for beforeunload
#if JSC
		//Somehow JSC's defaultView(conveted to the DOM) is not a event target
		//Even it is, JSC's beforeunload may be killed
		webkit_dom_document_get_default_view(
			(void *)webkit_dom_node_for_js_value(g_object_ref(doc)));
#else
		defaultview(doc);
#endif

	page->emitters = g_slist_prepend(page->emitters, emt);

	if (getsetbool(page, "rmnoscripttag"))
	{
		rmtags(doc, "NOSCRIPT");
		//have to monitor DOMNodeInserted or?
		addlistener(emt, "DOMContentLoaded", domloadcb, doc);
	}

	addlistener(emt, "DOMFocusIn"  , domfocusincb , page);
	addlistener(emt, "DOMFocusOut" , domfocusoutcb, page);
	//addlistener(emt, "DOMActivate" , domactivatecb, page);

	//for hint
	addlistener(emt, "resize"      , hintcb  , page);
	addlistener(emt, "scroll"      , hintcb  , page);
	addlistener(emt, "beforeunload", unloadcb, page);
	//heavy
	//addlistener(emt, "DOMSubtreeModified", hintcb, page);

	return NULL;
}


static void pageon(Page *page, bool finished)
{
	g_slist_free_full(page->emitters, g_object_unref);
	page->emitters = NULL;

	eachframes(page, frameon);

	if (!finished
		|| !g_str_has_prefix(webkit_web_page_get_uri(page->kit), APP":main")
		|| !g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL)
	)
		return;

	let doc = sdoc(page);

	let cl = getelms(doc, "IMG");
	let te;
	for (int j = 0; (te = idx(cl, j)); j++)
	{
		if (!g_strcmp0(sfree(attr(te, "SRC")), APP":F"))
		{
#if JSC
			let pe = prop(te, "parentElement");
#else
			WebKitDOMElement *pe =
				webkit_dom_node_get_parent_element((WebKitDOMNode *)te);
#endif
			char *f = g_strdup_printf(
					APP":f/%s", sfree(
						g_uri_escape_string(
							sfree(attr(pe, "HREF")) ?: "", NULL, true)));
#if JSC
			invoke(te, "setAttribute", aS("SRC"), aS(f));
#else
			webkit_dom_element_set_attribute(te, "SRC", f, NULL);
#endif
			g_free(f);
			jscunref(pe);
		}
		jscunref(te);
	}

	g_object_unref(cl);
}


//@misc com funcs
static void mode(Page *page)
{
	let te = eachframes(page, activeelm);

	if (te && (isinput(te) || attrb(te, "contenteditable")))
		send(page, "toinsert", NULL);
	else
		send(page, "tonormal", NULL);

	jscunref(te);
}

static void *focusselection(let doc)
{
	void *ret = NULL;
	let win = defaultview(doc);

#if JSC
	let selection = invoker(win, "getSelection");

	let an =
		   prop(selection, "anchorNode")
		?: prop(selection, "focusNode" )
		?: prop(selection, "baseNode"  )
		?: prop(selection, "extentNode");

	if (an) do
	{
		let pe = prop(an, "parentElement");
		if (pe && isins(clicktags, stag(pe)))
		{
			focuselm(pe);
			g_object_unref(pe);
			ret = pe;
			pe = NULL;
		}
		g_object_unref(an);
		an = pe;
	} while (an);

#else
	WebKitDOMDOMSelection *selection =
		webkit_dom_dom_window_get_selection(win);

	WebKitDOMNode *an = NULL;

	an = webkit_dom_dom_selection_get_anchor_node(selection)
	  ?: webkit_dom_dom_selection_get_focus_node(selection)
	  ?: webkit_dom_dom_selection_get_base_node(selection)
	  ?: webkit_dom_dom_selection_get_extent_node(selection);

	if (an) do
	{
		WebKitDOMElement *pe = webkit_dom_node_get_parent_element(an);
		if (pe && isins(clicktags , stag(pe)))
		{
			focuselm(pe);
			ret = pe;
			break;
		}

	} while ((an = webkit_dom_node_get_parent_node(an)));

#endif

	g_object_unref(selection);
	g_object_unref(win);
	return ret;
}


static void blur(let doc)
{
	let te = activeelm(doc);
	if (te)
	{
#if JSC
		invoke(te, "blur");
		g_object_unref(te);
#else
		webkit_dom_element_blur(te);
#endif
	}

	//clear selection

	let win = defaultview(doc);
#if JSC
	let selection = invoker(win, "getSelection");
	invoke(selection, "empty");
#else
	WebKitDOMDOMSelection *selection =
		webkit_dom_dom_window_get_selection(win);
	webkit_dom_dom_selection_empty(selection);
#endif
	g_object_unref(win);
	g_object_unref(selection);
}

static void halfscroll(Page *page, bool d)
{
	let win = defaultview(sdoc(page));

#if JSC
	double h = propd(win, "innerHeight");
	invoke(win, "scrollTo",
			aD(propd(win, "scrollX")),
			aD(propd(win, "scrollY") + (d ? h/2 : - h/2)));
#else
	double h = webkit_dom_dom_window_get_inner_height(win);
	double y = webkit_dom_dom_window_get_scroll_y(win);
	double x = webkit_dom_dom_window_get_scroll_x(win);
	webkit_dom_dom_window_scroll_to(win, x, y + (d ? h/2 : - h/2));
#endif

	g_object_unref(win);
}

//@ipccb
void ipccb(const char *line)
{
	char **args = g_strsplit(line, ":", 3);

	Page *page = NULL;
	long lid = atol(args[0]);
	for (int i = 0; i < pages->len; i++)
		if (((Page *)pages->pdata[i])->id == lid)
			page = pages->pdata[i];

	if (!page) return;

	Coms type = *args[1];
	char *arg = args[2];

	char *ipkeys = NULL;
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
		char key[2] = {0};
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
			g_free(page->rangestart);
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
		let doc = sdoc(page);
		let win = defaultview(doc);
		makelist(page, doc, win, Ctext, NULL, NULL);
		g_object_unref(win);
		break;
	}
	case Crm:
		rmhint(page);
		break;

	case Cmode:
		mode(page);
		break;

	case Cfocus:
		eachframes(page, focusselection);
		break;

	case Cblur:
		eachframes(page, blur);
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
	const char *reqstr = webkit_uri_request_get_uri(req);
	if (g_str_has_prefix(reqstr, APP":"))
	{
		if (!strcmp(reqstr, APP":F"))
			return true;
		return false;
	}
	const char *pagestr = webkit_web_page_get_uri(page->kit);
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
	const char *phost = soup_uri_get_host(puri);
	if (phost)
	{
		char **cuts = g_strsplit(
				getset(page, "reldomaincutheads") ?: "", ";", -1);
		for (char **cut = cuts; *cut; cut++)
			if (g_str_has_prefix(phost, *cut))
			{
				phost += strlen(*cut);
				break;
			}
		g_strfreev(cuts);

		SoupURI *ruri = soup_uri_new(reqstr);
		const char *rhost = soup_uri_get_host(ruri);

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

		char *rmhdrs = getset(page, "removeheaders");
		if (rmhdrs)
		{
			char **rms = g_strsplit(rmhdrs, ";", -1);
			for (char **rm = rms; *rm; rm++)
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
	page->id = webkit_web_page_get_id(kp);
#if JSC
	page->mf = webkit_web_page_get_main_frame(kp);
#endif
	page->seto = g_object_new(G_TYPE_OBJECT, NULL);
	g_ptr_array_add(pages, page);

	wbpath = path2conf("whiteblack.conf");
	setwblist(false);

	char *name = NULL;
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
	const char *str = g_variant_get_string((GVariant *)v, NULL);
	fullname = g_strdup(g_strrstr(str, ";") + 1);
	shared = fullname[0] == 's';
	fullname = fullname + 1;

	if (shared)
		ipcwatch("ext");

	pages = g_ptr_array_new();

	SIG(ex, "page-created", initpage, NULL);
}

