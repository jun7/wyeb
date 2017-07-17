# WithYourEditorBrowser / wyeb

[Screenshot](https://github.com/jun7/wyeb/wiki/img/screenshot.png)

### Todo
- Waiting for Webkit2gtk version 2.18 to get correct rects of elements

### Installation
depends 'webkit2gtk' 'markdown' 'perl-file-mimeinfo' //on arch linux

'markdown' 'perl-file-mimeinfo' are used only in main.conf

	make
	make install

### Features
wyeb is inspired from dwb and luakit, so basically usage is similar to those.

- Editable main page. It is a markdown text and contains bookmarks. **e** key opens it by editor.
- Settings per URI matched regular expression. And Ctrl + i/s switch setting set can be edited.
- Open actions. Most of actions assigned to keys are also can be accessed by shell.
For example, context-menu items we added are just shell scripts.
- Suffix. 'wyeb X "" ""' spawns a process using different dirs added the suffix 'X' for all data.
- [Window thumbnails.](https://github.com/jun7/wyeb/wiki/img/windowlist.png)
- No tab. But keys J/K/x or button actions.
- Focused history. Instead of loaded history.
- Misc. monitored conf files, saved search word for find, related domain only loading, whiteblack.conf, new window with clipboard text, hinting for callback script.
- [Adblock extension](https://github.com/jun7/wyebadblock). This takes boot time a lot and not controllable.

### Usage:
<pre>
command: wyeb [[[suffix] action|""] uri|arg|""]
  suffix: Process ID.
    It added to all directories conf, cache and etc.
  action: Such as new(default), open, opennew ...
    Except 'new' and some, actions are sent to a window last focused.


mouse:
  rocker gesture:
    left press and       -        right: back
    left press and move right and right: forward
    left press and move up    and right: raise bottom window and close
    left press and move down  and right: raise next   window and close
  middle button:
    on a link           : new background window
    on free space       : raise bottom window / show win list
    press and move left : raise bottom window / show win list
                                              / if mdlbtn2winlist: true
    press and move right: raise next   window
    press and move up   : go to top
    press and move down : go to bottom

context-menu:
  You can add your own script to context-menu. See 'menu' dir in
  the config dir, or click 'addMenu' in the context-menu. SUFFIX,
  ISCALLBACK, WINSLEN, WINID, URI, TITLE, PRIMARY/SELECTION,
  SECONDARY, CLIPBORAD, LINK, LINK_OR_URI, LINKLABEL, MEDIA, IMAGE,
  and MEDIA_IMAGE_LINK are set as environment variables. Available
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
0 - f          : tohint                 : 
0 - F          : tohintnew              : 
0 - t          : tohintback             : 
0 - T          : tohintbookmark         : 
0 - d          : tohintdl               : dl is Download
0 - D          : showdldir              : 
0 - y          : yankuri                : Clipboard
0 - b          : bookmark               : 
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
0 - x          : winlist                : 
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
4 - s          : setscript              : Use the 'set:script' section
4 - i          : setimage               : set:image
0 - u          : unset                  : 
0 - a          : addwhitelist           : URIs blocked by reldomain limitation
                                          and black list are added to whiteblack.conf
0 - A          : addblacklist           : URIs loaded
0 - (null)     : set                    : Use 'set:' + arg section of main.conf
0 - (null)     : new                    : 
0 - (null)     : newclipboard           : Open [arg + ' ' +] clipboard text
                                          in new window.
0 - (null)     : newselection           : Open [arg + ' ' +] selection ...
0 - (null)     : newsecondary           : Open [arg + ' ' +] secondaly ...
0 - (null)     : findclipboard          : 
0 - (null)     : findsecondary          : 
0 - (null)     : tohintopen             : 
0 - (null)     : openback               : 
0 - (null)     : download               : 
0 - (null)     : bookmarkthis           : 
0 - (null)     : bookmarklinkor         : 
0 - (null)     : showmsg                : 
0 - (null)     : tohintcallback         : arg is called with environment variables
                                          selected by hint.
0 - (null)     : sourcecallback         : 


</pre>
<hr>
<pre>

Copyright 2017 jun7

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
