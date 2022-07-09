{

PRT test 1756: On reading a number from a textfile, after skipping preceding
               spaces and end-of-lines, it is an error if the rest of the 
               sequence does not form a signed-number.

               ISO 7185 reference: 6.9.1

}

program iso7185prt1756(output);

var a: text;
    b: real;

begin

   rewrite(a);
   writeln(a, '      10e       ');
   reset(a);
   read(a, b)

end.
