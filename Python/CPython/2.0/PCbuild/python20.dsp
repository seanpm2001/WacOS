# Microsoft Developer Studio Project File - Name="python20" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102
# TARGTYPE "Win32 (ALPHA) Dynamic-Link Library" 0x0602

CFG=python20 - Win32 Alpha Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "python20.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "python20.mak" CFG="python20 - Win32 Alpha Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "python20 - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "python20 - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "python20 - Win32 Alpha Debug" (based on "Win32 (ALPHA) Dynamic-Link Library")
!MESSAGE "python20 - Win32 Alpha Release" (based on "Win32 (ALPHA) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName "python20"
# PROP Scc_LocalPath ".."

!IF  "$(CFG)" == "python20 - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "."
# PROP Intermediate_Dir "x86-temp-release\python20"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
F90=df.exe
CPP=cl.exe
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /Zi /O2 /I "..\Include" /I "..\PC" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "USE_DL_EXPORT" /YX /FD /Zm200 /c
MTL=midl.exe
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
RSC=rc.exe
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /i "..\Include" /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /machine:I386
# ADD LINK32 largeint.lib kernel32.lib user32.lib advapi32.lib shell32.lib /nologo /base:"0x1e100000" /subsystem:windows /dll /debug /machine:I386 /nodefaultlib:"libc"
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "."
# PROP Intermediate_Dir "x86-temp-debug\python20"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
F90=df.exe
CPP=cl.exe
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /Zi /Od /I "..\Include" /I "..\PC" /D "_DEBUG" /D "USE_DL_EXPORT" /D "WIN32" /D "_WINDOWS" /YX /FD /Zm200 /c
MTL=midl.exe
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
RSC=rc.exe
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /i "..\Include" /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 largeint.lib kernel32.lib user32.lib advapi32.lib shell32.lib /nologo /base:"0x1e100000" /subsystem:windows /dll /debug /machine:I386 /nodefaultlib:"libc" /out:"./python20_d.dll" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "."
# PROP Intermediate_Dir "alpha-temp-debug\python20"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
CPP=cl.exe
# ADD BASE CPP /nologo /Gt0 /W3 /GX /Zi /Od /I "..\Include" /I "..\PC" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "USE_DL_EXPORT" /YX /FD /MTd /c
# ADD CPP /nologo /Gt0 /W3 /GX /Zi /Od /I "..\Include" /I "..\PC" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "USE_DL_EXPORT" /YX /FD /MDd /c
MTL=midl.exe
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
RSC=rc.exe
# ADD BASE RSC /l 0x409 /i "..\Include" /d "_DEBUG"
# ADD RSC /l 0x409 /i "..\Include" /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 wsock32.lib largeint.lib winmm.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib /nologo /base:"0x1e100000" /subsystem:windows /dll /debug /machine:ALPHA /nodefaultlib:"libc" /out:"./python20_d.dll" /pdbtype:sept
# SUBTRACT BASE LINK32 /pdb:none
# ADD LINK32 largeint.lib kernel32.lib user32.lib advapi32.lib /nologo /base:"0x1e100000" /subsystem:windows /dll /debug /machine:ALPHA /nodefaultlib:"libc" /out:"./python20_d.dll" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "."
# PROP Intermediate_Dir "alpha-temp-release\python20"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
CPP=cl.exe
# ADD BASE CPP /nologo /MT /Gt0 /W3 /GX /Zi /O2 /I "..\Include" /I "..\PC" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "USE_DL_EXPORT" /YX /FD /c
# ADD CPP /nologo /MD /Gt0 /W3 /GX /O2 /I "..\Include" /I "..\PC" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "USE_DL_EXPORT" /YX /FD /c
MTL=midl.exe
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
RSC=rc.exe
# ADD BASE RSC /l 0x409 /i "..\Include" /d "NDEBUG"
# ADD RSC /l 0x409 /i "..\Include" /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 wsock32.lib largeint.lib winmm.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib /nologo /base:"0x1e100000" /subsystem:windows /dll /debug /machine:ALPHA /nodefaultlib:"libc"
# SUBTRACT BASE LINK32 /pdb:none
# ADD LINK32 largeint.lib kernel32.lib user32.lib advapi32.lib /nologo /base:"0x1e100000" /subsystem:windows /dll /debug /machine:ALPHA /nodefaultlib:"libc"
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "python20 - Win32 Release"
# Name "python20 - Win32 Debug"
# Name "python20 - Win32 Alpha Debug"
# Name "python20 - Win32 Alpha Release"
# Begin Source File

SOURCE=..\Modules\_codecsmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\_localemodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\abstract.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\acceler.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\arraymodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\audioop.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\binascii.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\bitset.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\bltinmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\bufferobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\ceval.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\classobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\cmathmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\cobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\codecs.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\compile.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\complexobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\PC\config.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\cPickle.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\cStringIO.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\dictobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\PC\dl_nt.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\dynload_win.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\errnomodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\errors.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\exceptions.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\fileobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\floatobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\fpectlmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\fpetestmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\frameobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\frozen.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\funcobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\gcmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\getargs.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\getbuildinfo.c

!IF  "$(CFG)" == "python20 - Win32 Release"

# ADD CPP /D BUILD=18

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

# ADD CPP /D BUILD=18

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\getcompiler.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\getcopyright.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\getmtime.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\getopt.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\PC\getpathp.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\getplatform.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\getversion.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\graminit.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\grammar1.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\imageop.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\import.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\PC\import_nt.c

!IF  "$(CFG)" == "python20 - Win32 Release"

# ADD CPP /I "..\Python"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

# ADD CPP /I "..\Python"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\importdl.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\intobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\listnode.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\listobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\longobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\main.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\marshal.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\mathmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\md5c.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\md5module.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\metagrammar.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\methodobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\modsupport.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\moduleobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\PC\msvcrtmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\myreadline.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\mystrtoul.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\newmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\node.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\object.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\operator.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\parser.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\parsetok.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\pcremodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\posixmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\pyfpe.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\pypcre.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\pystate.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\PC\python_nt.rc
# End Source File
# Begin Source File

SOURCE=..\Python\pythonrun.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\rangeobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\regexmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\regexpr.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\rgbimgmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\rotormodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\shamodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\signalmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\sliceobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\soundex.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\stringobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\stropmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\structmember.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\structmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\sysmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\thread.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\threadmodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\timemodule.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Parser\tokenizer.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Python\traceback.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\tupleobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\typeobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\unicodectype.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Objects\unicodeobject.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\Modules\yuvconvert.c

!IF  "$(CFG)" == "python20 - Win32 Release"

!ELSEIF  "$(CFG)" == "python20 - Win32 Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Debug"

!ELSEIF  "$(CFG)" == "python20 - Win32 Alpha Release"

!ENDIF 

# End Source File
# End Target
# End Project
