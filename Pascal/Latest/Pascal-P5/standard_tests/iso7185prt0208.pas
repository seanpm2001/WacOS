{

PRT test 208: Missing type identifier with field identifier

}

program iso7185prt0208;

var a: record b, c: integer;
              case d: of
                 true: ();
                 false: ()
       end;

begin

   a.b := 1

end.
