rem
rem Fix line ending on bash scripts
rem
echo.
echo Fixing the line endings on Unix files
echo.
flip -u bin\build
flip -u bin\clean
flip -u bin\compile
flip -u bin\configure
flip -u bin\cpcom
flip -u bin\cpcoms
flip -u bin\cpint
flip -u bin\cpints
flip -u bin\diffnole
flip -u bin\doseol
flip -u bin\fixeol
flip -u bin\make_flip
flip -u bin\p5
flip -u bin\regress
flip -u bin\run
flip -u bin\testpascals
flip -u bin\testprog
flip -u bin\unixeol
flip -u bin\zipp5
flip -u gpc/compile
flip -u gpc/cpcom
flip -u gpc/cpint
flip -u gpc/p5
flip -u gpc/run
flip -u ip_pascal/compile
flip -u ip_pascal/cpcom
flip -u ip_pascal/cpint
flip -u ip_pascal/p5
flip -u ip_pascal/run
flip -u make_flip
echo.
echo Fixing the line endings on DOS/Windows files
echo.
flip -m bin\build.bat
flip -m bin\clean.bat
flip -m bin\compile.bat
flip -m bin\configure.bat
flip -m bin\cpcom.bat
flip -m bin\cpcoms.bat
flip -m bin\cpint.bat
flip -m bin\cpints.bat
flip -m bin\diffnole.bat
flip -m bin\doseol.bat
rem flip -m     bin\fixeol.bat
flip -m bin\make_flip.bat
flip -m bin\p5.bat
flip -m bin\prtprt.bat
flip -m bin\regress.bat
flip -m bin\run.bat
flip -m bin\testpascals.bat
flip -m bin\testprog.bat
flip -m bin\unixeol.bat
flip -m bin\zipp5.bat
flip -m gpc/compile.bat
flip -m gpc/cpcom.bat
flip -m gpc/cpint.bat
flip -m gpc/p5.bat
flip -m gpc/run.bat
flip -m ip_pascal/compile.bat
flip -m ip_pascal/cpcom.bat
flip -m ip_pascal/cpint.bat
flip -m ip_pascal/p5.bat
flip -m ip_pascal/run.bat
