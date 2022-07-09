{

PRT test 1861: Function parameter result types don't match

    6.6.3.6 Parameter list congruity
    Two formal-parameter-lists shall be congruous if they contain the same
    number of formal-parametersections and if the formal-parameter-sections in
    corresponding positions match. Two formalparameter-sections shall match if
    any of the following statements is true.

    ...
    
    d) They are both functional-parameter-specifications, the 
    formal-parameter-lists of the functionheadings thereof are congruous, and 
    the type-identifiers of the result-types of the functionheadings thereof 
    denote the same type.
    
}

program iso7185prt1861;

type x = 1..10;

function y: x;

begin

   y := 1
   
end;

procedure z(function f: integer);

begin
end;

begin

   z(y)
   
end.