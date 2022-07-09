{

PRT test 1866: eoln() if eof() is true.

    Tests if eoln() generates an error if eof() is true. note that eoln() on a
    file at eof() was undefined before ISO 7185.
    
    6.6.6.5 Boolean functions
    
    eoln(f)

    The parameter f shall be a textfile ; if the actual-parameter-list is 
    omitted, the function shall be applied to the required textfile input (see
    6.10) and the program shall contain a program-parameter-list containing an
    identifier with the spelling input. When eoln(f) is activated, it shall be
    an error if f is undefined or if eof(f) is true ; otherwise, the function 
    shall yield the value true if f.R.first is an end-of-line component (see 
    6.4.3.5) ; otherwise, false.
}

program iso7185prt1866;

var f: text;
    b: boolean;
   
begin

   rewrite(f);
   writeln(f, 'test');
   reset(f);
   readln(f);
   b := eoln(f)
   
end.