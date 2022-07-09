{

PRT test 150: Missing second variable in with statement list

}

program iso7185prt0150;

var a: record b, c: integer end;
    d: record e, f: integer end;

begin

   with d, do e := f

end.
