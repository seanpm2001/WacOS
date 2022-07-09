{

PRT test 1855: String type with index that is not integer.

   Tests if a string type (type packed array of char) can have a non-integer
   subrange as it's index type.

}

program iso7185prt1855(output);

type enum = (one, two, three);
     sr   = two..three;

var s: packed array [sr] of char;

begin

   s := 'ab'
   
end.