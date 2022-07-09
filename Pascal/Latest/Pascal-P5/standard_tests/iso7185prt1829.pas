{

PRT test 1829: Parameter number mismatch. Less parameters than specified.

    ISO 7185 6.8.2.3

}

program iso7185prt1829;

procedure a(b: integer; c: char);

begin

   b := 1;
   c := 'a'

end;

begin

   a(1)

end.