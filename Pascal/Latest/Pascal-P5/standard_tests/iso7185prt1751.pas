{

PRT test 1751: For a case-statement, it is an error if none of the 
               case-constants is equal to the value of the case-index upon 
               entry to the case-statement.

               ISO 7185 reference: 6.8.3.5

}

program iso7185prt1751(output);

var a: integer;

begin

   a := 4;
   case a of

      1: writeln('one');
      2: writeln('two');
      3: writeln('three')

   end

end.
