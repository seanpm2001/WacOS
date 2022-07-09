{

PRT test 1714: It is an error if the file mode is not Inspection immediately
               prior to any use of get or read.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1714(output);

var a: file of integer;
    b: integer;

begin

   rewrite(a);
   read(a, b)

end.
