{

PRT test 1704: It is an error if the pointer-variable of an 
               identified-variable is undefined.

               ISO 7185 reference: 6.5.4

}

program iso7185prt1704(output);

var a: ^integer;
    b: integer;

begin

   b := a^

end.
