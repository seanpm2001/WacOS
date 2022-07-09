{

PRT test 1745: A term of the form i div j is an error if j is zero.

               ISO 7185 reference: 6.7.2.2

}

program iso7185prt1745(output);

var a: integer;
    b: integer;

begin

   a := 1;
   b := 0;
   a := a div b

end.
