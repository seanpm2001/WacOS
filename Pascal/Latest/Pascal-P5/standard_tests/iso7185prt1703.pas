{

PRT test 1703: It is an error if the pointer-variable of an 
               identified-variable denotes a nil-value.

               ISO 7185 reference: 6.5.4

}

program iso7185prt1703(output);

var a: ^integer;
    b: integer;


begin

   a := nil;
   b := a^

end.
