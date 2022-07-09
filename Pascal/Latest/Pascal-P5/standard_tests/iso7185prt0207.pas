{

PRT test 207: Missing identifier for variant

}

program iso7185prt0207;

var a: record b, c: integer;
              case : boolean of
                 true: ();
                 false: ()
       end;

begin

   a.b := 1

end.
