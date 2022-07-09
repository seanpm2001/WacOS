{

PRT test 1717: For read, it is an error if the value possessed by the 
               buffer-variable is not assignment compatible with the 
               variable-access.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1717(output);

var a: file of integer;
    b: char;

begin

   rewrite(a);
   write(a, 1);
   read(a, b)

end.
