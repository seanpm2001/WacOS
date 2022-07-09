{

PRT test 1918: Function assignment present but not executed.

    Tests the case where a function assignment exists, but is not executed due
    to being inside a conditional.

}

program iso7185prt1918(output);

function x: integer;

var y: integer;

begin

   y := 1;
   if y > 1 then x := 2
   
end;

begin

   writeln('Value is: ', x)
   
end.
