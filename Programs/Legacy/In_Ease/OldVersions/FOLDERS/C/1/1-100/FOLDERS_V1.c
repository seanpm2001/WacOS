#include <stdio.h>
#include <string.h>
// Start of script
// In Ease - An open source alternative to At Ease
// This is the folder support script for in ease
// Note: I am inexperienced with the C language, so this is not functional yet, and serves as pseudocode
searcher default = false
inEase default = true
platforms = char["BaSYS 5", "BaSYS 6", "WacOS 7", "WacOS 8", "WacOS 9"];
/* Project specificiation
Type: Desktop manager
Is legacy: true
Based on: At Ease
Written in: C
License: GNU General Public License V3.0 (GPL3)
Part of the WacOS operating system project
*/
// This project is highly incomplete, and is not functional yet. It currently contains mainly pseudocode, as I lay the foundation
int workgroupsTest() {
	// A test method of workgroups (for developers)
	// Coming soon
	workgroupsKey == true; // The key to unlock the workgroups mode
	workgroups == true; // Set to true for testing purposes
	break;
}
int workgroupsMain() {
	// In Ease for workgroups
	// An extension to add functionality similar to At Ease for workgroups
	/* Notable changes in workgroups mode:
	Pink folder -> Removable media
	Green folder -> Virtual floppy drive
	Ability to log in with any server with In Ease installed
	This mode also enables client configuration, network access and restrictions on how the client's computer can be used
	*/
	// Coming soon
	workgroups == false;
	break;
}
int folderTabs() {
	// The folder tabs/folder definitions and headers
	// Coming soon
	return beigeFolder();
	return blueFolder();
	if workgroups == true; {
		return pinkFolder();
		return greenFolder();
		break;
	}
	break;
}
int beigeFolder() {
	// The In Ease beige "Folder" for general In Ease content
	// Coming soon
	beigeFolderTitle = char("In Ease\n");
	contents = ["Nil", "Nil"];
	break;
}
int blueFolder() {
	// The In Ease blue "Folder" for documents
	// Coming soon
	blueFolderTitle = char("$username1\a");
	contents = ["Nil", "Nil"];
	break;
}
int pinkFolder() {
	// The In Ease pink "Folder" for removable media that is above the level of a floppy diskette
	// Coming soon
	pinkFolderTitle = char("$MOUNT%A\a");
	// Removable media types
	usb1 = asm(00000001); // Assembly Bytecode for USB 1.0 devices
	usb2 = asm(00000010); // Assembly Bytecode for USB 2.0 devices
	usb3 = asm(00000011); // Assembly Bytecode for USB 3.0 devices
	usbC = asm(00000100); // Assembly Bytecode for USB C devices
	vga1 = asm(00000101); // Assembly Bytecode for VGA devices
	svga1 = asm(00000110); // Assembly Bytecode for SVGA devices
	ethernet1 = asm(00000111); // Assembly Bytecode for Ethernet devices
	otherMax asm(11111111); // Sample Bytecode for entry 255/256 devices
	contents = ["Nil", "Nil"];
	break;
}
int greenFolder() {
	// The In Ease green "folder" for virtual floppy drives
	// Coming soon
	greenFolderTitle = char("$MOUNT%C\a");
	contents = ["Nil", "Nil"];
	break;
}
int main() {
	// Main method, calls all other methods
	// Work in progress
	// return workgroupsTest(); // Remove the first comment parameter in this line to enable developer mode
	return folderTabs();
	if workgroupsKey = true; {
		return workgroupsMain();
		break;
	}
	return 0;
	break;
}
return main();
return 0;
break;
exit;
/* File info
* File version: 1 (2022, Tuesday, May 24th at 8:37 pm PST) - Prepared early before officially releasing
* File type: C Source file (*.c *.h)
* Line count (including blank lines and compiler line): 111
*/
// This script is incomplete, and needs lots of work
// End of script
