{

PRT test 1916: Inappropriate application of sign.

    Use of sign on char value.

}

program iso7185prt1916(output);

var c: char;

begin

   c := 'a';
   writeln('Value is: ', +c)
   
end.
