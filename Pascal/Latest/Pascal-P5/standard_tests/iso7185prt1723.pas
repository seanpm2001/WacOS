{

PRT test 1723: For dispose, it is an error if the parameter of a pointer-type
               has a nil-value.

               ISO 7185 reference: 6.6.5.3

}

program iso7185prt1723(output);

var a: ^integer;

begin

   a := nil;
   dispose(a)

end.
