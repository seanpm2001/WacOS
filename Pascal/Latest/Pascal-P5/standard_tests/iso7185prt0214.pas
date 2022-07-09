{

PRT test 214: Missing second constant on variant

}

program iso7185prt0214;

type q = (one, two, three);

var a: record b, c: integer;
              case d: q of
                 one,: ();
                 three: ()
       end;

begin

   a.b := 1

end.
