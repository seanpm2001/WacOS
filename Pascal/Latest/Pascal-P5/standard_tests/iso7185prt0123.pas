{

PRT test 123: Missing ";" between statements on case statement

}

program iso7185prt0123;

var x, a: integer;

begin

   case x of

      1: a := 1
      2: a := 2

   end

end.
