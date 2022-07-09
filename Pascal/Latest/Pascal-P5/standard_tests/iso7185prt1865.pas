{

PRT test 1865: pred() where the predecessor does not exist.

    succ() and pred() are not the same as operators, which generally yield
    integer, but instead they give the same type as the operand:
    
    6.6.6.4 Ordinal functions
    
    pred(x)

    From the expression x that shall be of an ordinal-type, this function shall
    return a result that shall be of the same type as that of the expression 
    (see 6.7.1). The function shall yield a value whose ordinal number is one 
    less than that of the expression x, if such a value exists. It shall be an 
    error if such a value does not exist.
    
}

program iso7185prt1865;


type enum = (one, two, three);

var e: enum;

begin

   e := one;
   e := pred(e)
   
end.