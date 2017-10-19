### eXtremely simple Terminal based on VTE.

xt is an eXtremely simple Terminal based on VTE, intend to be used with tilling wm such as i3/sway etc.

### usage:
xt [-r] [-k] [-f font] [-t transparent] [-n history] [[-e] command [args ...]]

Args:

* -r: reverse teminal color
* -k: disable preset keyboard shortcuts
* -f <string>: set font, such as "Monospace 11"
* -t <int>: background tranparent percent
* -n <int>: lines of history
* -e <command [args ...]>: excute command with args, eat all remainning args.

### Shortcuts:
Default shortcuts can be disabled via '-k' arg.

* Ctrl+Shift+c: copy to clipboard
* Ctrl+Shift+v/Ctrl+Shift+y: paste from clipboard
* Ctrl+Shift+=: increase font size for session
* Ctrl+-: decrease font size for session
* Ctrl+=: reset font size to default value
