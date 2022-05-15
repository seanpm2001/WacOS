{ Start of script }
{ The main web suite script for OpenCyberdog }
{ Note: I am not too experienced with Pascal. The program may not function properly. For now, this is just pseudocode }
{ License: GPL3 (GNU General Public License V3.0) }
program openCyberdog_SuiteMain();
begin
	{ The base project "Cyberdog" had 4 main features:
	An email client
	A news feed
	An HTML/OpenDoc browser
	An address book
	I want to imitate the look and feel of the browser, but keep resources very low
	}
	write 'OpenCyberdog is starting...'
	// I hope to have these show up as icons as they load, similar to the boot process of early versions of Classic MacOS
	openCyberdog_emailClient()
	openCyberdog_newsClient()
	openCyberdog_HTMLBrowser()
	openCyberdog_openDocBrowser()
	openCyberdog_AddressBook()
	write 'Loading components...'
	openDocSupport()
	html2Support()
	css1Support()
	gifViewer()
	iframeSupport()
	cookieSupport()
	javaSupport()
	toolBar()
	write 'Checking version...'
	emulationMode()
	aboutBox()
	write 'Processing...'
	write 'OpenCyberdog is still in development, is not functional yet, and cannot be used. In the meantime, please use another web browser'
	break
end.
program openCyberdog_emailClient();
begin
	{ OpenCyberdog email client }
	write 'The OpenCyberdog email client is coming soon'
	break
end.
program openCyberdog_newsClient();
begin
	{ OpenCyberdog news feed }
	write 'The OpenCyberdog news feed is coming soon'
	break
end.
program openCyberdog_HTMLBrowser();
begin
	{ OpenCyberdog HTML browser }
	write 'The OpenCyberdog HTML web browser is coming soon'
	break
end.
program openCyberdog_openDocBrowser();
begin
	{ OpenCyberdog OpenDoc browser }
	write 'The OpenCyberdog OpenDoc web browser is coming soon'
	break
end.
program openCyberdog_AddressBook();
begin
	{ OpenCyberdog Address book }
	write 'The OpenCyberdog address book feature is coming soon'
	break
end.
program gifViewer();
begin
	{ GIF support for OpenCyberdog }
	write 'Support for GIF files in OpenCyberdog is coming soon'
	break
end.
program iframeSupport();
begin
	{ iFrame support for OpenCyberdog, only in version 2.0 }
	write '<iframe> support in OpenCyberdog is coming soon'
	break
end.
program cookieSupport();
begin
	{ HTTP Cookie support for OpenCyberdog, only in version 2.0 }
	write 'HTTP cookie support in OpenCyberdog is coming soon'
	break
end.
program javaSupport();
begin
	{ Java support for OpenCyberdog, only in version 2.0 }
	write 'Java support in OpenCyberdog is coming soon'
	break
end.
program openDocSupport();
begin
	{ OpenDoc support for OpenCyberdog }
	write 'OpenDoc support in OpenCyberdog is coming soon'
	break
end.
program html2Support();
begin
	{ HTML 2.0 support for OpenCyberdog }
	write 'HTML 2.0 support in OpenCyberdog is coming soon'
	break
end.
program css1Support();
begin
	{ CSS 1.0 support for OpenCyberdog }
	write 'CSS 1.0 support in OpenCyberdog is coming soon'
	break
end.
program emulationMode();
begin
	{ Emulate various versions of Cyberdog with OpenCyberdog }
	var beta1996(false) // Cyberdog beta from 1996, February 16th
	{ New features
	Unknown
	}
	var cdog1_0(false) // Cyberdog version 1.0 from 1996, May 13th
	{ New features
	Unknown
	}
	var cdog1_2(false) // Cyberdog version 1.2 from 1996, December 4th
	{ New features
	Unknown
	}
	var cdog2_0alpha(false) // Cyberdog version 2.0 alpha from 1996, December 21st
	{ New features
	Unknown
	}
	var cdog2_0(true) // Cyberdog version 2.0, released with Mac OS 8 on 1997, July 26th
	{ Features:
	Cookies
	iFrames
	GIF support
	Java support (NOT JavaScript)
	}
	break
end.
program aboutBox();
begin
	{ OpenCyberdog about box, gives info on the browser }
	write 'OpenCyberdog Alpha 1.0 - 2022, Sunday, May 15th at 3:40 pm PST'
	write 'This build is not yet functional'
	write 'Written in Pascal, designed for classic WacOS'
	write 'Also:\nOn the Internet, nobody knows you"re a dog'
	break
end.
program toolBar();
begin
	{ The program toolbar for OpenCyberdog }
	var tbar_file(true) // I don't know how to work the GUI, so the boolean statement just means that it exists and is enabled
	chr 'File'
	var tbar_edit(true)
	chr 'Edit'
	var tbar_view(true)
	chr 'View'
	var tbar_help(true)
	chr 'Help'
	tbar ['tbar_file', 'tbar_edit', 'tbar_view', 'tbar_help'] // The order of the title bar
	break
end.
{ On the Internet, nobody knows you're a dog }
{ File info }
{ File type: Pascal source file (*.pas) }
{ File version: 1 (2022, Sunday, May 15th at 3:40 pm PST) }
{ Line count (including blank lines and compiler line): 166 }
{ End of script }
