{

PRT test 1707a: It is an error if the value of each corresponding actual value
               parameter is not assignment compatible with the type possessed
               by the formal-parameter.

               ISO 7185 reference: 6.6.3.2

}

program iso7185prt1707a(output);

procedure b(c: integer);

begin

   c := 1

end;

begin

   b('c')

end.
