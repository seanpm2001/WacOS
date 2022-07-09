{

PRT test 1845: Goto/label issues. Label defined in outter block than use.

}

program iso7185prt1845(output);

label 1;

procedure a;

var i: integer;

begin

   goto 1;
   for i := 1 to 10 do begin

      1: writeln(i)

   end

end;

begin

   a

end.