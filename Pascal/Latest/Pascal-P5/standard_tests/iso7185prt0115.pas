{

PRT test 115: Missing "case" on case statement

}

program iso7185prt0115;

var x, a: integer;

begin

   x of 

      1: a := 1;
      2: a := 2

   end

end.