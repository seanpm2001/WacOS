{

PRT test 1834: Goto/label issues. Unreferenced label.

}

program iso7185prt1834(output);

label 1;

var i: integer;

begin

   for i := 1 to 10 do begin

      1: writeln(i)

   end

end.