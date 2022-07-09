{

PRT test 209: Missing type identifier without field identifier

}

program iso7185prt0209;

var a: record b, c: integer;
              case of
                 true: ();
                 false: ()
       end;

begin

   a.b := 1

end.
