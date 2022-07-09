{

PRT test 1743: An expression denotes a value unless a variable denoted by a
               variable-access contained by the expression is undefined at the
               time of its use, in which case that use is an error.

               ISO 7185 reference: 6.7.1

}

program iso7185prt1743(output);

var a: integer;

begin

   { In this case the undefined variable is output to force the error, if it can
     be done. Not many compilers check for undefines. }
   write(a)

end.
