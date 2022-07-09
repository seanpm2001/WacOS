{

PRT test 205: Missing ';' between successive fields

}

program iso7185prt0205;

var a: record b, c: integer end
    d: record e, f: integer end;

begin

   a.b := 1;
   d.e := 1

end.
