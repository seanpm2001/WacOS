{

PRT test 220: Standard field specification in variant

}

program iso7185prt0220;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 one, two: ();
                 three: ();
                 e, f: char
       end;

begin

   a.b := 1

end.
