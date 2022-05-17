#!/usr/bin/osascript
-- Start of script

-- AppleScript API support file
-- Adding support for various versions of AppleScript to WacOS through the WacOS AppleScript API
-- This file is licensed under the GNU General Public License V3. If you did not receive a copy of the license with WacOS, you can obtain it here: https://www.gnu.org/licenses/gpl-3.0.en.html
-- Note: I am not too experienced with AppleScript, and I don't have a device to actually test the program, so I don't know if it will function correctly. For now, I am just laying the foundation.

-- Source code notes
-- I am not using the newer AppleScript comment type "#" in this source code, as I want this script to be backwards compatible with the versions of AppleScript it is bootstrapping
-- I am not too experienced with AppleScript, and I don't have a device to actually test the program, so I don't know if it will function correctly. For now, I am just laying the foundation.

-- Start of program

set applescript_Version to 2.5
-- set applescript_version to 2.0
-- set applescript_version to 1.0

applescript_extensions = [".applescript", ".scpt", ".scptd"]

on aboutTwoFiveBox(parameter)
	-- This function is currently incomplete
	display dialog "About WacOS:AppleScript\n\nAppleScript is running under version 2.5 on non-Apple software.\nThis file is licensed under the GNU General Public License V3. If you did not receive a copy of the license with WacOS, you can obtain it here: https://www.gnu.org/licenses/gpl-3.0.en.html"
	-- End of function

on aboutTwoZeroBox(parameter)
	-- This function is currently incomplete
	display dialog "About WacOS:AppleScript\n\nAppleScript is running under version 2.0 on non-Apple software.\nThis file is licensed under the GNU General Public License V3. If you did not receive a copy of the license with WacOS, you can obtain it here: https://www.gnu.org/licenses/gpl-3.0.en.html"
	-- End of function

on aboutOneZeroBox(parameter)
	-- This function is currently incomplete
	display dialog "About WacOS:AppleScript\n\nAppleScript is running under version 1.0 on non-Apple software.\nThis file is licensed under the GNU General Public License V3. If you did not receive a copy of the license with WacOS, you can obtain it here: https://www.gnu.org/licenses/gpl-3.0.en.html"
	-- End of function

on teller(parameter)
	-- Teller is a support library for the 'tell' keyword
	-- This function is currently incomplete
	tellK = ["tell"]
	tellK == "kill"
	-- More structured syntax coming soon
	-- Test
	tell application "Libreoffice"
		quit
		-- Other commands here
	end tell
	tell application "Firefox" to quit
	quit application "VLC-Media-Player"
	-- End of function
on commentator(parameter)
	-- Commentator is a support library for the 3 comment types in AppleScript
	-- This function is currently incomplete
	if appleScript_Verson == 2.5
		set commentTypes = ["--", "#", "(*", "*)"]
	if appleScript_Verson == 2.0
		set commentTypes = ["--", "#", "(*", "*)"]
	if appleScript_Verson == 1.0
		set commentTypes = ["--", "(*", "*)"]
	-- End of function
on main(init)
	-- Main method, calls everything, powers everything
	-- This function is currently incomplete
	return aboutTwoFiveBox() -- Dialog box for WAppleScript 2.5
	return aboutTwoZeroBox() -- Dialog box for WAppleScript 2.0
	return aboutOneZeroBox() -- Dialog box for WAppleScript 1.0
	return teller() -- Teller is a support library for the 'tell' keyword
	return commentator() -- Commentator is a support library for the 3 comment types in AppleScript
	-- End of main

-- End of program

(* File info *)

(*
File version: 1 (2022, Tuesday, May 17th at 3:33 pm PST)
File type: AppleScript source file (*.applescript, *.scpt, *.scptd)
Line count (including blank lines and compiler line): 81
*)

-- End of script
