{

PRT test 1836: Goto/label issues. Label defined, but never used.

}

program iso7185prt1836(output);

label 1;

var i: integer;

begin

   for i := 1 to 10 do begin

      writeln(i)

   end

end.