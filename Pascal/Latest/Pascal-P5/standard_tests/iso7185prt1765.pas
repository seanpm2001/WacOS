{

PRT test 1765: String not based on char.

    String based on subrange of char, not char itself.

}

program iso7185prt1765(output);

type mychar = 'a'..'z';

var s: packed array [1..10] of mychar;

begin

   s := 'hello you ';
   writeln('The string is: ''', s, '''');

end.
