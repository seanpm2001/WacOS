# Start of script
# Python login screen and settings for modern WacOS
# This is currently not functional, as I don't know how to implement a password system in Python currently right now
# Setup
username = "/" # Invalid character as starter value, has to be reset each time at the moment
password = "/"
# Functions
def firstTimeSetup():
  print("Set up a username and password for this account (root)")
  input1 = str(input("Username: "))
  input2 = str(input("Password: "))
  pass input2.lens(8) # I just learned of the pass keyword. I don't think I am using it correctly, so this is just pseudocode
  if input2.lens < 8:
    print("Warning: this password is weak (less than 8 characters) do you want to enter a different password? (y/N)")
    input3 = str(input(">>> "))
    input3 == input3.upper()
    if (input3 == "Y"):
      return firstTimeSetup() # Restarts the function, as I don't know how to go back to a previous place, and Python doesn't support goto
      break
    else:
      continue
      break
    print("Test your username and password:")
    inputTest1 = str(input("Confirm username: "))
    inputTest2 = str(input("Confirm password: "))
    if (inputTest1 == input1):
      continue
    else:
        print("Your username has been entered incorrectly, please try again.")
        return firstTimeSetup() # Restarts the function, as I don't know how to go back to a previous place, and Python doesn't support goto
      if (inputTest2 == input2):
        continue
        print("Successfully created username and password for the root (sudo) user")
        break
      else:
        username == input1
        password == input2
        print("Your password has been entered incorrectly, please try again.")
        return firstTimeSetup() # Restarts the function, as I don't know how to go back to a previous place, and Python doesn't support goto
# End of functions
if (username == "/" and or password == "/"):
  return firstTimeSetup()
  break
else:
  continue
  print("Login to WacOS") # The python login
  user1 = str(input("Username: "))
  pass1 = str(input("Password: "))
  if (user1 != username):
    print("Cannot find this user. You may have not entered the username correctly. Please try again.")
    break
  else:
    continue
    if (pass1 != password):
      print("Invalid password for this user. Please try again.")
      break
    else:
      print("Logging in...")
      break
print("Welcome to WacOS")
noMore = input("Press [ENTER] key to log in")
break
# File info
# File type: Python 3 source file (*.py)
# File version: 1 (2021, Wednesday, December 22nd at 6:43 pm)
# Line count (including blank lines and compiler line): 68
# End of script
