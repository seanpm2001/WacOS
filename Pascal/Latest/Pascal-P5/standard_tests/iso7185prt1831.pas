{

PRT test 1831: Parameter type mismatch. Wrong type of a parameter.

    ISO 7185 6.8.2.3

}

program iso7185prt1831;

procedure a(b: integer; c: char);

begin

   b := 1;
   c := 'a'

end;

begin

   a(1, 2)

end.