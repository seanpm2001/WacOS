{

PRT test 1403: Missing first expression in index list

}

program iso7185prt1403(output);

var a: integer;
    b: array [1..10, 1..10] of integer;

begin

   a := b[ ,5]  

end.
