{

PRT test 1908: Use real value as case constant

    A real value is used as a case constant.

}

program iso7185prt1908(output);

var i: integer;

begin

   i := 1;
   case i of
   
      1.1: writeln('one');
      2: writeln('two')
      
   end

end.
