{

PRT test 121: Missing 2nd constant on case statement list

}

program iso7185prt0121;

var x, a: integer;

begin

   case x of

      1,: a := 1;
      2: a := 2

   end

end.
