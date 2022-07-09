{

PRT test 1904: if statement

    the condition part of an if statement must have boolean type
ISO 7185 6.8.3.4

}

program iso7185prt1904(output);

type myBoolean = (myFalse, myTrue);

var b: myBoolean;

begin

   b := myTrue;
   if b then
      writeln('error not detected');

end.
