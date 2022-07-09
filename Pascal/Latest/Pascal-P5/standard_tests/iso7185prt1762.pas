{

PRT test 1762: Invalid string index subrange.

    String index starts with 0.

}

program iso7185prt1762(output);

var s: packed array [0..10] of char;

begin

   s := 'h          ';
   writeln('The string is: ''', s, '''');

end.
