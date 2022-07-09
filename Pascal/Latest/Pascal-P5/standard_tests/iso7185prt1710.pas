{

PRT test 1710: It is an error if the file is undefined immediately prior to
               any use of put, write, writeln or page.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1710(output);

var a: file of integer;

begin

   write(a, 1)

end.
