{

PRT test 1764: String not packed.

    String lacks packed modifier.

}

program iso7185prt1764(output);

var s: array [1..10] of char;

begin

   s := 'hello, you';
   writeln('The string is: ''', s, '''');

end.
