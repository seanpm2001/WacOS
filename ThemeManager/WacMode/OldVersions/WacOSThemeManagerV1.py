# Start of script
def systemSoftwareTheme():
  print("Unavailable")
def rainbowGradientTheme():
  print("Unavailable")
def aquaTheme():
  print("Unavailable")
def metalTheme():
  print("Unavailable")
# Program start:
print("WacOS Wacintosh theme manager")
chaTheme = str(input("You are currently using the " + str(curTheme) + " theme. Do you want to switch to a new theme? (Y/N)"))
chaTheme = chaTheme.upper()
if (chaTheme != "Y" or "N"):
  raise typeError("Please enter either Y or N")
elif (chaTheme == "Y"):
  print("Select a new theme")
  print("| System software theme [ID: 1]")
  print("Make your computer look like a Macintosh from the 1980s, early 1990s\n")
  print("| Rainbow gradients [ID: 2]")
  print("Make your computer look like a classic Macintosh from System 7 to System 9.3")
  print("| Aqua theme [ID: 3]")
  print("Make your computer look like a glossy Mac OS X machine\n")
  print("| Metal theme [ID: 4]")
  print("Make your computer look more metallic\n")
  print("| Other [ID: 5]")
  print("No description available")
  themeChoiceINT = int(input("Enter an ID to continue"))
  if not type(themeChoiceINT) is int:
    raise TypeError("Only integers are allowed")
  elif (themeChoiceINT > 0 and themeChoiceINT < 6):
    print("Unable to change themes right now")
  elif (themeChoiceINT < 0 or themeChoiceINT > 6):
    print("Number is too high")
  # This is a bit confusing, and needs to be debugged
exitCon1 = str(input("Press [ENTER] key to quit"))
print("Goodbye.")
# End of script
