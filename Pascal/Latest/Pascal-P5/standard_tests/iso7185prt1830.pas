{

PRT test 1830: Parameter number mismatch. More parameters than specified.

    ISO 7185 6.8.2.3

}

program iso7185prt1830;

procedure a(b: integer; c: char);

begin

   b := 1;
   c := 'a'

end;

begin

   a(1, 'a', 1.0)

end.