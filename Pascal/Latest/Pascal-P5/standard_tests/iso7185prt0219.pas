{

PRT test 219: Attempt to define multiple variant sections

}

program iso7185prt0219;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 one, two: ();
                 three: ();
              case e: boolean of
                 true: ();
                 false: ()
       end;

begin

   a.b := 1

end.
