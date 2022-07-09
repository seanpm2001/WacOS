{

PRT test 1727: For pack, it is an error if any of the components of the 
               unpacked array are both undefined and accessed.

               ISO 7185 reference: 6.6.5.4

}

program iso7185prt1727(output);

var a: array [1..10] of integer;
    b: packed array [1..10] of integer;

begin

   pack(a, 1, b)

end.
