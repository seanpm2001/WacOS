{

PRT test 155: Missing only variable in with statement list

}

program iso7185prt0155;

var a: record b, c: integer end;
    d: record e, f: integer end;

begin

   a.b := 1;
   with do e := f

end.
