{

PRT test 803: Missing first parameter in parameter list

}

program iso7185prt0803;

procedure test(a, b: integer);

begin

   a := 1;
   b := 1

end;

begin

   test(, 2)

end.
