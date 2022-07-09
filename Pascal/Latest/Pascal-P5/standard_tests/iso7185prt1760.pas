{$s}
{

PRT test 1760: Alphanumeric label.

               Attempt to use alphanumeric label instead of number.

}

program iso7185prt1760(output);

label skip;

begin

   goto skip;

   writeln('*** Should not execute this');
   
   skip: writeln('At label');

end.
