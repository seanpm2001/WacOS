{

PRT test 1718: For write, it is an error if the value possessed by the 
               expression is not assignment-compatible with the 
               buffer-variable.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1718(output);

var a: file of integer;

begin

   rewrite(a);
   a^ := 'c'

end.
