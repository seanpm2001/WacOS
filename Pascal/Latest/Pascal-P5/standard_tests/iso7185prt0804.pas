{

PRT test 804: Missing second parameter in parameter list

}

program iso7185prt0804;

procedure test(a, b: integer);

begin

   a := 1;
   b := 1

end;

begin

   test(1, )

end.
