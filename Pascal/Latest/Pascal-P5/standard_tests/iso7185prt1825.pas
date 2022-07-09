{

PRT test 1825: Invalid type substitutions. Wrong type of case label.

    ISO 7185 6.8.3.5

}

program iso7185prt1825(input, output);

var i: integer;

begin

   read(i);
   case i of

      0: writeln('zero');
      1: writeln('one');
      'a': writeln('a')

   end

end.