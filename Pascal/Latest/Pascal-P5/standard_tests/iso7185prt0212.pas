{

PRT test 212: Missing case constant on variant

}

program iso7185prt0212;

var a: record b, c: integer;
              case d: boolean of
                 : ();
                 false: ()
       end;

begin

   a.b := 1

end.
