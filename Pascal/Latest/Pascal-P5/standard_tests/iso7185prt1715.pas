{

PRT test 1715: It is an error if the file is undefined immediately prior to 
               any use of get or read.

               ISO 7185 reference: 6.6.5.2

}

program iso7185prt1715(output);

var a: file of integer;

begin

   get(a)

end.
