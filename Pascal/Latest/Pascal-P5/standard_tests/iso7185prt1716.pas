{

PRT test 1716: It is an error if end-of-file is true immediately prior to any
               use of get or read.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1716(output);

var a: file of integer;
    b: integer;

begin

   rewrite(a);
   write(a, 1);
   reset(a);
   read(a, b);
   read(a, b)

end.
