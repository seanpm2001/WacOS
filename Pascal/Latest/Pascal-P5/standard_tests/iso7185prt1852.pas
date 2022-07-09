{

PRT test 1852: Out of range for index variable

   Test if out of range is checked on 'for' index variable.


}

program iso7185prt1852(output);

var i: 1..9;


begin

   for i := 0 to 10 do writeln('i: ', i)

end.