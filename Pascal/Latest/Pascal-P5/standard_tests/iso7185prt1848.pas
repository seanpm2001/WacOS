{

PRT test 1848: Variable reference to packed variable

   Passing a packed element as a variable reference.
   ISO 7185 6.6.3.3


}

program iso7185prt1848;

var r: packed record
          i: integer;
          b: boolean
       end;

procedure a(var b: boolean);

begin

   b := true

end;

begin

   with r do a(b)

end.