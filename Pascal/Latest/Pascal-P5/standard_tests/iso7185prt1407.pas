{

PRT test 1407: Missing field identifier after '.'

}

program iso7185prt1407(output);

var a: integer;
    b: record one, two: integer end;

begin

   a := b.

end.
