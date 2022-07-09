{

PRT test 1763: Invalid string index subrange.

    String index only one character.

}

program iso7185prt1763(output);

var s: packed array [1..1] of char;

begin

   s := 'h';
   writeln('The string is: ''', s, '''');

end.
