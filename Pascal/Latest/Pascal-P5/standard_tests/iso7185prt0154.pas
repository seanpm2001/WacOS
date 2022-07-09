{

PRT test 154: Missing 1st constant on case statement list

}

program iso7185prt0154;

var x, a: integer;

begin

   case x of

      ,1: a := 1;
      2: a := 2

   end

end.
