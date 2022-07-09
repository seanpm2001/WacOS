{

PRT test 1905: repeat statement

    the condition of the until part of a repeat statement must have boolean type
    ISO 7185 6.8.3.8

}

program iso7185prt1905(output);

type myBoolean = (myFalse, myTrue);

var b: myBoolean;

begin

   b := myTrue;
   repeat
      ;
   until b;
   writeln('error not detected');

end.
