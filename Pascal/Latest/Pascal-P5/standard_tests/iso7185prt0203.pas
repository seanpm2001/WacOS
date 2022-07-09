{

PRT test 203: Missing ':' between ident and type

}

program iso7185prt0203;

var a: record b integer end;

begin

   a.b := 1

end.
