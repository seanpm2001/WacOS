{

PRT test 1707b: It is an error if the value of each corresponding actual value
                parameter is not assignment compatible with the type possessed
                by the formal-parameter.

                ISO 7185 reference: 6.6.3.2

}

program iso7185prt1707b(output);

type r = record f: text end;

var d: r;

procedure b(c: r);

begin

   rewrite(c.f)
   
end;

begin

   b(d)
   
end.
