{

PRT test 1731: For unpack, it is an error if the index-type of the unpacked 
               array is exceeded.

               ISO 7185 reference: 6.6.5.4

}

program iso7185prt1731;

var a: array [1..10] of integer;
    b: packed array [1..10] of integer;

begin

   unpack(b, a, 5)

end.
