{

PRT test 1840: Write with invalid fields. Write with zero fraction.

    ISO 7185 6.9.3

}

program iso7185prt1840(output);

var r: real;

begin

   r := 1.0;
   writeln(r:1:0)

end.