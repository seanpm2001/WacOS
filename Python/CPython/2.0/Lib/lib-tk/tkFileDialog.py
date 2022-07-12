#
# Instant Python
# $Id$
#
# tk common file dialogues
#
# this module provides interfaces to the native file dialogues
# available in Tk 4.2 and newer.
#
# written by Fredrik Lundh, May 1997.
#

#
# options (all have default values):
#
# - defaultextension: added to filename if not explicitly given
#
# - filetypes: sequence of (label, pattern) tuples.  the same pattern
#   may occur with several patterns.  use "*" as pattern to indicate
#   all files.
#
# - initialdir: initial directory.  preserved by dialog instance.
#
# - initialfile: initial file (ignored by the open dialog).  preserved
#   by dialog instance.
#
# - parent: which window to place the dialog on top of
#
# - title: dialog title
#

from tkCommonDialog import Dialog

class _Dialog(Dialog):

    def _fixoptions(self):
        try:
            # make sure "filetypes" is a tuple
            self.options["filetypes"] = tuple(self.options["filetypes"])
        except KeyError:
            pass

    def _fixresult(self, widget, result):
        if result:
            # keep directory and filename until next time
            import os
            path, file = os.path.split(result)
            self.options["initialdir"] = path
            self.options["initialfile"] = file
        self.filename = result # compatibility
        return result


#
# file dialogs

class Open(_Dialog):
    "Ask for a filename to open"

    command = "tk_getOpenFile"

class SaveAs(_Dialog):
    "Ask for a filename to save as"

    command = "tk_getSaveFile"


#
# convenience stuff

def askopenfilename(**options):
    "Ask for a filename to open"

    return apply(Open, (), options).show()

def asksaveasfilename(**options):
    "Ask for a filename to save as"

    return apply(SaveAs, (), options).show()

# FIXME: are the following two perhaps a bit too convenient?

def askopenfile(mode = "r", **options):
    "Ask for a filename to open, and returned the opened file"

    filename = apply(Open, (), options).show()
    if filename:
        return open(filename, mode)
    return None

def asksaveasfile(mode = "w", **options):
    "Ask for a filename to save as, and returned the opened file"

    filename = apply(SaveAs, (), options).show()
    if filename:
        return open(filename, mode)
    return None


# --------------------------------------------------------------------
# test stuff

if __name__ == "__main__":

    print "open", askopenfilename(filetypes=[("all filez", "*")])
    print "saveas", asksaveasfilename()
