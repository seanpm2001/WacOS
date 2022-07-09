{

PRT test 217: Missing ')' on field list for variant

}

program iso7185prt0217;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 one, two: (;
                 three: ()
       end;

begin

   a.b := 1

end.
