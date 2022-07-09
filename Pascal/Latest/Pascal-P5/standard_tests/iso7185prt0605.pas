{

PRT test 605: Missing type identifier on parameter specification

}

program iso7185prt0605;

procedure a(b, c:);

begin

   b := 1;
   c := 1

end;

begin

   a(1,2)

end.
