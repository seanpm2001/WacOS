# Start of script

# Variables
x = int(0)
y = int(1)
z = int(2)
testX = int(1) # Test variable for switching between variants of the system
''' Legend
0 = classic Mode
1 = Wac OS X mode
2 = WOAHS X mode
3 = Modern WscOS mode
'''
# Arrays
cloudServiceNamesMain = ["pCloud\n", "DeGoo\n", "ProtonDrive\n"]
# Functions
def pCloud():
  print ("pCloud is an online file storage service hosted in Switzerland that focuses on user privacy")
  # Add methods here to support pCloud integration
  break
def deGoo():
  print ("deGoo is an online file storage service hosted in Sweden that focuses on user privacy and gives users 100 gigabytes of free space by default.")
  # Add methods here to support deGoo integration
  break
def protonDrive():
  print ("protonDrive is an online file storage service hosted in Switzerland by the creators of ProtonMail that focuses on user privacy and security. It was first released in 2021.")
  # Add methods here to support protonDrive integration
  break
# Classes (WacOS system modes)
class classicMode():
  print ("Sorry!\nCloud support is not supported in Classic WacOS mode. Please switch to WacOS X or newer to use cloud functionality")
  break
class xMode():
  print ("Cloud services for WacOS X")
  print ("Note: iCloud is not supported due to a lack of permission from Apple, and general incompatibilities")
  print ("Choose a cloud service: " + str(cloudServiceNamesMain))
  cloudSInput = input("Your choice: ")
  cloudSInput == cloudSInput.upper()
  if (cloudSInput == "PCLOUD"):
    print ("Sign in to pCloud")
    return pCloud()
    break
  elif (cloudSInput == "DEGOO"):
    print ("Sign in to DeGoo")
    return degoo()
    break
  elif (cloudSInput == "ProtonDrive"):
    print ("Sign in to ProtonDrive")
    return protonDrive()
    break
  else:
    print ("Invalid option! Please type the name of the service you want to choose from the supported options. Make sure everything is spelled correctly.")
    return restartXmode()
  break
class restartXMode():
  return xMode()
  break
class woahs_x_Mode():
  return xMode() # Temporary, until the GUI can be a difference between the options
  break
class wacOS_modern_Mode():
  return xMode() # Temporary, until the GUI can be a difference between the options
  break
class main():
  if (textX == 0):
    return classicMode()
    break
  elif (textX == 1):
    return xMode()
    break
  elif (textX == 2):
    return woahs_x_mode()
    break
  elif (testX == 3):
    return wacOS_modern_Mode()
    break
  else:
    return 0
  break
# Backup main method
return main()
#end

""" File info
File type: Python 3.9 source file (*.py)
File version: 1 (Thursday, 2021 September 30th at 3:32 pm)
Line count (including blank lines and compiler line): 91
"""

# End of script
