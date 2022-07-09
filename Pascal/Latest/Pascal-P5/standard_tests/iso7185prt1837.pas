{

PRT test 1837: Goto/label issues. Label used but not defined.

}

program iso7185prt1837(output);

var i: integer;

begin

   goto 1;
   for i := 1 to 10 do begin

      1: writeln(i)

   end

end.