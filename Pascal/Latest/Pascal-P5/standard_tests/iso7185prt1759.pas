{

PRT test 1759: Label "apparent value" not in the range 0 to 9999.

               ISO 7185 reference: 6.1.6

               The numeric value of a label must be within 0 to 9999.

}

program iso7185prt1759(output);

label 10000;

begin

   goto 10000;

   writeln('*** Should not execute this');
   
   10000: writeln('At label');

end.
