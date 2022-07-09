{

PRT test 210: Missing 'of' on variant

}

program iso7185prt0210;

var a: record b, c: integer;
              case d: boolean
                 true: ();
                 false: ()
       end;

begin

   a.b := 1

end.
