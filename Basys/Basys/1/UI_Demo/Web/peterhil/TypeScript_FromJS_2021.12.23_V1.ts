// Start of script
/* JavaScript data for Apple System 1 by @Peterhil */
/* Being represented as TypeScript */
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
* File type: TypeScript source file (*.ts *.tsx)
* File version: 1 (2021, Thursday, December 23rd at 7:57 pm)
* Line count (including blank lines and compiler line): 27
*/
// End of script
