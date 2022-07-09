{

PRT test 222: Missing ',' between first and second field idents in variant

}

program iso7185prt0222;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 one two: ();
                 three: ()
       end;

begin

   a.b := 1

end.
