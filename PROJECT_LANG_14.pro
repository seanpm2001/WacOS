mother_child(trude, sally).
 
father_child(tom, sally).
father_child(tom, erica).
father_child(mike, tom).
 
sibling(X, Y)      :- parent_child(Z, X), parent_child(Z, Y).
 
parent_child(X, Y) :- father_child(X, Y).
parent_child(X, Y) :- mother_child(X, Y).

% Some sample Prolog program so that the Linguist detects it as Prolog, rather than Perl or Raku.
write "I decided to make the fourteenth project language file for this project (WacOS) to be Prolog, as prolog is good for the advanced algorithms involved in voice assistants, and will be used in a future Siri alternative on this project".

% File info
% File type: Prolog source file (*.pl) (may be mis-identified as Perl or Raku source code)
% File version: 1 (Monday, 2021 Saturday September 25th at 7:24 pm)
% Line count (including blank lines and compiler line): 19
