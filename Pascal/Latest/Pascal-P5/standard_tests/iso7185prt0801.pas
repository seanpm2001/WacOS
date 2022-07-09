{

PRT test 801: Missing leading '(' in list

}

program iso7185prt0801;

procedure test(a, b: integer);

begin

   a := 1;
   b := 1

end;

begin

   test 1, 2)

end.
