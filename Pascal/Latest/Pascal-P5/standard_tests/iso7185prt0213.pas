{

PRT test 213: Missing first constant on variant

}

program iso7185prt0213;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 , two: ();
                 three: ()
       end;

begin

   a.b := 1

end.
