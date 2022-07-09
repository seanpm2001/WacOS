{

PRT test 1833: Goto/label issues. Intraprocedure goto nested block.

    ISO 7185 6.8.1

}

program iso7185prt1833(output);

label 1;

var i: integer;

procedure abort;

begin

   goto 1

end;

begin

   abort;
   for i := 1 to 10 do begin

      1: writeln(i)

   end

end.