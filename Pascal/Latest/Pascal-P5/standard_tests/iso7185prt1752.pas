{

PRT test 1752: For a for-statement, it is an error if the value of the 
               initial-value is not assignment-compatible with the type 
               possessed by the control-variable if the statement of the 
               for-statement is executed.

               ISO 7185 reference: 6.8.3.9

}

program iso7185prt1752(output);

var a: integer;

begin

   for a := 'c' to 10 do writeln('hi')

end.
