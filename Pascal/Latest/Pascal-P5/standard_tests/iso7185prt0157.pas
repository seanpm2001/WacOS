{

PRT test 157: Missing ',' between variables in with statement

}

program iso7185prt0157;

var a: record b, c: integer end;
    d: record e, f: integer end;

begin

   with a d do e := f

end.
