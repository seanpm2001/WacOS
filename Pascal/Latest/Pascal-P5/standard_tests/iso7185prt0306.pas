{

PRT test 306: Missing type ident after ':' for function

}

program iso7185prt0306;

function x(one, two: integer):;

begin

   one := 1;
   two := 2;
   x := 'a'

end;

begin

end.
