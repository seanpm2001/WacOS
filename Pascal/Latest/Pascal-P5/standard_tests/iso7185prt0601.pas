{

PRT test 601: Missing first parameter identifier

}

program iso7185prt0601;

procedure a(, c: integer);

begin

   c := 1

end;

begin

   a(1, 2)

end.
