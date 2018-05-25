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

typedef struct _WP {
	WebKitWebPage *kit;
	guint64        id;

	GSList        *aplist;
	WebKitDOMNode *apnode;
	gchar         *apkeys;

	gchar          lasttype;
	gchar         *lasthintkeys;
	WebKitDOMElement *rangestart; //not ref
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
	g_slist_free(page->aplist);
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
	WebKitDOMElement *elm;
#if NEWV
	WebKitDOMClientRectList *rects;
//	glong gap; //workaround
#endif
	glong fx;
	glong fy;
	glong x;
	glong y;
	glong w;
	glong h;
	glong zi;
} Elm;

static void clearelm(Elm *elm)
{
#if NEWV
	if (elm->rects)
		g_object_unref(elm->rects);
	elm->rects = NULL;
#endif
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
static bool isinput(WebKitDOMElement *te)
{
	gchar *tag = webkit_dom_element_get_tag_name(te);
	bool ret = false;
	if (isins(inputtags, tag))
	{
		if (strcmp(tag, "INPUT"))
		{
			g_free(tag);
			return true;
		}

		gchar *type = webkit_dom_element_get_attribute(te, "type");
		if (!type || !isins(inottext, type))
			ret = true;
	}
	g_free(tag);

	return ret;
}
static gchar *tofull(WebKitDOMElement *te, gchar *uri)
{
	if (!te || !uri) return NULL;
	gchar *bases = webkit_dom_node_get_base_uri((WebKitDOMNode *)te);
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
		send(*pages->pdata, "reloadlast", NULL);
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
		send(page, "blocked", uri);
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
static WebKitDOMElement *tldoc;
static WebKitDOMHTMLTextAreaElement *tlelm;
static WebKitDOMHTMLInputElement *tlielm;
static void textlinkset(Page *page, gchar *path)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
	if (tldoc != webkit_dom_document_get_document_element(doc)) return;

	GIOChannel *io = g_io_channel_new_file(path, "r", NULL);
	gchar *text;
	g_io_channel_read_to_end(io, &text, NULL, NULL);
	g_io_channel_unref(io);

	if (tlelm)
		webkit_dom_html_text_area_element_set_value(tlelm, text);
	else
		webkit_dom_html_input_element_set_value(tlielm, text);
	g_free(text);
}
static void textlinkget(Page *page, gchar *path)
{
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMElement   *te = webkit_dom_document_get_active_element(doc);
	gchar *tag = webkit_dom_element_get_tag_name(te);
	bool ista = !strcmp(tag, "TEXTAREA");
	g_free(tag);

	tlelm = NULL;
	tlielm = NULL;

	if (ista)
		tlelm = (WebKitDOMHTMLTextAreaElement *)te;
	else if (isinput(te))
		tlielm = (WebKitDOMHTMLInputElement *)te;
	else
	{
		send(page, "showmsg", "Not a text");
		return;
	}

	tldoc = webkit_dom_document_get_document_element(doc);

	gchar *text = tlelm ?
		webkit_dom_html_text_area_element_get_value(tlelm) :
		webkit_dom_html_input_element_get_value(tlielm);

	GIOChannel *io = g_io_channel_new_file(path, "w", NULL);
	g_io_channel_write_chars(io, text ?: "", -1, NULL, NULL);
	g_io_channel_unref(io);
	g_free(text);

	send(page, "textlinkon", NULL);
}


//@hinting
static bool styleis(WebKitDOMCSSStyleDeclaration *dec, gchar* name, gchar *pval)
{
	gchar *val = webkit_dom_css_style_declaration_get_property_value(dec, name);
	bool ret = (val && !strcmp(pval, val));
	g_free(val);

	return ret;
}

static Elm getrect(WebKitDOMElement *te)
{
	Elm elm = {0};

#if ! NEWV
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
#else
	elm.rects =
		webkit_dom_element_get_client_rects(te);

	WebKitDOMClientRect *rect =
		webkit_dom_element_get_bounding_client_rect(te);

	//workaround zoom and scroll causes gap
//	elm.gap = elm.y - webkit_dom_client_rect_get_top(rect);
	elm.y = webkit_dom_client_rect_get_top(rect);
	elm.x = webkit_dom_client_rect_get_left(rect);
	elm.h = webkit_dom_client_rect_get_height(rect);
	elm.w = webkit_dom_client_rect_get_width(rect);

	g_object_unref(rect);
#endif

	return elm;
}

static void _trim(glong *tx, glong *tw, glong *px, glong *pw)
{
	glong right = *tx + *tw;
	glong pr    = *px + *pw;
	if (pr < right)
		*tw -= right - pr;

	if (*px > *tx)
	{
		*tw -= *px - *tx;
		*tx = *px;
	}
}

static WebKitDOMElement *_makehintelm(
		Page *page,
		WebKitDOMDocument *doc,
		bool center ,glong y, glong x, glong h, glong w,
		gchar *uri, const gchar* text, gint len, bool head)
{
	WebKitDOMElement *ret = webkit_dom_document_create_element(doc, "div", NULL);
	WebKitDOMElement *area = webkit_dom_document_create_element(doc, "div", NULL);

	//ret
	webkit_dom_element_set_attribute(ret, "TITLE", uri ?: "-", NULL);
	static const gchar *retstyle =
		"position: absolute;" //somehow if fixed web page crashes
		"overflow: visible;"
		"display: block;"
		"visibility: visible;"
		"padding: 0;"
		"margin: 0;"
		"opacity: 1;"
		"top: %dpx;"
		"left: %dpx;"
		"height: %dpx;"
		"width: %dpx;"
		"text-align: center;"
		;
	gchar *stylestr = g_strdup_printf(
			retstyle, y, x, h, w);

	WebKitDOMCSSStyleDeclaration *styledec = webkit_dom_element_get_style(ret);
	webkit_dom_css_style_declaration_set_css_text(styledec, stylestr, NULL);
	g_object_unref(styledec);
	g_free(stylestr);

	//area
	static const gchar *areastyle =
		"position: absolute;"
		"z-index: 2147483647;"
		"background-color: #a6f;"
//		"background: linear-gradient(to right, fuchsia, white, white, fuchsia);"
//		"border: 1px solid white;" //causes changing of size
		"opacity: .1;"
//		"margin: -1px;"
		"border-radius: .4em;"
//		"cursor: pointer;"
		"height: 100%;"
		"width: 100%;"
		;

	styledec = webkit_dom_element_get_style(area);
	webkit_dom_css_style_declaration_set_css_text(styledec, areastyle, NULL);
	g_object_unref(styledec);

	webkit_dom_node_append_child((WebKitDOMNode *)ret, (WebKitDOMNode *)area, NULL);
	g_object_unref(area);

	//hint
	if (!text) return ret;

	WebKitDOMElement *hint = webkit_dom_document_create_element(doc, "span", NULL);

	gchar *ht = g_strdup_printf("%s", text + len);
	webkit_dom_element_set_inner_html(hint, ht, NULL);
	gchar *pad = strlen(ht) == 1 ? "2" : "1";
	g_free(ht);

	static const gchar *hintstyle =
//		"-webkit-transform: rotate(-9deg);"
		"position: relative;"
		"z-index: 2147483647;"
		"font-size: small !important;"
		"font-family: \"DejaVu Sans Mono\", monospace !important;"
		"font-weight: bold;"
		"background: linear-gradient(%s);"
		"color: white;"
//		"border: 1px solid indigo;"
		"border-radius: .3em;"
		"opacity: 0.%s;"
		"display:inline-block;"
		"padding: .1em %spx 0;"
		"line-height: 1em;"
		"top: %s%dem;"
		"%s;" //user setting
		"%s"
		;

	const gchar *opacity = head ? "9" : "4";
	const gchar *bg      = head ? "#649, #203" : "#203, #203";
	const gint offset = 6;

	stylestr = center ?
		g_strdup_printf(hintstyle,
				bg, opacity, pad, ".", offset, getset(page, "hintstyle"),
				"background: linear-gradient(darkorange, red);")
		:
		g_strdup_printf(hintstyle,
				bg, opacity, pad, "-.", y > offset ? offset : y,
				getset(page, "hintstyle"), "");

	styledec = webkit_dom_element_get_style(hint);
	webkit_dom_css_style_declaration_set_css_text(styledec, stylestr, NULL);
	g_object_unref(styledec);
	g_free(stylestr);

	webkit_dom_node_append_child((WebKitDOMNode *)ret, (WebKitDOMNode *)hint, NULL);
	g_object_unref(hint);

	return ret;
}
static WebKitDOMElement *makehintelm(Page *page,
		WebKitDOMDocument *doc, Elm *elm, const gchar* text, gint len,
		glong pagex, glong pagey)
{
	gchar *tag = webkit_dom_element_get_tag_name(elm->elm);
	bool center = isins(uritags, tag) && !isins(linktags, tag);
	g_free(tag);

	gchar *uri =
		webkit_dom_element_get_attribute(elm->elm, "ONCLICK") ?:
		webkit_dom_element_get_attribute(elm->elm, "HREF") ?:
		webkit_dom_element_get_attribute(elm->elm, "SRC");

#if NEWV
	WebKitDOMElement *ret = webkit_dom_document_create_element(doc, "div", NULL);
	static const gchar *retstyle =
		"position: absolute;"
		"overflow: visible;"
		"top: 0px;"
		"left: 0px;"
		"height: 10px;"
		"width: 10px;"
		;
	WebKitDOMCSSStyleDeclaration *styledec = webkit_dom_element_get_style(ret);
	webkit_dom_css_style_declaration_set_css_text(styledec, retstyle, NULL);
	g_object_unref(styledec);

	gulong l = webkit_dom_client_rect_list_get_length(elm->rects);
	for (gulong i = 0; i < l; i++)
	{
		WebKitDOMClientRect *rect =
			webkit_dom_client_rect_list_item(elm->rects, i);

		glong
			y = webkit_dom_client_rect_get_top(rect),
			x = webkit_dom_client_rect_get_left(rect),
			h = webkit_dom_client_rect_get_height(rect),
			w = webkit_dom_client_rect_get_width(rect);

		_trim(&x, &w, &elm->x, &elm->w);
		_trim(&y, &h, &elm->y, &elm->h);

		WebKitDOMElement *hint = _makehintelm(page, doc, center,
				y + elm->fy + pagey,
//				y + elm->fy + elm->gap,
				//gap is workaround. so x is left.
				x + elm->fx + pagex,
				h,
				w,
				uri, text, len, i == 0);

		webkit_dom_node_append_child(
				(WebKitDOMNode *)ret, (WebKitDOMNode *)hint, NULL);
		g_object_unref(hint);
	}

	return ret;
#else

	return _makehintelm(page, doc, center,
			elm->y + elm->fy + pagey,
			elm->x + elm->fx + pagex, elm->h, elm->w, uri, text, len, true);
#endif
	g_free(uri);
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

	WebKitDOMDocument *doc  = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMElement  *elm  = webkit_dom_document_get_document_element(doc);
	WebKitDOMNode     *node = (WebKitDOMNode *)elm;

	for (GSList *next = page->aplist; next; next = next->next)
	{
		if (page->apnode == node)
		{
			webkit_dom_node_remove_child(page->apnode, next->data, NULL);
			g_object_unref(next->data);
		}
	}

	g_slist_free(page->aplist);
	g_free(page->apkeys);
	page->aplist = NULL;
	page->apnode = NULL;
	page->apkeys = NULL;
}

static void trim(Elm *te, Elm *prect)
{
	_trim(&te->x, &te->w, &prect->x, &prect->w);
	_trim(&te->y, &te->h, &prect->y, &prect->h);
}
static Elm checkelm(WebKitDOMDOMWindow *win, Elm *frect, Elm *prect,
		WebKitDOMElement *te, bool js, bool notttag)
{
	Elm ret = {0};

	//elms visibility hidden have size also opacity
	WebKitDOMCSSStyleDeclaration *dec =
		webkit_dom_dom_window_get_computed_style(win, te, NULL);

	static gchar *check[][2] = {
		{"visibility", "hidden"},
		{"opacity"   , "0"},
		{"display"   , "none"},
	};
	for (int k = 0; k < sizeof(check) / sizeof(*check); k++)
		if (styleis(dec, check[k][0], check[k][1]))
			goto retfalse;

	ret = getrect(te);

	glong bottom = ret.y + ret.h;
	glong right  = ret.x + ret.w;
	if (
		(ret.y < 0         && bottom < 0       ) ||
		(ret.y > frect->h  && bottom > frect->h) ||
		(ret.x < 0         && right  < 0       ) ||
		(ret.x > frect->w  && right  > frect->w)
	)
		goto retfalse;

	ret.insight = true;

#if ! NEWV
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
				glong nr = MIN(right, rectp.x + rectp.w);
				ret.w += nr - right;
				right = nr;
				break;
			}
			g_object_unref(decp);
		}
	}
#endif

	gchar *zc = webkit_dom_css_style_declaration_get_property_value(dec, "z-index");
	ret.zi = atoi(zc);
	g_free(zc);

	if (prect)
		trim(&ret, ret.zi > prect->zi ? frect : prect);

	if (js && (ret.h < 1 || ret.w < 1))
		goto retfalse;

	if (js && notttag && !styleis(dec, "cursor", "pointer"))
		goto retfalse;

	g_object_unref(dec);

	ret.elm = te;
	ret.fx = frect->x;
	ret.fy = frect->y;
	ret.ok = true;

	return ret;

retfalse:
	clearelm(&ret);
	g_object_unref(dec);
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

static bool eachclick(WebKitDOMDOMWindow *win, WebKitDOMHTMLCollection *cl,
		Coms type, GSList **elms, Elm *frect, Elm *prect)
{
	bool ret = false;

	for (gint i = 0; i < webkit_dom_html_collection_get_length(cl); i++)
	{
		WebKitDOMElement *te =
			(WebKitDOMElement *)webkit_dom_html_collection_item(cl, i);

		gchar *tag = webkit_dom_element_get_tag_name(te);
		if (isins(clicktags, tag))
		{
			Elm elm = checkelm(win, frect, prect, te, true, false);
			if (elm.ok)
				addelm(&elm, elms);

			g_free(tag);
			continue;
		}
		g_free(tag);

		Elm elm = checkelm(win, frect, prect, te, true, true);
		if (!elm.insight) continue;

		WebKitDOMHTMLCollection *ccl = webkit_dom_element_get_children(te);
		Elm *crect = prect;
		WebKitDOMCSSStyleDeclaration *dec =
			webkit_dom_dom_window_get_computed_style(win, te, NULL);
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
static GSList *_makelist(Page *page, WebKitDOMDocument *doc,
		Coms type, GSList *elms, Elm *frect, Elm *prect)
{
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);

	const gchar **taglist = clicktags; //Cclick
	if (type == Clink ) taglist = linktags;
	if (type == Curi  ) taglist = uritags;
	if (type == Cspawn) taglist = uritags;
	if (type == Crange) taglist = uritags;
	if (type == Ctext ) taglist = texttags;

	if (type == Cclick && page->script)
	{
		WebKitDOMHTMLCollection *cl = webkit_dom_element_get_children(
				(WebKitDOMElement *)webkit_dom_document_get_body(doc));
		eachclick(win, cl, type, &elms, frect, prect);
		g_object_unref(cl);
	}
	else for (const gchar **tag = taglist; *tag; tag++)
	{
		WebKitDOMHTMLCollection *cl =
			webkit_dom_document_get_elements_by_tag_name_as_html_collection(doc, *tag);

		for (gint j = 0; j < webkit_dom_html_collection_get_length(cl); j++)
		{
			WebKitDOMNode *tn = webkit_dom_html_collection_item(cl, j);
			WebKitDOMElement *te = (void *)tn;

			Elm elm = checkelm(win, frect, prect, te, false, false);
			if (elm.ok)
			{
				if (type == Ctext)
				{
					clearelm(&elm);
					if (!isinput(te)) continue;

					webkit_dom_element_focus(te);
					g_object_unref(win);
					return NULL;
				}

				addelm(&elm, &elms);
			}

		}
		g_object_unref(cl);
	}

	g_object_unref(win);
	return elms;
}

static GSList *makelist(Page *page, WebKitDOMDocument *doc, Coms type,
		Elm *frect, GSList *elms)
{
	Elm frectr = {0};
	if (!frect)
	{
		WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);
		frectr.w = webkit_dom_dom_window_get_inner_width(win);
		frectr.h = webkit_dom_dom_window_get_inner_height(win);
		frect = &frectr;
		g_object_unref(win);
	}
	Elm prect = *frect;
	prect.x = prect.y = 0;

	//D(rect %d %d %d %d, rect.y, rect.x, rect.h, rect.w)
	elms = _makelist(page, doc, type, elms, frect, &prect);

	WebKitDOMHTMLCollection *cl =
		webkit_dom_document_get_elements_by_tag_name_as_html_collection(doc, "IFRAME");

	for (gint j = 0; j < webkit_dom_html_collection_get_length(cl); j++)
	{
		void *tn = webkit_dom_html_collection_item(cl, j);
		WebKitDOMHTMLIFrameElement *tfe = tn;
		WebKitDOMElement *te = tn;

		WebKitDOMDocument *fdoc =
			webkit_dom_html_iframe_element_get_content_document(tfe);

		WebKitDOMDOMWindow *fwin = webkit_dom_document_get_default_view(fdoc);
		Elm cfrect = checkelm(fwin, frect, &prect, te, false, false);
		g_object_unref(fwin);

		if (cfrect.ok)
		{
			gdouble cx = webkit_dom_element_get_client_left(te);
			gdouble cy = webkit_dom_element_get_client_top(te);
			gdouble cw = webkit_dom_element_get_client_width(te);
			gdouble ch = webkit_dom_element_get_client_height(te);

			cfrect.w = MIN(cfrect.w - cx, cw);
			cfrect.h = MIN(cfrect.h - cy, ch);

			cfrect.x += cfrect.fx + cx;
			cfrect.y += cfrect.fy + cy;
			elms = makelist(page, fdoc, type, &cfrect, elms);
		}

		clearelm(&cfrect);
	}

	g_object_unref(cl);

	return elms;
}

static void hintret(Page *page, Coms type, WebKitDOMElement *te)
{
	gchar uritype = 'l';
	gchar *uri = NULL;
	gchar *label = NULL;
	if (type == Curi || type == Cspawn || type == Crange)
	{
		uri = webkit_dom_element_get_attribute(te, "SRC");

		if (!uri)
		{
			WebKitDOMHTMLCollection *cl = webkit_dom_element_get_children(te);

			for (gint i = 0; i < webkit_dom_html_collection_get_length(cl); i++)
			{
				WebKitDOMElement *le = (void *)webkit_dom_html_collection_item(cl, i);
				gchar *tag = webkit_dom_element_get_tag_name(le);
				if (g_strcmp0(tag, "SOURCE") == 0 &&
						(uri = webkit_dom_element_get_attribute(le, "SRC")))
				{
					g_free(tag);
					break;
				}
				g_free(tag);
			}

			g_object_unref(cl);
		}

		if (uri && (type == Cspawn || type == Crange))
		{
			gchar *tag = webkit_dom_element_get_tag_name(te);
			if (!strcmp(tag, "IMG"))
				uritype = 'i';
			else
				uritype = 'm';

			g_free(tag);
		}
	}

	if (!uri)
		uri = webkit_dom_element_get_attribute(te, "HREF");

	if (!uri)
		uri = g_strdup("about:blank");

	label =
		webkit_dom_html_element_get_inner_text((WebKitDOMHTMLElement *)te) ?:
		webkit_dom_element_get_attribute(te, "ALT") ?:
		webkit_dom_element_get_attribute(te, "TITLE");

	WebKitDOMDocument *odoc = webkit_dom_node_get_owner_document((void *)te);
	gchar *ouri = webkit_dom_document_get_document_uri(odoc);

	gchar *suri = tofull(te, uri);
	gchar *retstr = g_strdup_printf("%c%s %s %s", uritype, ouri, suri, label);
	send(page, "hintret", retstr);

	g_free(uri);
	g_free(label);
	g_free(ouri);
	g_free(suri);
	g_free(retstr);
}

static bool makehint(Page *page, Coms type, gchar *hintkeys, gchar *ipkeys)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
	page->lasttype = type;

	if (type != Cclick)
	{
		WebKitDOMDocumentType *dtype = webkit_dom_document_get_doctype(doc);
		if (dtype && strcmp("html", webkit_dom_document_type_get_name(dtype)))
		{
			//no elms may be;P
			gchar *retstr = g_strdup_printf("l%s ", webkit_web_page_get_uri(page->kit));
			send(page, "hintret", retstr);
			g_free(retstr);

			g_free(ipkeys);
			return false;
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

	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);
	glong pagex = webkit_dom_dom_window_get_scroll_x(win);
	glong pagey = webkit_dom_dom_window_get_scroll_y(win);
	g_object_unref(win);

	GSList *elms = makelist(page, doc, type, NULL, NULL);
	guint tnum = g_slist_length(elms);

	page->apnode = (WebKitDOMNode *)webkit_dom_document_get_document_element(doc);
	gint keylen = strlen(hintkeys);
	gint iplen = ipkeys ? strlen(ipkeys) : 0;
	gint digit = getdigit(keylen, tnum);
	bool last = iplen == digit;
	elms = g_slist_reverse(elms);
	gint i = -1;
	bool ret = false;

	bool rangein = false;
	gint rangeleft = getsetint(page, "hintrangemax");
	WebKitDOMElement *rangeend = NULL;

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
		WebKitDOMElement *te = elm->elm;

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
		WebKitDOMElement *te = elm->elm;

		rangein |= te == page->rangestart;

		i++;
		gchar *key = makekey(hintkeys, keylen, tnum, i, digit);

		if (dofocus)
		{
			if (!focused &&
					(type != Crange || !page->rangestart || rangein) &&
					g_str_has_prefix(key, ipkeys ?: ""))
			{
				webkit_dom_element_focus(te);
				focused = true;
			}
		}
		else if (last && type != Crange)
		{
			if (!ret && !strcmp(key, ipkeys))
			{
				ret = true;

				webkit_dom_element_focus(te);
				if (type == Cclick)
				{
					bool isi = isinput(te);
#if NEWV
					if (page->script && !isi)
					{
						WebKitDOMClientRect *rect =
							webkit_dom_client_rect_list_item(elm->rects, 0);

						gdouble x = webkit_dom_client_rect_get_left(rect);
						gdouble y = webkit_dom_client_rect_get_top(rect);
						gdouble w = webkit_dom_client_rect_get_width(rect);
						gdouble h = webkit_dom_client_rect_get_height(rect);

						gchar *arg = g_strdup_printf("%f:%f",
							x + elm->fx + w / 2.0 + 1.0,
							y + elm->fy + h / 2.0 + 1.0
						);
#else
					gchar *tag = webkit_dom_element_get_tag_name(te);
					bool isa = !strcmp(tag, "A");
					g_free(tag);

					if (page->script && !isi && !isa)
					{
						gchar *arg = g_strdup_printf("%ld:%ld",
								elm->x + elm->fx + elm->w / 2,
								elm->y + elm->fy + 1);
#endif
						send(page, "click", arg);
						g_free(arg);
					}
					else
					{
						if (webkit_dom_element_has_attribute(te, "TARGET") &&
								!getsetbool(page, "javascript-can-open-windows-automatically"))
							send(page, "showmsg", "The element has target, may have to type the enter key.");

						WebKitDOMEvent *ce =
							webkit_dom_document_create_event(doc, "MouseEvent", NULL);
						webkit_dom_event_init_event(ce, "click", true, true);
						webkit_dom_event_target_dispatch_event(
							(WebKitDOMEventTarget *)te, ce, NULL);
						g_object_unref(ce);
					}

					if (isi)
						send(page, "toinsert", NULL);
					else
						send(page, "tonormal", NULL);

				}
				else
				{
					hintret(page, type, te);
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
				WebKitDOMElement *ne = makehintelm(page,
						doc, elm, has ? key : NULL, iplen, pagex, pagey);
				webkit_dom_node_append_child(page->apnode, (WebKitDOMNode *)ne, NULL);
				page->aplist = g_slist_prepend(page->aplist, ne);
				ret |= has;
			}
		}

		g_free(key);
		clearelm(elm);
		g_free(elm);

		if (rangein)
			rangein = --rangeleft > 0 && rangeend != te;
	}

	for (GSList *next = rangeelms; next; next = next->next)
	{
		hintret(page, type, next->data);
		g_usleep(getsetint(page, "rangeloopusec"));
	}
	g_slist_free(rangeelms);

	g_slist_free(elms);

	return ret;
}


//@context
static void domfocusincb(WebKitDOMDOMWindow *w, WebKitDOMEvent *e, Page *page)
{
	WebKitDOMDocument *doc = webkit_dom_dom_window_get_document(w);
	WebKitDOMElement *te = webkit_dom_document_get_active_element(doc);
	gchar *href = te ? webkit_dom_element_get_attribute(te, "HREF") : NULL;
	gchar *uri = tofull(te, href);
	send(page, "focusuri", uri);

	g_free(uri);
	g_free(href);
}
static void domfocusoutcb(WebKitDOMDOMWindow *w, WebKitDOMEvent *ev, Page *page)
{ send(page, "focusuri", NULL); }
//static void domactivatecb(WebKitDOMDOMWindow *w, WebKitDOMEvent *ev, Page *page)
//{ DD(domactivate!) }
//static void domloadcb(WebKitDOMDOMWindow *w, WebKitDOMEvent *ev, Page *page) {}
static void hintcb(WebKitDOMDOMWindow *w, WebKitDOMEvent *ev, Page *page)
{
	if (page->apnode)
		makehint(page, page->lasttype, NULL, NULL);
}
static void pagestart(Page *page)
{
	g_slist_free_full(page->black, g_free);
	g_slist_free_full(page->white, g_free);
	page->black = NULL;
	page->white = NULL;
}
static void frameon(Page *page, WebKitDOMDocument *doc)
{
	WebKitDOMEventTarget *emitter = WEBKIT_DOM_EVENT_TARGET(
			webkit_dom_document_get_default_view(doc));

	page->emitters = g_slist_prepend(page->emitters, emitter);

	webkit_dom_event_target_add_event_listener(emitter,
			"DOMFocusIn", G_CALLBACK(domfocusincb), false, page);
	webkit_dom_event_target_add_event_listener(emitter,
			"DOMFocusOut", G_CALLBACK(domfocusoutcb), false, page);
//	webkit_dom_event_target_add_event_listener(emitter,
//			"DOMActivate", G_CALLBACK(domactivatecb), false, page);
//	webkit_dom_event_target_add_event_listener(emitter,
//			"DOMContentLoaded", G_CALLBACK(domloadcb), false, page);

	//for refresh hint
	webkit_dom_event_target_add_event_listener(emitter,
			"resize", G_CALLBACK(hintcb), false, page);
	webkit_dom_event_target_add_event_listener(emitter,
			"scroll", G_CALLBACK(hintcb), false, page);
//may be heavy
//	webkit_dom_event_target_add_event_listener(emitter,
//			"DOMSubtreeModified", G_CALLBACK(hintcb), false, page);

	WebKitDOMHTMLCollection *cl =
		webkit_dom_document_get_elements_by_tag_name_as_html_collection(
				doc, "IFRAME");
	for (gint j = 0; j < webkit_dom_html_collection_get_length(cl); j++)
		frameon(page,
				webkit_dom_html_iframe_element_get_content_document(
					(void *)webkit_dom_html_collection_item(cl, j)));

	g_object_unref(cl);
}
static void pageon(Page *page)
{
	g_slist_free_full(page->emitters, g_object_unref);
	page->emitters = NULL;
	frameon(page, webkit_web_page_get_dom_document(page->kit));
}


//@misc com funcs
static void mode(Page *page)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMElement  *te = webkit_dom_document_get_active_element(doc);

	if (te)
	{
		gchar *type = webkit_dom_element_get_attribute(te, "contenteditable");
		bool iseditable = type && !strcmp(type, "true");
		g_free(type);

		if (iseditable || isinput(te))
			return send(page, "toinsert", NULL);
	}

	send(page, "tonormal", NULL);
}

static void focus(Page *page)
{
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);

	WebKitDOMDOMSelection *selection =
		webkit_dom_dom_window_get_selection(win);

	WebKitDOMNode *an = NULL;

	an = webkit_dom_dom_selection_get_anchor_node(selection)
	  ?: webkit_dom_dom_selection_get_focus_node(selection)
	  ?: webkit_dom_dom_selection_get_base_node(selection)
	  ?: webkit_dom_dom_selection_get_extent_node(selection);

	if (an) do
	{
		WebKitDOMElement *elm = webkit_dom_node_get_parent_element(an);
		if (!elm) continue;
		gchar *tag = webkit_dom_element_get_tag_name(elm);

		if (isins(clicktags , tag))
		{
			webkit_dom_element_focus(elm);
			g_free(tag);
			break;
		}
		g_free(tag);

	} while ((an = webkit_dom_node_get_parent_node(an)));

	g_object_unref(selection);
	g_object_unref(win);
}

static void blur(Page *page)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMElement  *elm = webkit_dom_document_get_active_element(doc);
	if (elm)
		webkit_dom_element_blur(elm);

	//clear selection
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);
	WebKitDOMDOMSelection *selection =
		webkit_dom_dom_window_get_selection(win);
	webkit_dom_dom_selection_empty(selection);

	g_object_unref(selection);
	g_object_unref(win);
}

static void halfscroll(Page *page, bool d)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);
	glong y = webkit_dom_dom_window_get_scroll_y(win);
	glong x = webkit_dom_dom_window_get_scroll_x(win);
	glong h = webkit_dom_dom_window_get_inner_height(win);
	y += (d ? h/2 : - h/2);
	webkit_dom_dom_window_scroll_to(win, x, y);
	g_object_unref(win);
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

		pageon(page);
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
		if (!makehint(page, type, confcstr("hintkeys"), ipkeys))
		{
			send(page, "showmsg", "No hint");
			send(page, "tonormal", NULL);
		}
		break;
	case Ctext:
	{
		WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
		makelist(page, doc, Ctext, NULL, NULL);
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
	const gchar *pagestr = webkit_web_page_get_uri(page->kit);

	SoupMessageHeaders *head = webkit_uri_request_get_http_headers(req);
	if (!head && g_str_has_prefix(reqstr, APP":"))
		return false;

	bool ret = false;
	int check = checkwb(reqstr);
	if (check == 0)
		ret = true;
	else if (check == -1 && getsetbool(page, "adblock"))
	{
		bool (*checkf)(const char *, const char *) =
			g_object_get_data(G_OBJECT(page->kit), "wyebcheck");
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
	send(page, "setreq", NULL);

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

