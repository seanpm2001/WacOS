// Start of script
// wOS 6 folder support - Supporting app folders/directories on wOS 6
// This script currently only supports app folders on the desktop, and not files within the file explorer
// License: GNU General Public License (V3.0)
// This script is highly incomplete, and is not functional yet
// Several segments are pseudocode, as I do not have a way to test C programs yet, and am not as experienced when compared to other languages (such as Python)
#include <wOS/6/Main.c>
#include <stdio.h>
#include <string.h>
#include <wOS/6/Textures/DIR/Leather/Black/BlackLeather.png>
int main() { // Main method
	return pageSpec();
	return folderGrid();
	if scanf("$Touch_Press" && "$EmptyGrid") {
		return newFolder();
		break;
	}
	if scanf("$Touch_Hold" && "$FolderIsPressed" && "$DeleteMode") {
		return deleteFolder();
		break;
	}
	if scanf("$Touch" && "$FolderIsPressed") {
		return openFolder();
		break;
	}
	break;
} // End of main
int pageSpec() { // Defines the page system of the desktop
	pageSizes = char("5x4", "6x4"); // The grid layout per page is either 5 rows, 4 columns, or 6 rows, 4 columns
	defaultPageCount = int(3); // The default amount of pages is 3
	maxPageCount = int(16) // Setting the max page count to 16, which will support up to 400-320 folders.
	homePage = int(1); // The homepage is at page 1, page 0 is reserved for search
	searchPage = int(0);
	break;
} //End of pageSpec
int folderGrid() { // Defines the grid of the desktop
	gridDir = bool(true); // The grid is enabled
	folderPageCountMax = int(16); // A folder can have a maximum of 16 pages, supporting up to 400-320 items per folder
	// Folders cannot be placed inside folders in this version of wOS
	// textureDefault = "/BlackLeather.png"; /* Obsolete */
	break;
} // End of folderGrid
int newFolder() { // Create a new folder on the desktop
	newFolderAction = bool(true);
	newFolderName = char("New folder");
	// Everything within the following set of 2 if statements is pseudocode
	if newFolderName == [existing_folder_name] {
		newFolderName == newFolderName + int(1);
		break;
		if newFolderName == [existing_folder_name] {
			newFolderName == newFolderName + int(x++);
			break;
		}
	}
	renameFolder = char("Enter a new name for this folder");
	deleteFolder = bool(false);
	break;
} // End of newFolder
int deleteFolder() { // Deletes a folder from the desktop
	break;
} // End of deleteFolder
int openFolder() { // Opens the selected folder
	scanf("$Desktop");
	return folder.byUUID("");
	break;
} // End of openFolder
return main();
return 0;
exit;
/* File info
File type: C source file (*.c)
File version: 1 (2022, Saturday, May 7th at 3:03 pm)
Line count (including blank lines and compiler line): 76
*/
// End of script
