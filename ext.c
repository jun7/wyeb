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


typedef struct {
	WebKitWebPage     *kit;
	gchar             *id;
	GSList            *appended;
	WebKitDOMNode     *apnode;
	gchar             *apkeys;
	gchar              lasttype;
	gchar             *lasthintkeys;
} Page;

static GPtrArray *pages = NULL;

static void free_page(Page *page)
{
	g_free(page->id);
	g_slist_free(page->appended);
	g_free(page->apkeys);
	g_free(page->lasthintkeys);

	g_free(page);
}

typedef struct {
	WebKitDOMElement *elm;
	glong top;
	glong left;
	glong height;
	glong width;
	glong zi;
} Elm;

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
	"SEARCH", "TEXT", "URL", "EMAIL",  "PASSWORD", "TEL", NULL
};
static const gchar *ilimitedtext[] = {
	"MONTH",  "NUMBER", "TIME", "WEEK", "DATE", "DATETIME-LOCAL", NULL
};
static const gchar *inottext[] = {
	"COLOR", "FILE", "RADIO", "RANGE", "CHECKBOX", "BUTTON", "RESET", "SUBMIT",

	// unknown
	"IMAGE", //to be submit
	// not focus
	"HIDDEN",
	NULL
};


static void send(Page *page, gchar *action, gchar *arg)
{
	gchar *ss = g_strconcat(page->id, ":", action, ":", arg, NULL);
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
		if (strcmp(tag, "input") != 0) return true;

		gchar *type = webkit_dom_element_get_attribute(te, "type");
		if (!type || !isin(inottext, type))
			ret = true;
	}
	g_free(tag);

	return ret;
}

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

//D(width %d %f %f %s,
//		webkit_dom_element_get_scroll_width(te),//editor have if lot inputed then lot
//		webkit_dom_element_get_offset_width(te),
//		webkit_dom_element_get_client_width(te),//editor have
//		webkit_dom_element_get_inner_html(te)
//)
		//for version 2.18
//		WebKitDOMClientRect *rect = 
//			webkit_dom_element_get_bounding_client_rect(te);
//		WebKitDOMClientRectList *rects = 
//			webkit_dom_element_get_client_rects(te);
//		glong top, left, bottom, right;
//		top = rect->top();
//		left = rect->left();
//		bottom = rect->bottom();
//		right = rect->right();
//		width = rect->width();
//		height = rect->height();

	elm.height = webkit_dom_element_get_offset_height(te);
	elm.width  = webkit_dom_element_get_offset_width(te);

	for (WebKitDOMElement *le = te; le;
			le = webkit_dom_element_get_offset_parent(le))
	{
		elm.top +=
			webkit_dom_element_get_offset_top(le) -
			webkit_dom_element_get_scroll_top(le);
		elm.left +=
			webkit_dom_element_get_offset_left(le) -
			webkit_dom_element_get_scroll_left(le);
	}
	return elm;
}


static WebKitDOMElement *makehintelm(
		WebKitDOMDocument *doc, Elm *elm, const gchar* text, gint len)
{
	WebKitDOMElement *ret = webkit_dom_document_create_element(doc, "div", NULL);
	WebKitDOMElement *area = webkit_dom_document_create_element(doc, "div", NULL);
	WebKitDOMElement *hint = webkit_dom_document_create_element(doc, "span", NULL);

	//ret
	static const gchar *retstyle =
		"position: absolute;"
		"overflow: visible;"
		"display: block;"
		"visibility: visible;"
		"font-size: medium !important;"
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
			retstyle, elm->top, elm->left, elm->height, elm->width);

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
	gchar *pre = g_strdup(text);
	pre[len] = '\0';
	gchar *ht = g_strdup_printf("%s<b>%s</b>", pre, text + len);
	webkit_dom_element_set_inner_html(hint, ht, NULL);
	g_free(ht);
	g_free(pre);

	static const gchar *hintstyle =
//		"position: absolute;"
//		"-webkit-transform: rotate(-23deg);"
		"position: relative;"
		"z-index: 2147483647;"
		"font-size: medium !important;"
		"font-family: monospace;"
		"background: linear-gradient(#649, #203);"
		"color: white;"
//		"border: 1px solid red;"
		"border-radius: .3em;"
		"opacity: 0.9;"
		"padding: .0em .2em 0em .12em;"
//		"font-weight: bold;"
//		"font-weight: normal;"
		"top: %s%d%s;"
		;

	gchar *tag = webkit_dom_element_get_tag_name(elm->elm);

	if (isin(uritags, tag) && !isin(linktags, tag))
		stylestr = g_strdup_printf(hintstyle, "", elm->height * 3 / 7, "px");
	else
		stylestr = g_strdup_printf(hintstyle, "-.", elm->top > 6 ? 6 : elm->top, "em");
	g_free(tag);

	styledec = webkit_dom_element_get_style(hint);
	webkit_dom_css_style_declaration_set_css_text(styledec, stylestr, NULL);
	g_object_unref(styledec);
	g_free(stylestr);

	webkit_dom_node_append_child((WebKitDOMNode *)ret, (WebKitDOMNode *)hint, NULL);
	g_object_unref(hint);

	return ret;
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

	for (GSList *next = page->appended; next; next = next->next)
	{
		if (page->apnode == node)
			webkit_dom_node_remove_child(page->apnode, next->data, NULL);

		g_object_unref(next->data);
	}

	g_slist_free(page->appended);
	g_free(page->apkeys);
	page->appended = NULL;
	page->apnode = NULL;
	page->apkeys = NULL;
}

static GSList *makelist(Page *page, gchar type, gint *tnum)
{
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);

	page->apnode = (WebKitDOMNode *)webkit_dom_document_get_document_element(doc);

//	D(outer %d, webkit_dom_dom_window_get_outer_height(win));
//	D(otop %d, webkit_dom_dom_window_get_page_y_offset(win));
	glong winheight = webkit_dom_dom_window_get_inner_height(win);
	glong winwidth  = webkit_dom_dom_window_get_inner_width(win);

	glong wintop    = webkit_dom_dom_window_get_scroll_y(win);
	glong winleft   = webkit_dom_dom_window_get_scroll_x(win);

	GSList *elms = NULL;

	const gchar **taglist = clicktags;
	if (type == Clink) taglist = linktags;
	if (type == Curi ) taglist = uritags;
	if (type == Ctext) taglist = texttags;

	for (const gchar **tag = taglist; *tag; tag++)
	{
		WebKitDOMHTMLCollection *cl =
			webkit_dom_document_get_elements_by_tag_name_as_html_collection(doc, *tag);

		for (gint j = 0; j < webkit_dom_html_collection_get_length(cl) ; j++)
		{
			WebKitDOMNode *tn = webkit_dom_html_collection_item(cl, j);
			WebKitDOMElement *te = (WebKitDOMElement *)tn;

			//elms visibility hidden have size also opacity
			WebKitDOMCSSStyleDeclaration *dec =
				webkit_dom_dom_window_get_computed_style(win, te, NULL);

			static gchar *check[][2] = {
				{"visibility", "hidden"},
				{"opacity"   , "0"},
			};
			bool checkb = false;
			for (int k = 0; k < sizeof(check) / sizeof(*check); k++)
			{
				if (styleis(dec, check[k][0], check[k][1]))
					checkb = true;

				if (checkb) break;
			}
			if (checkb)
			{
				g_object_unref(dec);
				continue;
			}

			Elm rect = getrect(te);
			//no size no operation //not works, see google's page nav
			//if (height == 0 || width == 0) continue;

			glong bottom = rect.top  + rect.height;
			glong right  = rect.left + rect.width;

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
						glong nr = MIN(right, rectp.left + rectp.width);
						rect.width += nr - right;
						right = nr;
						break;
					}
					g_object_unref(decp);
				}
			}

			if (
				(rect.top  <= 0         && bottom <= 0        ) ||
				(rect.top  >= winheight && bottom >= winheight) ||
				(rect.left <= 0         && right  <= 0        ) ||
				(rect.left >= winwidth  && right  >= winwidth )
				)
			{
				g_object_unref(dec);
				continue;
			}


			//now the element is in sight

			if (type == Ctext)
			{
				g_object_unref(dec);

				if (!isinput(te)) continue;

				webkit_dom_element_focus(te);
				g_object_unref(win);
				return NULL;
			}

			rect.top += wintop;
			rect.left += winleft;

			(*tnum)++;
			Elm *elm = g_new(Elm, 1);
			*elm = rect;
			elm->elm = te;
			elm->zi = -1;

			gchar *zc = webkit_dom_css_style_declaration_get_property_value(dec, "z-index");
			elm->zi = atoi(zc);
			g_free(zc);

			if (elms)
			{
				for (GSList *next = elms; next; next = next->next) {
					if (elm->zi >= ((Elm *)next->data)->zi)
					{
						elms = g_slist_insert_before(elms, next, elm);
						break;
					}

					if (!next->next)
					{
						elms = g_slist_append(elms, elm);
						break;
					}
				}
			} else
				elms = g_slist_append(elms, elm);

			g_object_unref(dec);
		}
		g_object_unref(cl);
	}

	g_object_unref(win);
	return elms;
}


static bool makehint(Page *page, gchar type, gchar *hintkeys, gchar *ipkeys)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);

	page->lasttype = type;

	g_free(page->lasthintkeys);
	page->lasthintkeys = g_strdup(hintkeys);

	if (strlen(hintkeys) < 3) hintkeys = HINTKEYS;
	rmhint(page);

	page->apkeys = ipkeys;


	gint tnum = 0;
	GSList *elms = makelist(page, type, &tnum);

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
					if (isinput(te))
						send(page, "toinsert", NULL);
					else
						send(page, "tonormal", NULL);

					WebKitDOMEvent *ce =
						webkit_dom_document_create_event(doc, "MouseEvent", NULL);

					webkit_dom_event_init_event(ce, "click", true, true);

//					webkit_dom_mouse_event_init_mouse_event(
//							(WebKitDOMMouseEvent *)ce,
//							"click", //const gchar *type,
//							true,    //gboolean canBubble,
//							true,    //gboolean cancelable,
//							win,     //WebKitDOMDOMWindow *view,
//							0,       //glong detail,
//							0,       //glong screenX,
//							0,       //glong screenY,
//					  		//glong clientX,
//							webkit_dom_element_get_client_left(elm->elm) + 1,
//							//glong clientY,
//							webkit_dom_element_get_client_top(elm->elm) + 1,
//							false,   //gboolean ctrlKey,
//							false,   //gboolean altKey,
//							false,   //gboolean shiftKey,
//							false,   //gboolean metaKey,
//							1,       //gushort button,
//							(WebKitDOMEventTarget *)elm->elm);

					webkit_dom_event_target_dispatch_event(
						(WebKitDOMEventTarget *)te, ce, NULL);

					g_object_unref(ce);
				} else {
					gchar *href = NULL;
					if (type == Curi)
						href = webkit_dom_element_get_attribute(te, "SRC");

					if (!href)
						href = webkit_dom_element_get_attribute(te, "HREF");

					if (!href)
						href = g_strdup("about:blank");

					gchar *bases = webkit_dom_node_get_base_uri((WebKitDOMNode *)te);
					SoupURI *base = soup_uri_new(bases);
					SoupURI *last = soup_uri_new_with_base(base, href);
					gchar *retstr = soup_uri_to_string(last, false);

					send(page, "hintret", retstr);

					soup_uri_free(base);
					soup_uri_free(last);
					g_free(href);
					g_free(bases);
					g_free(retstr);
				}

				ret = true;
			}
		}
		else if (!ipkeys ||  g_str_has_prefix(key, ipkeys))
		{
			ret = true;
			WebKitDOMElement *ne = makehintelm(doc, elm, key, iplen);

			webkit_dom_node_append_child(page->apnode, (WebKitDOMNode *)ne, NULL);
			page->appended = g_slist_prepend(page->appended, ne);
		}

		g_free(key);

		g_free(elm);
	}

	g_slist_free(elms);

	return ret;
}

//dom cbs

//static void domfocusincb(WebKitDOMElement *elm, WebKitDOMEvent *e, Page *page)
//{ send(page, "toinsert", NULL); }
//static void domfocusoutcb(WebKitDOMElement *welm, WebKitDOMEvent *ev, Page *page)
//{ send(page, "tonormal", NULL); }
//static void domactivatecb(WebKitDOMElement *welm, WebKitDOMEvent *ev, Page *page)
//{ DD(domactivate!) }
static void hintcb(WebKitDOMElement *welm, WebKitDOMEvent *ev, Page *page)
{
	if (page->appended)
	{
		gchar *k = g_strdup(page->lasthintkeys);
		makehint(page, page->lasttype, k, NULL);
		g_free(k);
	}
}
static void pageon(Page *page)
{
	//DD(pageon)
	WebKitDOMDocument  *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);
	WebKitDOMElement   *elm = webkit_dom_document_get_document_element(doc);

//WEBKIT_DOM_EVENT_NONE
//WEBKIT_DOM_EVENT_CAPTURING_PHASE
//WEBKIT_DOM_EVENT_AT_TARGET
//WEBKIT_DOM_EVENT_BUBBLING_PHASE
//WEBKIT_DOM_EVENT_MOUSEDOWN
//WEBKIT_DOM_EVENT_MOUSEUP
//WEBKIT_DOM_EVENT_MOUSEOVER
//WEBKIT_DOM_EVENT_MOUSEOUT
//WEBKIT_DOM_EVENT_MOUSEMOVE
//WEBKIT_DOM_EVENT_MOUSEDRAG
//WEBKIT_DOM_EVENT_CLICK
//WEBKIT_DOM_EVENT_DBLCLICK
//WEBKIT_DOM_EVENT_KEYDOWN
//WEBKIT_DOM_EVENT_KEYUP
//WEBKIT_DOM_EVENT_KEYPRESS
//WEBKIT_DOM_EVENT_DRAGDROP
//WEBKIT_DOM_EVENT_FOCUS
//WEBKIT_DOM_EVENT_BLUR
//WEBKIT_DOM_EVENT_SELECT
//WEBKIT_DOM_EVENT_CHANGE

//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMFocusIn", G_CALLBACK(domfocusincb), false, page);
//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMFocusOut", G_CALLBACK(domfocusoutcb), false, page);
//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMActivate", G_CALLBACK(domactivatecb), false, page);

	//for refresh hint
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(win),
			"resize", G_CALLBACK(hintcb), false, page);
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(win),
			"scroll", G_CALLBACK(hintcb), false, page);
//may be heavy
//	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(elm),
//			"DOMSubtreeModified", G_CALLBACK(hintcb), false, page);
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

void ipccb(const gchar *line)
{
	gchar **args = g_strsplit(line, ":", 4);

	Page *page;
#if SHARED
		page = pages->pdata[atoi(args[0]) - 1];
#else
		page = *pages->pdata;
#endif

	gchar type = *args[1];
	gchar *arg = args[2];

	gchar *ipkeys = NULL;
	switch (type) {
	case Con:
		pageon(page);
		break;

	case Ckey:
		{
			gchar key[2] = {0};
			key[0] = toupper(arg[0]);
			arg++;

			ipkeys = page->apkeys ?
				g_strconcat(page->apkeys, key, NULL) : g_strdup(key);

			type = page->lasttype;
		}

	case Cclick:
	case Clink:
	case Curi:
		if (!makehint(page, type, arg, ipkeys)) send(page, "tonormal", NULL);
		break;

	case Ctext:
		makelist(page, Ctext, NULL);
		break;

	case Cfocus:
		focus(page);
		break;

	case Cblur:
		blur(page);
	case Crm:
		rmhint(page);
		break;

	case Cfree:
		free_page(page);
		break;

	default:
		D(extension gets unknown command %s, line)
	}
}

//page cbs
//static gboolean contextcb(
//		WebKitWebPage *w,
//		WebKitContextMenu *menu,
//		WebKitWebHitTestResult *htr,
//		gpointer p)
//{ return false; }
//static gboolean reqcb( //for adblock
//		WebKitWebPage *page,
//		WebKitURIRequest *req,
//		WebKitURIResponse *res,
//		gpointer p)
//{ return false; }
//static void formcb(WebKitWebPage *page, GPtrArray *elms, gpointer p) {}
//static void loadcb(WebKitWebPage *wp, gpointer p) {}
//static void uricb(Page* page) {}
static void initex(WebKitWebExtension *ex, WebKitWebPage *wp)
{
	Page *page = g_new0(Page, 1);
	page->kit = wp;
	page->id = g_strdup_printf("%lu", webkit_web_page_get_id(wp));
	g_ptr_array_add(pages, page);

#if ! SHARED
	ipcwatch(page->id);
#endif

//	SIG( page->kit, "context-menu"            , contextcb, NULL);
//	SIG( page->kit, "send-request"            , reqcb    , NULL);
//	SIG( page->kit, "document-loaded"         , loadcb   , NULL);
//	SIGW(page->kit, "notify::uri"             , uricb    , page);
//	SIG( page->kit, "form-controls-associated", formcb   , NULL);
}
G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(
		WebKitWebExtension *ex, const GVariant *v)
{
	fullname = g_strdup(g_variant_get_string((GVariant *)v, NULL));

#if SHARED
	ipcwatch("ext");
#endif

	pages = g_ptr_array_new();

	SIG(ex, "page-created", initex, NULL);
}

