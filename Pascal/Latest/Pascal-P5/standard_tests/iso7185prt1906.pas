{

PRT test 1906: while statement

    the condition part of a while statement must have boolean type
ISO 7185 6.8.3.8

}

program iso7185prt1906(output);

type myBoolean = (myFalse, myTrue);

var b: myBoolean;

begin

   b := myFalse;
   while b do
      ;
   writeln('error not detected');

end.
