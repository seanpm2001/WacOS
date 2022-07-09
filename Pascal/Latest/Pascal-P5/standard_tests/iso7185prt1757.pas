{

PRT test 1757: It is an error if the buffer-variable is undefined immediately
               prior to any use of read.

               ISO 7185 reference: 6.9.1

               Other than eof being true, I don't know of another undefined
               buffer variable condition.

}

program iso7185prt1757;

var a: file of integer;
    b: integer;

begin

   rewrite(a);
   reset(a);
   read(a, b)

end.
