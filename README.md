### eXtremely simple Terminal based on VTE.

xt is an eXtremely simple Terminal based on VTE, intend to be used with tilling wm such as i3/sway etc.

### usage:
xt [-r] [-k] [-f font] [-t transparent] [-n history] [[-e] command [args ...]]

Args:

* -r: reverse terminal color
* -k: disable default shortcuts
* -d: disable Gtk CSD. default for sway wm
* -f &lt;string&gt;: set font, such as "Monospace 11"
* -t &lt;int&gt;: background tranparency percent
* -n &lt;int&gt;: lines of history, default is unlimited
* -e &lt;command [args ...]&gt;: excute command with args, eat all remainning args.

### Shortcuts:
Default shortcuts can be disabled via '-k' arg.

* Ctrl+Shift+c: copy to clipboard
* Ctrl+Shift+v: paste from clipboard
* Ctrl+Shift+y: paste from primary 
* Ctrl+Shift+=: increase font size for session
* Ctrl+-: decrease font size for session
* Ctrl+=: reset font size to default value
