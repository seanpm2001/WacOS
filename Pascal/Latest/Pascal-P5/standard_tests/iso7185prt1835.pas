{

PRT test 1835: Goto/label issues. No label to go to.

}

program iso7185prt1835(output);

label 1;

var i: integer;

begin

   goto 1;
   for i := 1 to 10 do begin

      writeln(i)

   end

end.