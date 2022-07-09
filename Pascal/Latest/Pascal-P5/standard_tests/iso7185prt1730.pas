{

PRT test 1730: For unpack, it is an error if any of the components of the 
               packed array are undefined.

               ISO 7185 reference: 6.6.5.4

}

program iso7185prt1730(output);

var a: array [1..10] of integer;
    b: packed array [1..10] of integer;

begin

   unpack(b, a, 1)

end.
