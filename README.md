## Changes
Install dir /bin/ -> /usr/bin/

The directorys' name is changed from 'wyebrowser' to 'wyeb.' because it was too long.
wyeb uses old name if old conf dir is found though.
Changing the name loses the cache and local storage what webkit has saved.
See ~/.cache and ~/.local/share to keep it by rename or cleanup.

$SUFFIX's default value is changed from "" to "/" though "" in the args is still accepted.
So the double-quotations of $SUFFIX are no longer required.

mdlbtn2winlist of the conf is removed. set new key mdlbtnleft=winlist if you use it.

depends markdown -> discount: You have to add the flag '-f -style' to the generator of the main.conf

# WithYourEditorBrowser / wyeb

[Screenshot](https://github.com/jun7/wyeb/wiki/img/favicon.png)
/ [OwnStyleBookmarks](https://github.com/jun7/wyeb/wiki/img/bookmark.jpg)
/ [ContextMenuInFileManager](https://github.com/jun7/wyeb/wiki/img/contextmenu.jpg)
/ [Hinting](https://github.com/jun7/wyeb/wiki/img/hinting.png)
/ [History](https://github.com/jun7/wyeb/wiki/img/history.jpg)

### Installation
depends:

- arch linux: 'webkit2gtk' 'discount' 'perl-file-mimeinfo'
- debian 9.3: libwebkit2gtk-4.0-dev discount libfile-mimeinfo-perl

'discount(markdown)' 'perl-file-mimeinfo' are used only in main.conf

	make
	make install

### Features
wyeb is inspired by dwb and luakit, so basically usage is similar to them.

- Editable main page. It is a markdown text and contains bookmarks. **e** key opens it by editor. As this all settings are thrown to editor.
- Settings per URI matched regular expression. **e** on a page adds URI to the conf and opens it. And another thing, Ctrl + i/s and v switch setting set can be edited.
- Open actions. Most of actions assigned to keys are also can be accessed by shell.
For example, context-menu items we added are just shell scripts.
- Suffix. 'wyeb X "" ""' spawns a process using different dirs added the suffix 'X' for all data.
- [Hacked Hinting.](https://github.com/jun7/wyeb/wiki/img/hackedhint.png) For pages having javascript. This screenshot's wyeb uses webkit2gtk version 2.17.4
- [Window List.](https://github.com/jun7/wyeb/wiki/img/windowlist.jpg) Key **z**
- No tab. But keys J/K/x/X or button actions.
- Rocker gesture and middle button gesture. We can change it even to call a script. (e.g. mdlbtnleft=spawn sh -c "wyeb // showmsg \`pwd\`")
Of course it is in the set, so we can set it by uri.
- Focused history. Instead of loaded history.
- Pointer Mode. **p** makes pure click event for javascript pages.
- Range hinting. See hidden files in the menu dir. You have to assign keys for it by use of the accels.
- Misc. monitored conf files, saved search word for find, related domain only loading, whiteblack.conf, new window with clipboard text, hinting for callback script.
- [Adblock extension](https://github.com/jun7/wyebadblock). This takes boot time though.

### Usage:
Also there are [Tips](https://github.com/jun7/wyeb/wiki)
<pre>
command: wyeb [[[suffix] action|""] uri|arg|""]
  suffix: Process ID.
    It is added to all directories conf, cache and etc.
    '/' is default. '//' means $SUFFIX.
  action: Such as new(default), open, opennew ...
    Except 'new' and some, without a set of $SUFFIX and $WINID,
    actions are sent to a window last focused

mouse:
  rocker gesture:
    left press and       -        right: back
    left press and move right and right: forward
    left press and move up    and right: raise bottom window and close
    left press and move down  and right: raise next   window and close
  middle button:
    on a link           : new background window
    on free space       : raise bottom window
    press and move left : raise bottom window
    press and move right: raise next   window
    press and move up   : go to top
    press and move down : go to bottom

context-menu:
  You can add your own script to context-menu. See 'menu' dir in
  the config dir, or click 'editMenu' in the context-menu. SUFFIX,
  ISCALLBACK, WINSLEN, WINID, URI, TITLE, PRIMARY/SELECTION,
  SECONDARY, CLIPBORAD, LINK, LINK_OR_URI, LINKLABEL, LABEL_OR_TITLE,
  MEDIA, IMAGE, MEDIA_IMAGE_LINK, CURRENTSET and DLDIR
  are set as environment variables. Available
  actions are in 'key:' section below. Of course it supports dir
  and '.'. '.' hides it from menu but still available in the accels.
accels:
  You can add your own keys to access context-menu items we added.
  To add Ctrl-Z to GtkAccelMap, insert '&lt;Primary&gt;&lt;Shift&gt;z' to the
  last "" in the file 'accels' in the conf directory assigned 'c'
  key, and remeve the ';' at the beginning of line. alt is &lt;Alt&gt;.

key:
4 - is ctrl
(null) is only for script

0 - Escape     : tonormal               : To Normal Mode
4 - bracketleft: tonormal               : 
0 - i          : toinsert               : 
0 - I          : toinsertinput          : To Insert Mode with focus of first input
0 - p          : topointer              : pp resets damping
0 - P          : tomdlpointer           : make middle click
4 - p          : torightpointer         : right click
0 - f          : tohint                 : 
0 - F          : tohintnew              : 
0 - t          : tohintback             : 
0 - T          : tohintbookmark         : 
0 - d          : tohintdl               : dl is Download
0 - D          : showdldir              : 
0 - y          : yankuri                : Clipboard
0 - Y          : yanktitle              : Clipboard
0 - b          : bookmark               : arg: "" or "uri + ' ' + label"
0 - B          : bookmarkbreak          : Add line break to the main page
0 - q          : quit                   : 
0 - Q          : quitall                : 
0 - j          : scrolldown             : 
0 - k          : scrollup               : 
0 - h          : scrollleft             : 
0 - l          : scrollright            : 
4 - j          : arrowdown              : 
4 - k          : arrowup                : 
4 - h          : arrowleft              : 
4 - l          : arrowright             : 
4 - f          : pagedown               : 
4 - b          : pageup                 : 
0 - g          : top                    : 
0 - G          : bottom                 : 
0 - plus       : zoomin                 : 
0 - minus      : zoomout                : 
0 - equal      : zoomreset              : 
0 - J          : nextwin                : 
0 - K          : prevwin                : 
0 - x          : quitnext               : 
0 - X          : quitprev               : 
0 - z          : winlist                : 
0 - H          : back                   : 
0 - L          : forward                : 
0 - s          : stop                   : 
0 - r          : reload                 : 
0 - R          : reloadbypass           : reload bypass cache
0 - slash      : find                   : 
0 - n          : findnext               : 
0 - N          : findprev               : 
0 - asterisk   : findselection          : 
0 - o          : open                   : 
0 - w          : opennew                : New window
0 - O          : edituri                : 
0 - W          : editurinew             : 
0 - colon      : showhelp               : 
0 - M          : showhistory            : 
0 - m          : showmainpage           : 
4 - C          : clearallwebsitedata    : 
0 - e          : edit                   : 
0 - E          : editconf               : 
0 - c          : openconfigdir          : 
0 - v          : setv                   : Use the 'set:v' section
4 - s          : setscript              : Use the 'set:script' section
4 - i          : setimage               : set:image
0 - u          : unset                  : 
0 - a          : addwhitelist           : URIs blocked by reldomain limitation
                                          and black list are added to whiteblack.conf
0 - A          : addblacklist           : URIs loaded
4 - e          : textlink               : For textarea in insert mode
0 - (null)     : set                    : Use 'set:' + arg section of main.conf
0 - (null)     : set2                   : Not toggle
0 - (null)     : new                    : 
0 - (null)     : newclipboard           : Open [arg + ' ' +] clipboard text
                                          in a new window.
0 - (null)     : newselection           : Open [arg + ' ' +] selection ...
0 - (null)     : newsecondary           : Open [arg + ' ' +] secondaly ...
0 - (null)     : findclipboard          : 
0 - (null)     : findsecondary          : 
0 - (null)     : tohintopen             : 
0 - (null)     : openback               : 
0 - (null)     : openwithref            : current uri is sent as Referer
0 - (null)     : download               : 
0 - (null)     : showmsg                : 
0 - (null)     : click                  : x:y
0 - (null)     : spawn                  : arg is called with environment variables
0 - (null)     : jscallback             : run script of arg1 and
                                          arg2 is called with $JSRESULT
0 - (null)     : tohintcallback         : arg is called with environment variables
                                          selected by hint.
0 - (null)     : tohintrange            : Same as tohintcallback but range.
0 - (null)     : sourcecallback         : the web resource is sent via pipe


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
