{

PRT test 1737: For chr(x), the function returns a result of char-type that is 
               the value whose ordinal number is equal to the value of the 
               expression x if such a character value exists. It is an error
               if such a character value does not exist.

               ISO 7185 reference: 6.6.5.3

}

program iso7185prt1737;

var a: integer;
    b: char;

begin

   a := -1;
   b := chr(a)

end.
