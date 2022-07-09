{

PRT test 602: Missing second parameter identifier

}

program iso7185prt0602;

procedure a(b,: integer);

begin

   b := 1

end;

begin

   a(1, 2)

end.
