# wyeb

![Hinting](https://github.com/jun7/wyeb/wiki/img/hinting.png)

[Screenshot](https://github.com/jun7/wyeb/wiki/img/favicon.png)
/ [OwnStyleBookmarks](https://github.com/jun7/wyeb/wiki/img/bookmark.png)
/ [ContextMenuInFileManager](https://github.com/jun7/wyeb/wiki/img/contextmenu.jpg)
/ [History](https://github.com/jun7/wyeb/wiki/img/history.jpg)

### Features
wyeb is inspired by dwb and luakit, so basically usage is similar to them.

- Editable main page. It is a markdown text containing bookmarks.
**e** key opens it by an text editor. As this, all settings are thrown to text editors.
- Monitored conf files. For example, do `echo "* {color:red \!important}" >> user.css` on the conf dir(key **c**).
It should be applied immediately.
- Settings per URI matched regular expression. **e** on a page adds URI to the conf and opens it.
And another thing, **ctrl-i/s** and **v** switch setting 'set:' can be edited.
- Open actions. Most of actions assigned to keys can be accessed by shell.
For example, context-menu items we added are just shell scripts.
- Suffix. `wyeb X "" ""` spawns a process using different dirs added the suffix 'X' for all data.
- [Hacked Hinting.](https://github.com/jun7/wyeb/wiki/img/hackedhint.png) For pages having javascript.
- [Window List.](https://github.com/jun7/wyeb/wiki/img/windowlist.jpg) Key **z**
- No tab. But keys **J/K/x/X** or button actions. `tabbed wyeb plugto` works though.
Make sure tabbed takes no notice of the reordering of wins
without adding `if(sel != c) focus(c);` to the configure event.
- Rocker gestures and middle button gestures. We can change it even to call a script.
(e.g. mdlbtnleft=spawn sh -c "wyeb // showmsg \`pwd\`")
Of course it is in the 'set;', so we can set it by uri.
- Pointer Mode. **p** makes pure click event for javascript pages.
Also it moves pointer pos used by scroll and keeps pos last clicked for same layout pages.
- Range hinting. **ctrl-r**. Also see hidden files in the menu dir, it has callback interface.
- Misc. related domain only loading,
whiteblack.conf, new window with clipboard text, hinting for callback scripts.
- [Adblock extension](https://github.com/jun7/wyebadblock).

### Installation
depends:

- arch linux: 'webkit2gtk' 'discount' 'perl-file-mimeinfo'
- debian 9.4: libwebkit2gtk-4.0-dev discount libfile-mimeinfo-perl

'discount(markdown)' 'perl-file-mimeinfo' are used only in the main.conf

	make
	sudo make install

For testing, make and run without install

	./testrun.sh

For arch linux: https://aur.archlinux.org/packages/wyeb-git/

### Usage:
Also there are [Tips](https://github.com/jun7/wyeb/wiki)
<pre>

usage: wyeb [[[suffix] action|""] uri|arg|""]

  wyeb google.com
  wyeb new google.com
  wyeb / new google.com

  suffix: Process ID.
    It is added to all directories conf, cache and etc.
    '/' is default. '//' means $SUFFIX.
  action: Such as new(default), open, opennew ...
    Except 'new' and some, without a set of $SUFFIX and $WINID,
    actions are sent to the window last focused

mouse:
  rocker gesture:
    left press and       -        right: back
    left press and move right and right: forward
    left press and move up    and right: raise bottom window and close
    left press and move down  and right: raise next   window and close
  middle button:
    on a link            : new background window
    on free space        : winlist
    press and move left  : raise bottom window
    press and move right : raise next   window
    press and move up    : go to top
    press and move down  : go to bottom
    press and scroll up  : go to top
    press and scroll down: go to bottom

context-menu:
  You can add your own script to context-menu. See 'menu' dir in
  the config dir, or click 'editMenu' in the context-menu.
  ISCALLBACK, SUFFIX, WINID, WINSLEN, CURRENTSET, URI, TITLE, FOCUSURI,
  LINK, LINK_OR_URI, LINKLABEL, LABEL_OR_TITLE,
  MEDIA, IMAGE, MEDIA_IMAGE_LINK,
  WINX, WINY, WIDTH, HEIGHT, CANBACK, CANFORWARD,
  PRIMARY/SELECTION, SECONDARY, CLIPBORAD,
  DLDIR and CONFDIR are set as environment variables.
  Available actions are in the 'key:' section below.
  Of course it supports directories and '.'.
  '.' hides it from the menu but still available in the accels.
accels:
  You can add your own keys to access context-menu items we added.
  To add Ctrl-Z to GtkAccelMap, insert '<Primary><Shift>z' to the
  last "" in the file 'accels' in the conf directory assigned 'c'
  key, and remove the ';' at the beginning of the line. alt is <Alt>.

key:
#4 - is ctrl
#(null) is only for script
0 - Escape     : tonormal           : To Normal Mode
4 - bracketleft: tonormal           : 
0 - i          : toinsert           : 
0 - I          : toinsertinput      : To Insert Mode with focus of first input
0 - p          : topointer          : pp resets damping. Esc clears pos. Press enter/space makes btn press
0 - P          : topointermdl       : Makes middle click
4 - p          : topointerright     : right click
0 - f          : tohint             : 
0 - F          : tohintnew          : 
0 - t          : tohintback         : 
0 - d          : tohintdl           : dl is Download
0 - T          : tohintbookmark     : 
4 - r          : tohintrangenew     : Open new windows
0 - D          : showdldir          : 
0 - y          : yankuri            : Clipboard
0 - Y          : yanktitle          : Clipboard
0 - b          : bookmark           : arg: "" or "uri + ' ' + label"
0 - B          : bookmarkbreak      : Add line break to the main page
0 - q          : quit               : 
0 - Q          : quitall            : 
0 - j          : scrolldown         : 
0 - k          : scrollup           : 
0 - h          : scrollleft         : 
0 - l          : scrollright        : 
4 - j          : arrowdown          : 
4 - k          : arrowup            : 
4 - h          : arrowleft          : 
4 - l          : arrowright         : 
4 - f          : pagedown           : 
4 - b          : pageup             : 
4 - d          : halfdown           : 
4 - u          : halfup             : 
0 - g          : top                : 
0 - G          : bottom             : 
0 - plus       : zoomin             : 
0 - minus      : zoomout            : 
0 - equal      : zoomreset          : 
0 - J          : nextwin            : 
0 - K          : prevwin            : 
0 - x          : quitnext           : Raise next win and quit current win
0 - X          : quitprev           : 
0 - z          : winlist            : 
0 - H          : back               : 
0 - L          : forward            : 
0 - s          : stop               : 
0 - r          : reload             : 
0 - R          : reloadbypass       : Reload bypass cache
0 - slash      : find               : 
0 - n          : findnext           : 
0 - N          : findprev           : 
0 - asterisk   : findselection      : 
0 - o          : open               : 
0 - w          : opennew            : New window
0 - O          : edituri            : Edit arg or focused link or current page's URI
0 - W          : editurinew         : 
0 - colon      : showhelp           : 
0 - M          : showhistory        : 
4 - m          : showhistoryall     : 
0 - m          : showmainpage       : 
4 - C          : clearallwebsitedata: 
0 - e          : edit               : Edit current uri conf or mainpage
0 - E          : editconf           : 
0 - c          : openconfigdir      : 
0 - v          : setv               : Use the 'set:v' group
4 - s          : setscript          : Use the 'set:script' group
4 - i          : setimage           : set:image
0 - u          : unset              : 
0 - a          : addwhitelist       : Add URIs blocked to whiteblack.conf as white list
0 - A          : addblacklist       : URIs loaded
4 - e          : textlink           : For text elements in insert mode
0 - (null)     : set                : Use 'set:' + arg group of main.conf. This toggles
0 - (null)     : set2               : Not toggle
0 - (null)     : setstack           : arg == NULL ? remove last : add set without checking duplicate
0 - (null)     : new                : 
0 - (null)     : newclipboard       : Open [arg + ' ' +] clipboard text in a new window
0 - (null)     : newselection       : Open [arg + ' ' +] selection ...
0 - (null)     : newsecondary       : Open [arg + ' ' +] secondaly ...
0 - (null)     : findclipboard      : 
0 - (null)     : findsecondary      : 
0 - (null)     : tohintopen         : not click but opens uri as opennew/back
0 - (null)     : openback           : 
0 - (null)     : openwithref        : Current uri is sent as Referer
0 - (null)     : download           : 
0 - (null)     : dlwithheaders      : Current uri is sent as Referer. Also cookies
0 - (null)     : showmsg            : 
0 - (null)     : raise              : 
0 - (null)     : winpos             : x:y
0 - (null)     : winsize            : w:h
0 - (null)     : click              : x:y
0 - (null)     : openeditor         : 
0 - (null)     : spawn              : arg is called with environment variables
0 - (null)     : jscallback         : Runs script of arg1 and arg2 is called with $RESULT
0 - (null)     : tohintcallback     : arg is called with env selected by hint
0 - (null)     : tohintrange        : Same as tohintcallback but range
0 - (null)     : sourcecallback     : The web resource is sent via pipe
0 - (null)     : cookies            : ` wyeb // cookies $URI 'sh -c "echo $RESULT"' ` prints headers.
  Make sure, the callbacks of wyeb are async.
  The stdout is not caller's but first process's stdout.

</pre>
<hr>
<pre>

Copyright 2017-2018 jun7

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

</pre>
