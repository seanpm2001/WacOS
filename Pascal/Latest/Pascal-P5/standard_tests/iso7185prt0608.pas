{

PRT test 608: Missing ';' between parameter specifications

}

program iso7185prt0608;

procedure a(b, c: integer d: char);

begin

   b := 1;
   c := 1;
   d := 'a'

end;

begin

   a(1, 2, 'a')

end.
