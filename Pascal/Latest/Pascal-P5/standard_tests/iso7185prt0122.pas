{

PRT test 122: Missing ":" before statement on case statement

}

program iso7185prt0122;

var x, a: integer;

begin

   case x of

      1 a := 1;
      2: a := 2

   end

end.
