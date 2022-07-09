{

PRT test 1744: A term of the form x/y is an error if y is zero.

               ISO 7185 reference: 6.7.2.2

}

program iso7185prt1744;

var a: integer;
    b: integer;

begin

   { note I do this in integer because 0.0 in float is posibly inacurate, ie., 
     not 0 }
   a := 1;
   b := 0;
   a := round(a/b);

end.
