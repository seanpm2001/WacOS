{

PRT test 156: Missing ',' between case constants

}

program iso7185prt0156;

var x, a: integer;

begin

   case x of

      1 2: a := 1;
      3: a := 2

   end

end.
