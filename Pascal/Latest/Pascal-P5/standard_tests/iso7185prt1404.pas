{

PRT test 1404: Missing second expression in index list

}

program iso7185prt1404(output);

var a: integer;
    b: array [1..10, 1..10] of integer;

begin

   a := b[5, ]  

end.
