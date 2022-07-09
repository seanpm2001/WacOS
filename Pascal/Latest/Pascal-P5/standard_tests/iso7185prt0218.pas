{

PRT test 218: Missing ';' between successive variant cases

}

program iso7185prt0218;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 one, two: ()
                 three: ()
       end;

begin

   a.b := 1

end.
