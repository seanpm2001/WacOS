{

PRT test 215: Missing ':' on variant case

}

program iso7185prt0215;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 one, two ();
                 three: ()
       end;

begin

   a.b := 1

end.
