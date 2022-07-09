{

PRT test 1508: Missing number before exponent

}

program iso7185prt1508(output);

var a: integer;

begin

   { somewhat ambiguous, could be "e" as identifier }
   a := e+5

end.
