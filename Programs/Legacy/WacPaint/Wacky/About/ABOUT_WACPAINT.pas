{ Start of script }
program about_WacPaint(output);
begin
  { logoDisplay() }
  write('About WacPaint')
  write('Running in: Wacky mode')
  write('Version: 0.01 (Alpha) - 2021 Friday September 24th\nDescription unavailable')
  authorsButton();
  licenseButton();
  continueButton();
  break
end.
function logoDisplay;
begin
  { image(///Programs/Legacy/WacPaint/Classic/About/ICON.bmp) }
  break
end.
function authorsButton();
begin
  // The authors button for the about dialog box
  // The button should be placed to the left of the license button
  write('[Authors]')
  continue;
    write('Authors')
    write('@seanpm2001 / @WacOS-dev')
    write('No other authors to list at the moment')
    break
  end.
end.
function licenseButton();
  // The license button for the about dialog box
  // The button should be centered in the middle of the bottom of the dialog box
  write'[License]')
  continue;
    write('GNU General Public License V3');
    write('<one line to give the program"s name and a brief idea of what it does.>');
    write('Copyleft (🄯) 2021 @seanpm2001 / @WacOS-dev');
    write(' ');
    write('This program is free software: you can redistribute it and/or modify');
    write('it under the terms of the GNU General Public License as published by');
    write('the Free Software Foundation, either version 3 of the License, or');
    write('(at your option) any later version.');
    write(' ');
    write('This program is distributed in the hope that it will be useful,');
    write('but WITHOUT ANY WARRANTY; without even the implied warranty of');
    write('MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the');
    write('GNU General Public License for more details.');
    write(' ');
    write('You should have received a copy of the GNU General Public License');
    write('along with this program.  If not, see <https://www.gnu.org/licenses/>.');
    write(' ');
    scanf('Continue');
    break;
  end.
end.
function continueButton();
  // The continue button for the about dialog box
  // The button should be centered at the far right of the bottom of the dialog box
  write('[Continue');
  break;
end.
// File version: 1 (Friday, 2021 September 24th at 6:11 pm)
// File type: Pascal source file (*.pas)
// Line count (including blank lines and compiler line): 66
// End of script
