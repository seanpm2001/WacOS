{

PRT test 206: Misspelled 'case' to variant

}

program iso7185prt0206;

var a: record b, c: integer;
              csae d: boolean of
                 true: ();
                 false: ()
       end;

begin

   a.b := 1

end.
