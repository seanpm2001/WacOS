{

PRT test 1755: On reading an integer from a textfile, it is an error if the 
               value of the signed-integer read is not assignment-compatible
               with the type possessed by variable-access.

               ISO 7185 reference: 6.9.1

}

program iso7185prt1755(output);

var a: text;
    b: 1..5;

begin

   rewrite(a);
   writeln(a, '      10       ');
   reset(a);
   read(a, b)

end.
