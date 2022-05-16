-- Start of script
-- "Hello" dialog for Kiri
-- Written in AppleScript
-- Licensed under the GNU General Public License V3.0 (GPL3)
#include <inputK.pro>
if inputK == "Hello":
  rand = int.randint(1, 6)
  if rand == 1:
	  say "Hello to you to"
  else if rand == 2:
    say "Hi"
  else if rand == 3:
    say "Hello"
  else if rand == 4:
    say "Yo"
  else if rand == 5:
    say "What's up"
  else if rand == 6:
    say "Hello World!"
else:
	return 0
-- File info
-- File type: AppleScript source file (*.applescript *.scpt *.scptd)
-- File version: 1 (2022, Monday, May 16th at 12:51 pm PST)
-- Line count (including blank lines and compiler line): 27
-- End of script
