% Start of script
% The main script for the Kiri voice assistant
% Read from primary libraries
read from "///kiri/LIBraries/voice-commands/1/", nl.
read from "///kiri/LIBraries/ENGINE.pl", nl.
% Main
?- write('Hello, what can I help you with?'), nl.
wait for response, nl.
if no response in 10, nl.
  exit, nl.
  break, nl.
else, nl.
break, nl.
?-
% File info
% File type: Prolog source file (*.pl) Not to be confused with Perl/Raku
% File version: 1 (Monday, 2021 September 27th at 6:03 pm)
% Line count (including blank lines and compiler line): 21

% End of script
