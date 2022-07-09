{

PRT test 149: Missing first variable in with statement list

}

program iso7185prt0149;

var a: record b, c: integer end;
    d: record e, f: integer end;

begin

   with ,d do e := f

end.
