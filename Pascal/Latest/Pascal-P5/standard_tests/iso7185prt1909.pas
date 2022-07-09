{

PRT test 1909: Second expression

    An error in the second part of a boolean expression.
    This tests if the implementation tries to short circuit evaluate the 
    expression.

}

program iso7185prt1909(output);

var i: integer;
    a: array [1..10] of char;

begin

   i := 11;
   writeln('The result is: ', (i = 1) and (a[i] = 'g'))

end.
