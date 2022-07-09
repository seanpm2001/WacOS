{

PRT test 603: Missing ',' between parameter identifiers

}

program iso7185prt0603;

procedure a(b c: integer);

begin

   b := 1;
   c := 1

end;

begin

   a(1, 2)

end.
