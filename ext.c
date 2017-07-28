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

#include <math.h>
#include <ctype.h>
#include <webkit2/webkit-web-extension.h>

static gchar *fullname = "";
#include "general.c"

#if WEBKIT_MAJOR_VERSION > 2 || WEBKIT_MINOR_VERSION > 16
# define NEWV 1
#else
# define NEWV 0
#endif

typedef struct {
	bool           removed;
	WebKitWebPage *kit;
	guint64        id;

	GSList        *aplist;
	WebKitDOMNode *apnode;
	gchar         *apkeys;

	Coms           lasttype;
	gchar         *lasthintkeys;
	bool           relonly;
	bool           showblocked;
	bool           script;
	gchar         *cutheads;
	GSList        *black;
	GSList        *white;
	WebKitDOMDOMWindow *emitter;
} Page;

static GPtrArray *pages = NULL;

static void freepage(Page *page)
{
	page->removed = true;

	g_slist_free(page->aplist);
	g_free(page->apkeys);
	g_free(page->lasthintkeys);
	g_free(page->cutheads);
	g_slist_free_full(page->black, g_free);
	g_slist_free_full(page->white, g_free);

	if (page->emitter) g_object_unref(page->emitter);

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
static const gchar *itext[] = { //no name is too
	"search", "text", "url", "email",  "password", "tel", NULL
};
static const gchar *ilimitedtext[] = {
	"month",  "number", "time", "week", "date", "datetime-local", NULL
};
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
	gchar *ss = g_strdup_printf("%d:%s:%s", page->id, action, arg);
	//D(send to main %s, ss)
	ipcsend("main", ss);
	g_free(ss);
}
static bool isin(const gchar **ary, gchar *val)
{
	if (!val) return false;
	for (;*ary; ary++)
		if (strcmp(val, *ary) == 0) return true;
	return false;
}
static bool isinput(WebKitDOMElement *te)
{
	gchar *tag = webkit_dom_element_get_tag_name(te);
	bool ret = false;
	if (isin(inputtags, tag))
	{
		if (strcmp(tag, "INPUT") != 0) return true;

		gchar *type = webkit_dom_element_get_attribute(te, "type");
		if (!type || !isin(inottext, type))
			ret = true;
	}
	g_free(tag);

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
static GSList   *wblist = NULL;
static gchar    *wbpath = NULL;
static __time_t  wbtime = 0;
static void setwblist(bool monitor); //declaration
static void preparewb()
{
	prepareif(&wbpath, NULL, "whiteblack.conf",
			"# First char is 'w':white list or 'b':black list.\n"
			"# Second and following chars are regular expressions.\n"
			"# Preferential order: bottom > top\n"
			"# Keys 'a' and 'A' on wyeb add blocked or loaded list to this file.\n"
			"\n"
			"w^https?://([a-z0-9]+\\.)*githubusercontent\\.com/\n"
			"\n"
			, setwblist);

	if (wbpath)
		wbpath = path2conf("whiteblack.conf");
}
void setwblist(bool monitor)
{
	if (monitor && !g_file_test(wbpath, G_FILE_TEST_EXISTS))
	{
		g_slist_free_full(wblist, (GDestroyNotify)clearwb);
		return;
	}

	preparewb();

	if (!getctime(wbpath, &wbtime)) return;
	if (wblist)
		g_slist_free_full(wblist, (GDestroyNotify)clearwb);
	wblist = NULL;

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

	if (monitor)
		send(*pages->pdata, "reloadlast", NULL);
}
static gint checkwb(const gchar *uri) // -1 no result, 0 black, 1 white;
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
	if (page->showblocked)
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

	gchar   pre  = white ? 'w' : 'b';
	guint   len  = g_slist_length(list);

	fprintf(f, "\n# Following list are %s in %s\n",
			white ? "blocked" : "loaded",
			webkit_web_page_get_uri(page->kit));

	list = g_slist_reverse(g_slist_copy(list));
	for (GSList *next = list; next; next = next->next)
	{
		gchar *esc = escape(next->data);
		gchar *line = g_strdup_printf("%c^%s\n", pre, esc);
		g_free(esc);

		fputs(line, f);

		g_free(line);
	}
	g_slist_free(list);

	fclose(f);

	setwblist(false);
	send(page, "openeditor", wbpath);
}



//@textlink
static gchar *tlpath = NULL;
static Page  *tlpage = NULL;
static WebKitDOMElement *tldoc;
static WebKitDOMHTMLTextAreaElement *tlelm;
static void textlinkcheck(bool monitor)
{
	if (!tlpage || tlpage->removed) return;
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(tlpage->kit);
	if (tldoc != webkit_dom_document_get_document_element(doc)) return;

	GIOChannel *io = g_io_channel_new_file(tlpath, "r", NULL);
	gchar *text;
	g_io_channel_read_to_end(io, &text, NULL, NULL);
	g_io_channel_unref(io);

	webkit_dom_html_text_area_element_set_value(tlelm, text);
	g_free(text);
}
static void textlinkon(Page *page)
{
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMElement   *te = webkit_dom_document_get_active_element(doc);
	gchar *tag = webkit_dom_element_get_tag_name(te);
	bool ist = strcmp(tag, "TEXTAREA") == 0;
	g_free(tag);
	if (!ist)
	{
		send(page, "showmsg", "Not a textare");
		return;
	}

	if (!tlpath)
	{
		tlpath = g_build_filename(
			g_get_user_data_dir(), fullname, "textlink.txt", NULL);
		monitor(tlpath, textlinkcheck);
	}
	tlpage = page;
	tldoc = webkit_dom_document_get_document_element(doc);
	tlelm = (WebKitDOMHTMLTextAreaElement *)te;

	gchar *text = webkit_dom_html_text_area_element_get_value(tlelm);
	GIOChannel *io = g_io_channel_new_file(tlpath, "w", NULL);
	g_io_channel_write_chars(io, text, -1, NULL, NULL);
	g_io_channel_unref(io);
	g_free(text);

	send(page, "openeditor", tlpath);
}


//@hinting
static bool styleis(WebKitDOMCSSStyleDeclaration *dec, gchar* name, gchar *pval)
{
	gchar *val = webkit_dom_css_style_declaration_get_property_value(dec, name);
	bool ret = (val && strcmp(pval, val) == 0);
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
		WebKitDOMDocument *doc,
		bool center ,glong y, glong x, glong h, glong w,
		const gchar* text, gint len, bool head)
{
	WebKitDOMElement *ret = webkit_dom_document_create_element(doc, "div", NULL);
	WebKitDOMElement *area = webkit_dom_document_create_element(doc, "div", NULL);
	WebKitDOMElement *hint = webkit_dom_document_create_element(doc, "span", NULL);

	//ret
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
//		"background-color: purple;"
		"background-color: #a6f;"
//		"background: linear-gradient(to right, fuchsia, white, white, fuchsia);"
//		"border: 1px solid white;" //causes changing of size
		"opacity: 0.1;"
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

	gchar *ht = g_strdup_printf("%s", text + len);

	webkit_dom_element_set_inner_html(hint, ht, NULL);
	g_free(ht);

	static const gchar *hintstyle =
//		"position: absolute;"
//		"-webkit-transform: rotate(-23deg);"
		"position: relative;"
		"z-index: 2147483647;"
		"font-size: medium !important;"
		"font-family: monospace !important;"
		"background: linear-gradient(#649, #203);"
		"color: white;"
//		"border: 1px solid red;"
		"border-radius: .3em;"
		"opacity: 0.%s;"
		"padding: .0em .2em 0em .2em;"
//		"font-weight: bold;"
//		"font-weight: normal;"
		"top: %s%d%s;"
		;

	const gchar *opacity = head ? "9" : "6";

	if (center)
		stylestr = g_strdup_printf(hintstyle, opacity, ".", 6, "em");
	else
		stylestr = g_strdup_printf(hintstyle, opacity, "-.", y > 6 ? 6 : y, "em");

	styledec = webkit_dom_element_get_style(hint);
	webkit_dom_css_style_declaration_set_css_text(styledec, stylestr, NULL);
	g_object_unref(styledec);
	g_free(stylestr);

	webkit_dom_node_append_child((WebKitDOMNode *)ret, (WebKitDOMNode *)hint, NULL);
	g_object_unref(hint);

	return ret;
}
static WebKitDOMElement *makehintelm(
		WebKitDOMDocument *doc, Elm *elm, const gchar* text, gint len,
		glong pagex, glong pagey)
{
	gchar *tag = webkit_dom_element_get_tag_name(elm->elm);
	bool center = isin(uritags, tag) && !isin(linktags, tag);
	g_free(tag);

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

		WebKitDOMElement *hint = _makehintelm(doc, center,
				y + elm->fy + pagey,
//				y + elm->fy + elm->gap,
				//gap is workaround. so x is left.
				x + elm->fx + pagex,
				h,
				w,
				text, len, i == 0);

		webkit_dom_node_append_child(
				(WebKitDOMNode *)ret, (WebKitDOMNode *)hint, NULL);
		g_object_unref(hint);
	}

	return ret;
#else

	return _makehintelm(doc, center,
			elm->y + elm->fy + pagey,
			elm->x + elm->fx + pagex, elm->h, elm->w, text, len, true);
#endif
}


static gint getdigit(gint len, gint num)
{
	gint tmp = num;
	gint digit = 1;
	while (tmp = tmp / len) digit++;
	return digit;
}

static gchar *makekey(gchar *keys, gint len, gint max, gint tnum, gint digit)
{
	gchar ret[digit + 1];
	ret[digit] = '\0';

	gint llen = len;
	while (llen--) {
		if (pow(llen, digit) < max)
			break;
	};
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

	WebKitDOMDocument  *doc  = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMElement   *elm  = webkit_dom_document_get_document_element(doc);
	WebKitDOMNode      *node = (WebKitDOMNode *)elm;

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
	ret.insight = true;

	glong bottom = ret.y + ret.h;
	glong right  = ret.x + ret.w;
	if (
		(ret.y <= 0         && bottom <= 0       ) ||
		(ret.y >= frect->h  && bottom >= frect->h) ||
		(ret.x <= 0         && right  <= 0       ) ||
		(ret.x >= frect->w  && right  >= frect->w)
	)
		goto retfalse;

#if ! NEWV
	if (styleis(dec, "display", "inline"))
	{
		WebKitDOMElement *le = te;
		while (le = webkit_dom_node_get_parent_element((WebKitDOMNode *)le))
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

	if (prect)
		trim(&ret, prect);

	if (js && (ret.h < 1 || ret.w < 1))
		goto retfalse;

	if (js && notttag && !styleis(dec, "cursor", "pointer"))
		goto retfalse;

	gchar *zc = webkit_dom_css_style_declaration_get_property_value(dec, "z-index");
	ret.zi = atoi(zc);
	g_free(zc);

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
	if (*elms)
	{
		for (GSList *next = *elms; next; next = next->next) {
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
	} else
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
		if (isin(clicktags, tag))
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
				styleis(dec, "overflow", "hidden") ||
				styleis(dec, "overflow", "scroll") ||
				styleis(dec, "overflow", "auto")
		)
		{
			crect = &elm;
		}
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
			WebKitDOMElement *te =
				(WebKitDOMElement *)webkit_dom_html_collection_item(cl, j);

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
	gchar uritype = 'n';
	gchar *uri = NULL;
	gchar *title = NULL;
	if (type == Curi || type == Cspawn)
	{
		uri = webkit_dom_element_get_attribute(te, "SRC");
		if (type == Cspawn)
		{
			gchar *tag = webkit_dom_element_get_tag_name(te);
			if (strcmp(tag, "IMG") == 0)
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
	else if (type == Cspawn)
		uritype = 'l';

	title =
		webkit_dom_html_element_get_inner_text((WebKitDOMHTMLElement *)te) ?:
		webkit_dom_element_get_attribute(te, "ALT") ?:
		webkit_dom_element_get_attribute(te, "TITLE");

	gchar *bases = webkit_dom_node_get_base_uri((WebKitDOMNode *)te);
	SoupURI *base = soup_uri_new(bases);
	SoupURI *last = soup_uri_new_with_base(base, uri);
	gchar *suri = soup_uri_to_string(last, false);

	gchar *retstr = g_strdup_printf("%c%s %s", uritype, suri, title);
	send(page, "hintret", retstr);

	soup_uri_free(base);
	soup_uri_free(last);
	g_free(uri);
	g_free(title);
	g_free(bases);
	g_free(suri);
	g_free(retstr);
}

static bool makehint(Page *page, Coms type, gchar *hintkeys, gchar *ipkeys)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
	page->lasttype = type;

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
	gint i = 0;
	elms = g_slist_reverse(elms);

	bool ret = false;
	for (GSList *next = elms; next; next = next->next)
	{
		Elm *elm = (Elm *)next->data;
		WebKitDOMElement *te = elm->elm;

		gchar *key = makekey(hintkeys, keylen, tnum, i, digit);
		i++;

		if (last)
		{
			if (!ret && strcmp(key, ipkeys) == 0)
			{
				webkit_dom_element_focus(te);

				if (type == Cclick)
				{
					bool isi = isinput(te);
					if (isi)
						send(page, "toinsert", NULL);
					else
						send(page, "tonormal", NULL);

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
					bool isa = strcmp(tag, "A") == 0;
					g_free(tag);

					if (page->script && !isi && !isa)
					{
						gchar *arg = g_strdup_printf("%d:%d",
								elm->x + elm->fx + elm->w / 2,
								elm->y + elm->fy + 1);
#endif
						send(page, "clickhere", arg);
						g_free(arg);
					}
					else
					{
						WebKitDOMEvent *ce =
							webkit_dom_document_create_event(doc, "MouseEvent", NULL);

						webkit_dom_event_init_event(ce, "click", true, true);

						webkit_dom_event_target_dispatch_event(
							(WebKitDOMEventTarget *)te, ce, NULL);

						g_object_unref(ce);
					}
				} else
					hintret(page, type, te);

				ret = true;
			}
		}
		else if (!ipkeys ||  g_str_has_prefix(key, ipkeys))
		{
			ret = true;
			WebKitDOMElement *ne = makehintelm(doc, elm, key, iplen, pagex, pagey);
			webkit_dom_node_append_child(page->apnode, (WebKitDOMNode *)ne, NULL);
			page->aplist = g_slist_prepend(page->aplist, ne);
		}

		g_free(key);
		clearelm(elm);
		g_free(elm);
	}

	g_slist_free(elms);

	return ret;
}



//@dom cbs

//static void domfocusincb(WebKitDOMElement *elm, WebKitDOMEvent *e, Page *page)
//{ send(page, "toinsert", NULL); }
//static void domfocusoutcb(WebKitDOMElement *welm, WebKitDOMEvent *ev, Page *page)
//{ send(page, "tonormal", NULL); }
//static void domactivatecb(WebKitDOMElement *welm, WebKitDOMEvent *ev, Page *page)
//{ DD(domactivate!) }
static void hintcb(WebKitDOMElement *welm, WebKitDOMEvent *ev, Page *page)
{
	if (page->apnode)
	{
		makehint(page, page->lasttype, NULL, NULL);
	}
}


//@misc com funcs
static void pagestart(Page *page)
{
	setwblist(false);

	g_slist_free_full(page->black, g_free);
	g_slist_free_full(page->white, g_free);
	page->black = NULL;
	page->white = NULL;

	if (tlpage == page)
		tlpage = NULL;
}

static void pageon(Page *page)
{
	//DD(pageon)
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	if (page->emitter) g_object_unref(page->emitter);
	page->emitter = webkit_dom_document_get_default_view(doc);
	WebKitDOMElement   *elm = webkit_dom_document_get_document_element(doc);

//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMFocusIn", G_CALLBACK(domfocusincb), false, page);
//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMFocusOut", G_CALLBACK(domfocusoutcb), false, page);
//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMActivate", G_CALLBACK(domactivatecb), false, page);

	//for refresh hint
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(page->emitter),
			"resize", G_CALLBACK(hintcb), false, page);
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(page->emitter),
			"scroll", G_CALLBACK(hintcb), false, page);
//may be heavy
//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMSubtreeModified", G_CALLBACK(hintcb), false, page);
}

static void mode(Page *page)
{
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMElement   *te = webkit_dom_document_get_active_element(doc);

	gchar *type = webkit_dom_element_get_attribute(te, "contenteditable");
	bool iseditable = type && strcmp(type, "true") == 0;
	g_free(type);

	if (iseditable || isinput(te))
		send(page, "toinsert", NULL);
	else
		send(page, "tonormal", NULL);
}

static void focus(Page *page)
{
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);

	WebKitDOMDOMSelection *selection =
		webkit_dom_dom_window_get_selection(win);

	WebKitDOMNode *an = NULL;

	an = webkit_dom_dom_selection_get_anchor_node(selection);
	an = an ?: webkit_dom_dom_selection_get_focus_node(selection);
	an = an ?: webkit_dom_dom_selection_get_base_node(selection);
	an = an ?: webkit_dom_dom_selection_get_extent_node(selection);

	if (an)
		do {
			WebKitDOMElement *elm = webkit_dom_node_get_parent_element(an);
			if (!elm) continue;
			gchar *tag = webkit_dom_element_get_tag_name(elm);

			if (isin(clicktags , tag))
			{
				webkit_dom_element_focus(elm);
				g_free(tag);
				break;
			}
			g_free(tag);

		} while (an = webkit_dom_node_get_parent_node(an));

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


//@ipccb
void ipccb(const gchar *line)
{
	gchar **args = g_strsplit(line, ":", 4);

	Page *page;
#if SHARED
	long lid = atol(args[0]);
	for (int i = 0; i < pages->len; i++)
		if (((Page *)pages->pdata[i])->id == lid)
			page = pages->pdata[i];
#else
	page = *pages->pdata;
#endif

	Coms type = *args[1];
	gchar *arg = args[2];

	gchar *ipkeys = NULL;
	switch (type) {
	case Cstart:
		page->relonly     = arg[0] == 'y';
		page->showblocked = arg[1] == 'y';
		g_free(page->cutheads);
		page->cutheads = g_strdup(arg + 2);
		pagestart(page);
		break;
	case Con:
		pageon(page);
		break;

	case Ckey:
		{
			gchar key[2] = {0};
			key[0] = toupper(arg[0]);
			arg = NULL;

			ipkeys = page->apkeys ?
				g_strconcat(page->apkeys, key, NULL) : g_strdup(key);

			type = page->lasttype;
		}

	case Cclick:
	case Clink:
	case Curi:
	case Cspawn:
		if (arg)
		{
			page->script = *arg == 'y';
			arg++;
		}
		if (!makehint(page, type, arg, ipkeys)) send(page, "tonormal", NULL);
		break;

	case Ctext:
	{
		WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
		makelist(page, doc, Ctext, NULL, NULL);
		break;
	}
	case Cmode:
		mode(page);
		break;

	case Cfocus:
		focus(page);
		break;

	case Cblur:
		blur(page);
	case Crm:
		rmhint(page);
		break;

	case Cwhite:
		showwhite(page, strcmp(arg, "white") == 0 ? true : false );
		break;

	case Ctlon:
		textlinkon(page);
		break;
	case Ctlcheck:
		textlinkcheck(false);
		break;

	case Cfree:
		freepage(page);
		break;

	default:
		D(extension gets unknown command %s, line)
	}
}


//@page cbs
static gint pagereq = 0;
static bool redirected = false;
static gboolean reqcb(
		WebKitWebPage *p,
		WebKitURIRequest *req,
		WebKitURIResponse *res,
		Page *page)
{
	const gchar *reqstr =  webkit_uri_request_get_uri(req);

	int check = checkwb(reqstr);
	if (check == 0)
	{
		addwhite(page, reqstr);
		return true;
	}

	pagereq++;

	SoupMessageHeaders *head = webkit_uri_request_get_http_headers(req);
	if (!head) return false; //scheme hasn't header
	const gchar *ref = soup_message_headers_get_list(head, "Referer");
	if (!ref) return false; //open page request

	if (res && pagereq == 2) //redirect of page
	{
		//in redirection we can't get current uri for reldomain check
		pagereq = 1;
		redirected = true;
		return false;
	}

	if (check == 1 || !page->relonly)
	{
		addblack(page, reqstr);
		return false;
	}


	//reldomain check
	bool ret = false;
	const gchar *uristr = webkit_web_page_get_uri(page->kit);

	SoupURI *puri = soup_uri_new(uristr);
	SoupURI *ruri = soup_uri_new(reqstr);

	const gchar *phost = soup_uri_get_host(puri);

	if (phost)
	{
		gchar **cuts = g_strsplit(page->cutheads, ";", -1);
		for (gchar **cut = cuts; *cut; cut++)
			if (g_str_has_prefix(phost, *cut))
			{
				phost += strlen(*cut);
				break;
			}
		g_strfreev(cuts);

		const gchar *rhost = soup_uri_get_host(ruri);
		if (!g_str_has_suffix(rhost, phost))
		{
			addwhite(page, reqstr);
			ret = true;
		}
	}

	if (!ret)
		addblack(page, reqstr);

	soup_uri_free(puri);
	soup_uri_free(ruri);
	return ret;
}
//static void formcb(WebKitWebPage *page, GPtrArray *elms, gpointer p) {}
//static void loadcb(WebKitWebPage *wp, gpointer p) {}
static void uricb(Page* page)
{
	//workaround: when in redirect change uri delays
	if (redirected)
		pagereq = 1;
	else
		pagereq = 0;

	redirected = false;
}

static void initex(WebKitWebExtension *ex, WebKitWebPage *wp)
{
	Page *page = g_new0(Page, 1);
	page->kit = wp;
	page->id = webkit_web_page_get_id(wp);
	g_ptr_array_add(pages, page);


	setwblist(false);
#if ! SHARED
	gchar *tmp = g_strdup_printf("%lu", page->id);
	ipcwatch(tmp);
	g_free(tmp);
#endif

//	SIG( page->kit, "context-menu"            , contextcb, NULL);
	SIG( page->kit, "send-request"            , reqcb    , page);
//	SIG( page->kit, "document-loaded"         , loadcb   , NULL);
	SIGW(page->kit, "notify::uri"             , uricb    , page);
//	SIG( page->kit, "form-controls-associated", formcb   , NULL);
}
G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(
		WebKitWebExtension *ex, const GVariant *v)
{
	const gchar *str = g_variant_get_string((GVariant *)v, NULL);
	fullname = g_strdup(g_strrstr(str, ";") + 1);

#if SHARED
	ipcwatch("ext");
#endif

	pages = g_ptr_array_new();

	SIG(ex, "page-created", initex, NULL);
}

