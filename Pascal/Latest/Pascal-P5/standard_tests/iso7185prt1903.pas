{

PRT test 1903: Goto/label issues

    Goto nested block.
    ISO 7185 6.8.1

}

program iso7185prt1903(output);

label 1;

var i: integer;

begin

   for i := 1 to 10 do
      if i < 0 then goto 1;

   for i := 1 to 10 do begin

      1: writeln(i)

   end;

end.
