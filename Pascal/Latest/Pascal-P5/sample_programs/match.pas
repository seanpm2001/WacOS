{$l-}
{******************************************************************************

             match snatch game program
             adapted from 'A PRIMER ON PASCAL'
             by conway, gries and zimmerman.

******************************************************************************}

program matchsnatch(input, output);

type mover = (user, prog);

var matches,               { number of matches left }
    movelimit: integer;    { limit on each move }
    whosemove: packed array[1..3] of char;
                           { 'you' = prog, 'me' = user }
    nextmove: mover;
    move,                  { current move }
    i: integer;

begin { matchsnatch }

   { initalization }
   { get game parameters }
   writeln;
   writeln('welcome to match-snatch');
   writeln;
   matches := 0;
   while matches < 1 do begin

      writeln('how many matches to start ?');
      readln(matches);
      if matches < 1 then writeln('must be at least 1');
     
   end;
   movelimit := 0;
   while (movelimit < 2) or (movelimit > matches) do begin

      writeln('how many in 1 move ?');
      readln(movelimit);
      if movelimit < 1 then writeln('must be at least 1');
      if movelimit > matches then writeln('not that many matches')
      
   end;
   { determine who moves first }
   whosemove := '   ';
   while (whosemove <> 'me ') and (whosemove <> 'you') do begin { first loop }

      writeln('who moves first -- you or me ?');
      whosemove := '   ';
      i := 1;
      while not eoln and (i <= 3) do begin
           
         read(whosemove[i]);
         i := i + 1

      end;
      readln
     
   end; { first loop }
   writeln;
   if whosemove = 'you' then nextmove := prog else nextmove := user;
   { alternate moves -- user and program }
   while matches > 0 do begin { move loop }

      if nextmove = user then begin { user's move }

         move := 0;
         while (move < 1) or (move > matches) or (move > movelimit) do begin

            writeln('how many do you take ?');
            readln(move);
            if move < 1 then writeln('must take at least one');
            if move > movelimit then writeln('that''s more than we agreed on');
            if move > matches then writeln('there aren''t that many');

         end;
         matches := matches - move;
         writeln('there are ', matches, ' left');
         nextmove := prog
          
      end { user's move } else begin { program's move }

           move := (matches - 1) mod (movelimit + 1);
           if move = 0 then move := 1;
           writeln('I take ', move, ' matches');
           matches := matches - move;
           writeln('there are ', matches, ' left');
           nextmove := user
          

      end { program's move }

   end; { move loop }
   { report outcome }
   { player who made last move lost }
   if nextmove = user then writeln('you won, nice going.')
                      else writeln('I won, tough luck.')

end.
