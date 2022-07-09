{

PRT test 221: Missing ',' between first and second field idents

}

program iso7185prt0221;

var a: record b c: integer end;

begin

   a.b := 1

end.
