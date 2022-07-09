{

PRT test 1832: Goto/label issues. Goto nested block

    ISO 7185 6.8.1

}

program iso7185prt1832(output);

label 1;

var i: integer;

begin

   goto 1;
   for i := 1 to 10 do begin

      1: writeln(i)

   end

end.