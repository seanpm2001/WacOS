// Start of script
/* JavaScript data for Apple System 1 by @Peterhil */
var startList = function() {
    if (document.all && document.getElementById) {
        var navRoot = document.getElementById("menubar");
        for (i=0; i < navRoot.childNodes.length; i++) {
node = navRoot.childNodes[i];
if (node.nodeName == "DIV" || node.nodeName == "LI") {
    node.onmouseover = function() {
        this.className += " over";
    }
    node.onmouseout = function() {
        this.className = this.className.replace(" over", "");
    }
}
        }
    }
}
window.onload = startList;
/* File info
* File type: JavaScript source file (*.js)
* File version: 2 (2021, Wednesday, December 22nd at 3:06 pm)
* Line count (including blank lines and compiler line): 26
*/
// End of script
