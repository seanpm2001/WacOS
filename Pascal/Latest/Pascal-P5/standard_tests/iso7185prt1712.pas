{

PRT test 1712: It is an error if the buffer-variable is undefined immediately 
               prior to any use of put.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1712(output);

var a: file of integer;

begin

   rewrite(a);
   put(a)

end.
