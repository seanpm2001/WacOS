{

PRT test 1724: For dispose, it is an error if the parameter of a pointer-type is
               undefined.

               ISO 7185 reference: 6.6.5.3

}

program iso7185prt1724(output);

var a: ^integer;

begin

   dispose(a)

end.
