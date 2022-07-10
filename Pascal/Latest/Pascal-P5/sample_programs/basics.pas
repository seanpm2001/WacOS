(*$l-*)
(******************************************************************************
*                                                                             *
*                            TINY PASCAL BASIC                                *
*                                                                             *
*                            1980 S. A. MOORE                                 *
*                                                                             *
* Implements a small basic in Pascal. An example of how small a program can   *
* be to implement a simple language.                                          *
* Variables are allowed, using the letters "a" thru "z". Integers are denoted *
* by the letters alone. Strings are denoted by "a$" form.                     *
* The following statements are implemented:                                   *
*                                                                             *
*    input <variable>   Reads the contents of the variable from the user.     *
*                       If the variable is integer, a line is read from the   *
*                       user, then any spaces on the line skipped, then a     *
*                       number read.                                          *
*                       If the variable is string, the entire line is         *
*                       assigned to it, including any spaces.                 *
*                                                                             *
*    print <expr> [,<expr].. [;] Prints the expression. The expression can be *
*                       integer or string. If a trailing ";" exists, the next *
*                       print will resume on the same line. Any number of     *
*                       items may appear to be printed on the same line,      *
*                       separated by ",".                                     *
*                                                                             *
*    goto <integer>     Control resumes at the line specified by the integer. *
*                       Note that no "calculated gotos" are allowed.          *
*                                                                             *
*    if <expr> then <statement>  The expression must be a integer. If the     *
*                       condition is 0, control resumes on the next line.     *
*                       if the condition is not 0, the statement after "then" *
*                       is executed (as well as the rest of the line).        *
*                                                                             *
*    rem <line>         The entire rest of the line is ignored.               *
*                                                                             *
*    stop               Terminates program execution. The values of variables *
*                       are not cleared.                                      *
*                                                                             *
*    run                All variables are cleared, with integers becoming 0,  *
*                       and strings becoming empty. Then control passes to    *
*                       the first statement in the program.                   *
*                                                                             *
*    list [<start>[,<end>]]  Lists all program lines between the given lines. *
*                       The default if no lines are given is the starting     *
*                       and ending lines of the entire program.               *
*                                                                             *
*    new                Clears the entire program and stops execution.        *
*                                                                             *
*    [let] <var> = <expr>  Assigns the value of the expression to the         *
*                       variable. The variable must be the same type (string  *
*                       or integer) as the expression. The "let" keyword is   *
*                       optional.                                             *
*                                                                             *
*    bye                Exits basic for the operating system.                 *
*                                                                             *
* Expressions can contain the following operators:                            *
*                                                                             *
*    <, >, =, <>, <=, >=          Comparision.                                *
*    +, -, *, /, mod              Basic math.                                 *
*    left$(<str>, <expr>)         The leftmost characters of the string.      *
*    right$(<str>, <expr>)        The rightmost characters of the string.     *
*    mid$(<str>, <start>, <len>)  The middle characters of the string.        *
*    str$(<expr>)                 The string form of the integer expression.  *
*    val(<str>)                   The integer equivalent of the string.       *
*    chr(<str>)                   The ascii value of the first character.     *
*                                                                             *
* The internal form of the program is keyword compressed for effiency, which  *
* both allows for a smaller internal program, and simplifies the decoding of  *
* keywords.                                                                   *
*                                                                             *
*                                                                             *
* Notes:                                                                      *
*                                                                             *
* 1. If the program store were of the same form as basic strings, routines    *
* that handle both in common could be used (example: getting a number from    *
* the string).                                                                *
*                                                                             *
******************************************************************************)

program basics(input, output);

label   88, 77, 99;

const   maxlin = 9999; (* maximum line number *)
        maxpgm = 100;  (* maximum line store *)
        maxstk = 10;   (* maximum temp count *)
        maxkey = 29;   (* maximum key store *)

        (* key codes *)

        cinput =  1; cprint =  2; cgoto  =  3; cif    =  4;
        crem   =  5; cstop  =  6; crun   =  7; clist  =  8;
        cnew   =  9; clet   = 10; cbye   = 11; clequ  = 12;
        cgequ  = 13; cequ   = 14; cnequ  = 15; cltn   = 16;
        cgtn   = 17; cadd   = 18; csub   = 19; cmult  = 20;
        cdiv   = 21; cmod   = 22; cleft  = 23; cright = 24;
        cmid   = 25; cthen  = 26; cstr   = 27; cval   = 28;
        cchr   = 29;

type    string10   = packed array [1..10] of char;   (* key *)
        string80   = packed array [1..80] of char;   (* general string *)
        bstring80  = record
                       len : integer;
                       str : string80
                    end;
        vartyp     = (tint, tstr); (* variable type *)
        (* error codes *)
        errcod     = (eitp, estate, eexmi, eeque, estyp, epbful, eiovf, evare,
                      elabnf, einte, econv, elntl, ewtyp, erpe, eexc, emqu,
                      eifact, elintl, estrovf, eedlexp, elpe, ecmaexp, estre,
                      estrinx);

var     prgm:  array [0..maxpgm] of string80; (* program store *)
        strs:  array ['a'..'z'] of bstring80;  (* string store *)
        ints:  array ['a'..'z'] of integer;   (* integer store *)
        keywd: array [cinput..cchr] of string10; (* keywords *)
        temp:  array [1..maxstk] of record
                                        typ  : vartyp;
                                        int  : integer;
                                        bstr : bstring80
                                     end;
        prgmc,           (* program counter (0 = input line) *)
        top,             (* current temps top *)
        linec: integer;  (* character position *)

(* print key compressed line *)

procedure prtlin(var str : string80);

var i, j: integer;

procedure prtkey(var str : string10);

var i, j: integer;

begin (* prtkey *)

   j := 10;
   while (str[j] = ' ') and (j > 0) do j := j - 1;
   j := j + 1;
   i := 1;
   while i < j do begin write(str[i]); i := i + 1 end

end; (* prtkey *)

begin (* prtlin *)

   j := 80;
   while (str[j] = ' ') and (j > 0) do j := j - 1;
   j := j + 1;
   i := 1;
   while i < j do begin

      if ord(str[i]) < ord(' ') then prtkey(keywd[ord(str[i])])
      else write(str[i]);
      i := i + 1

   end;
   writeln

end; (* prtlin *)

(* print error *)

procedure prterr(err : errcod);

begin

   if prgmc <> 0 then prtlin(prgm[prgmc]);
   write('*** ');
   case err of

      eitp:     writeln('Interpreter error');
      estate:   writeln('Statement expected');
      eexmi:    writeln('Expression must be integer');
      eeque:    writeln('"=" expected');
      estyp:    writeln('Operands not of same type');
      epbful:   writeln('Program buffer full');
      eiovf:    writeln('Input overflow');
      evare:    writeln('Variable expected');
      elabnf:   writeln('Statement label not found');
      einte:    writeln('Integer expected');
      econv:    writeln('Conversion error');
      elntl:    writeln('Line number too large');
      ewtyp:    writeln('Operand(s) of wrong type');
      erpe:     writeln('")" expected');
      eexc:     writeln('Expression too complex');
      emqu:     writeln('Missing quote');
      eifact:   writeln('Invalid factor');
      elintl:   writeln('Line number too large');
      estrovf:  writeln('String overflow');
      eedlexp:  writeln('End of line expected');
      elpe:     writeln('"(" expected');
      ecmaexp:  writeln('"," expected');
      estre:    writeln('String expected');
      estrinx:  writeln('String indexing error')

   end;
   goto 88 (* loop to ready *)

end;

(* check character *)

function chkchr : char;

var c: char;

begin

   if linec <= 80 then c := prgm[prgmc][linec]
   else c := ' ';
   chkchr := c

end;

(* check end of line *)

function chkend: boolean;

begin

   chkend := linec > 80 (* past end of line *)

end;

(* get character *)

function getchr: char;

begin

   getchr := chkchr;
   if not chkend then linec := linec + 1

end;

(* check next character *)

function chknxt(c : char) : boolean;

begin

   chknxt := c = chkchr;
   if c = chkchr then c := getchr

end;

(* skip spaces *)

procedure skpspc;

var c: char;

begin

   while (chkchr = ' ') and not chkend do c := getchr;

end;

(* check end of statement *)

function chksend: boolean;

begin

   skpspc; (* skip spaces *)
   chksend := chkend or (chkchr = ':') (* check eoln or ':' *)

end;

(* check null string *)

function null(var str : string80) : boolean;

var i: integer;
    f: boolean;

begin

   f := true;
   for i := 1 to 80 do if str[i] <> ' ' then f := false;
   null := f

end;

(* check digit *)

function digit(c : char) : boolean;

begin

   digit := (ord(c) >= ord('0')) and (ord(c) <= ord('9'))

end;

(* convert to lower case *)

function lcase(c : char) : char;

begin

   if (ord(c) >= ord('A')) and (ord(c) <= ord('Z')) then
      c := chr(ord(c) - ord('A') + ord('a'));
   lcase := c

end;

(* check alphabetical *)

function alpha(c : char) : boolean;

begin

   alpha := (ord(lcase(c)) >= ord('a')) and
      (ord(c) <= ord('z'))

end;

(* parse leading integer *)

function lint(var str : string80) : integer;

var i, v: integer;
    b:    boolean;

begin

   v := 0;
   i := 1;
   while (i < 80) and (str[i] = ' ') do i := i + 1;
   repeat

      if digit(str[i]) then begin

         v := v*10 + (ord(str[i]) - ord('0'));
         if i <> 80 then begin

            i := i + 1;
            b := false

         end else b := true

      end else b := true

   until b;
   lint := v

end;

(* search label *)

function schlab(lab : integer):integer;

var i: integer;

begin

   i := 1;
   while (lab <> lint(prgm[i])) and (i < maxpgm) do i := i + 1;
   if lab <> lint(prgm[i]) then prterr(elabnf);
   schlab := i

end;

(* input string *)

procedure inpstr(var str : string80);

var i: integer;

begin

   for i := 1 to 80 do str[i] := ' ';
   i := 1;
   while (i <= 80) and not eoln do begin

      read(str[i]);
      i := i + 1

   end;
   readln;
   if (i > 80) then prterr(eiovf)

end;

(* parse variable reference *)

function getvar : char;

begin

   if not alpha(chkchr) then prterr(evare);
   getvar := lcase(getchr)

end;

(* enter line to store *)

procedure enter(var str : string80);

var line, i, j, k: integer;
    f:             boolean;

begin

   line := lint(str);
   if line > maxlin then prterr(elintl); (* input line number to large *)
   i := 1;
   f := false;
   repeat

      if null(prgm[i]) then f := true
      else if lint(prgm[i]) < line then begin

         i := i + 1;
         if i > maxpgm then f := true

      end else f := true

   until f;
   if i > maxpgm then prterr(epbful);
   if null(prgm[i]) then prgm[i] := str
   else if lint(prgm[i]) = line then begin

      j := 1;
      while (str[j] = ' ') and (j < 80) do j := j + 1;
      while digit(str[j]) and (j < 80) do j := j + 1;
      while (str[j] = ' ') and (j < 80) do j := j + 1;
      if j = 80 then begin

         for k := i to maxpgm - 1 do prgm[k] := prgm[k + 1];
         for j := 1 to 80 do prgm[maxpgm][j] := ' '

      end else prgm[i] := str

   end else if not null(prgm[maxpgm]) then prterr(epbful)
   else begin

      for k := maxpgm downto i + 1 do prgm[k] := prgm[k - 1];
      prgm[i] := str

   end

end;

(* compress keys *)

procedure keycom(var str : string80);

var ts:        string80;
    k, i1, i2: integer;
    f:         boolean;
    c:         char;

function matstr(var stra: string80; var i: integer;
                 var strb: string10): boolean;

var i1, i2: integer;
    f:      boolean;

begin (* matstr *)

   i1 := i;
   i2 := 1;
   repeat

      if strb[i2] = ' ' then f := false
      else if lcase(stra[i1]) = lcase(strb[i2]) then begin

         f := true;
         i1 := i1 + 1;
         i2 := i2 + 1

      end
      else f := false

   until not f or (i1 > 80) or (i2 > 10);
   if i2 > 10 then begin f := true; i := i1 end
   else if strb[i2] = ' ' then begin f := true; i := i1 end
   else f := false;
   matstr := f

end; (* matstr *)

begin (* keycom *)

   for i2 := 1 to 80 do ts[i2] := ' ';
   i1 := 1;
   i2 := 1;
   repeat

      if str[i1] = '"' then begin

         ts[i2] := '"';
         i1 := i1 + 1;
         i2 := i2 + 1;
         c := ' ';
         while (i1 <= 80) and (c <> '"') do begin

            c := str[i1];
            ts[i2] := str[i1];
            i1 := i1 + 1;
            i2 := i2 + 1

         end

      end else if str[i1] <= ' ' then begin

         (* replace spaces or control characters with a space *)
         ts[i2] := ' ';
         i1 := i1 + 1;
         i2 := i2 + 1

      end else begin

         k := 1;
         f := false;
         while (k <= maxkey) and not f do
         begin

            f := matstr(str, i1, keywd[k]);
            k := k + 1

         end;
         if f then ts[i2] := chr(k - 1)
         else begin ts[i2] := str[i1]; i1 := i1 + 1 end;
         i2 := i2 + 1

      end

   until i1 > 80;
   for i1 := 1 to 80 do str[i1] := ts[i1]
(* this diagnostic prints the resulting tolken sequence *)
(*;for i1 := 1 to 80 do write(ord(str[i1]), ' ');*)

end; (* keycom *)

(* get integer *)

function getint: integer;

var v: integer;

begin

   v := 0;
   skpspc;
   if not digit(chkchr) then prterr(einte);
   repeat v := v*10 + (ord(getchr) - ord('0'))
   until not digit(chkchr);
   getint := v

end;

(* get integer from string *)

function getval(var str: string80): integer;

var i: integer;

begin

   i := 1;
   while (i <= 80) and (str[i] = ' ') do i := i + 1;
   if not digit(str[i]) then prterr(einte);
   getval := lint(str);
   while (i < 80) and digit(str[i]) do i := i + 1;
   while (i < 80) and (str[i] = ' ') do i := i + 1;
   if i <> 80 then prterr(econv)

end;

(* get integer from basic string *)

function getbval(var str: bstring80): integer;

var i, v: integer;

begin

   i := 1;
   while (i <= str.len) and (str.str[i] = ' ') do i := i + 1; (* skip spaces *)
   if not digit(str.str[i]) then prterr(einte); (* number not present *)
   v := 0; (* clear result *)
   while (i <= str.len) and digit(str.str[i]) do begin (* parse digit *)

      v := v*10+ord(str.str[i])-ord('0'); (* scale, convert and add in digit *)
      i := i+1 (* next character *)

   end;
   while (i <= str.len) and (str.str[i] = ' ') do i := i + 1;
   if i <= str.len then prterr(econv);
   getbval := v (* return result *)

end;

(* place integer to string *)

procedure putbval(var str: bstring80; v: integer);

var p: integer; (* power holder *)
    i: integer; (* string index *)

begin

   str.len := 0; (* clear result string *)
   p := 10000; (* set maximum power *)
   i := 1; (* set 1st character *)
   if v < 0 then begin (* negative *)

      str.str[i] := '-'; (* place minus sign *)
      i := i + 1; (* next character *)
      v := -v (* negate number *)

   end;
   while p <> 0 do begin (* fit powers *)

      str.str[i] := chr(v div p+ord('0')); (* place digit *)
      if str.str[1] = '-' then begin (* negative *)

         if (str.str[2] <> '0') or (p = 1) then i := i + 1; (* next digit *)

      end else (* positive *)
         if (str.str[1] <> '0') or (p = 1) then i := i + 1; (* next digit *)
      v := v mod p; (* remove from value *)
      p := p div 10 (* find next power *)

   end;
   str.len := i-1 (* set length of string *)

end;

(* print basic string *)

procedure prtbstr(var bstr: bstring80);

var i: integer;

begin

   for i := 1 to bstr.len do write(bstr.str[i]);

end;

(* input basic string *)

procedure inpbstr(var bstr: bstring80);

var i: integer;

begin

   for i := 1 to 80 do bstr.str[i] := ' ';
   i := 1;
   while (i < 80) and not eoln do begin

      read(bstr.str[i]);
      i := i + 1

   end;
   if (i > 80) and not eoln then prterr(eiovf);
   readln;
   bstr.len := i-1

end;

(* concatenate basic strings *)

procedure cat(var bstra, bstrb: bstring80);

var i: integer; (* index for string *)

begin

   if (bstra.len + bstrb.len) > 80 then prterr(estrovf); (* string overflow *)
   (* copy source after destination *)
   for i := 1 to bstrb.len do bstra.str[bstra.len+i] := bstrb.str[i];
   bstra.len := bstra.len + bstrb.len (* set new length *)

end;

(* check strings equal *)

function strequ(var bstra, bstrb: bstring80): boolean;

var i: integer; { index for string }
    m: boolean; { match flag }

begin


   m := true; { say they match }
   if bstra.len <> bstrb.len then m := false { lengths unequal, no match }
   else { compare by character }
      for i := 1 to bstra.len do
         if bstra.str[i] <> bstrb.str[i] then m := false;
   strequ := m { return match status }

end;

(* check string less than *)

function strltn(var bstra, bstrb: bstring80): boolean;

var i: integer; { index for string }
    m: boolean; { match flag }

begin


   m := true; { say less than }
   i := 1; { set 1st character }
   { skip to end or first unequal character }
   while (i <= bstra.len) and (i <= bstrb.len) and
      (bstra.str[i] = bstrb.str[i]) do i := i+1;
   if (i <= bstra.len) and (i <= bstrb.len) then begin

      { stopped on valid (mismatching) character }
      m := bstra.str[i] < bstrb.str[i] { match by character }

   end else m := bstra.len < bstrb.len; { else longer string is greater }
   strltn := m { return match status }

end;

(* check stack items equal *)

function chkequ : boolean;

begin

   if temp[top].typ <> temp[top - 1].typ then prterr(ewtyp)
   else if temp[top].typ = tint then
      chkequ := temp[top - 1].int = temp[top].int
   else chkequ := strequ(temp[top - 1].bstr, temp[top].bstr)

end;

(* check stack items less than *)

function chkltn: boolean;

begin

   if temp[top].typ <> temp[top - 1].typ then prterr(ewtyp)
   else if temp[top].typ = tint then
      chkltn := temp[top - 1].int < temp[top].int
   else chkltn := strltn(temp[top - 1].bstr, temp[top].bstr)

end;

(* check stack items greater than *)

function chkgtn: boolean;

begin

   if temp[top].typ <> temp[top - 1].typ then prterr(ewtyp)
   else if temp[top].typ = tint then
      chkgtn := temp[top - 1].int > temp[top].int
   else chkgtn := strltn(temp[top].bstr, temp[top - 1].bstr)

end;

(* set tos true *)

procedure settrue;

begin

   temp[top].typ := tint;
   temp[top].int := 1

end;

(* set tos false *)

procedure setfalse;

begin

   temp[top].typ := tint;
   temp[top].int := 0

end;

(* clear program store *)

procedure clear;

var x, y: integer;
    c:    char;

begin

   for x := 1 to maxpgm do
      for y := 1 to 80 do prgm[x][y] := ' ';
   for c := 'a' to 'z' do strs[c].len := 0;
   for c := 'a' to 'z' do ints[c] := 0;
   prgmc := 0;
   linec := 1;
   top := 1

end;

(* clear variable store *)

procedure clrvar;

var c: char;

begin

   for c := 'a' to 'z' do strs[c].len := 0;
   for c := 'a' to 'z' do ints[c] := 0;
   prgmc := 0;
   linec := 1;
   top := 1

end;

(* execute string *)

procedure exec;

label 1; (* exit procedure *)

var c: char;

(* execute statement *)

procedure stat;

var x, y: integer;
    c:    char;
    s:    string80;
    b:    boolean;

(* parse expression *)

procedure expr;

(* parse simple expression *)

procedure sexpr;

(* parse term *)

procedure term;

(* parse factor *)

procedure factor;

var i: integer;
    c: char;

begin (* factor *)

   skpspc;
   c := chkchr; (* save starting character *)
   if chknxt('(') then begin

      expr;
      if not chknxt(')') then prterr(erpe)

   end else if chknxt(chr(cadd)) then begin

      factor;
      if temp[top].typ <> tint then prterr(ewtyp)

   end else if chknxt(chr(csub)) then begin

      factor;
      if temp[top].typ <> tint then prterr(ewtyp);
      temp[top].int := - temp[top].int

   end else if chknxt('"') then begin

      top := top + 1;
      if top > maxstk then prterr(eexc);
      temp[top].typ := tstr;
      i := 1;
      while (i <= 80) and (chkchr <> '"') do begin

         temp[top].bstr.str[i] := getchr;
         i := i + 1

      end;
      if not chknxt('"') then prterr(emqu);
      temp[top].bstr.len := i - 1

   end else if digit(chkchr) then begin

      top := top + 1;
      if top > maxstk then prterr(eexc);
      temp[top].typ := tint;
      temp[top].int := getint

   end else if alpha(chkchr) then begin

      top := top + 1;
      if top > maxstk then prterr(eexc);
      c := getvar;
      if chknxt('$') then begin

         temp[top].typ := tstr;
         temp[top].bstr := strs[c]

      end else begin

         temp[top].typ := tint;
         temp[top].int := ints[c]

      end

   end else if chknxt(chr(cleft)) or chknxt(chr(cright)) or
               chknxt(chr(cmid)) then begin

      (* left$, right$ *)
      skpspc; (* skip spaces *)
      if not chknxt('(') then prterr(elpe); (* '(' expected *)
      expr; (* parse expression *)
      if temp[top].typ <> tstr then prterr(estre); (* string expected *)
      skpspc; (* skip spaces *)
      if not chknxt(',') then prterr(ecmaexp); (* ',' expected *)
      expr; (* parse expression *)
      if temp[top].typ <> tint then prterr(einte); (* integer expected *)
      skpspc; (* skip spaces *)
      if c <> chr(cmid) then begin (* left$ or right$ *)

         if not chknxt(')') then prterr(erpe); (* ')' expected *)
         if temp[top].int > temp[top-1].bstr.len then prterr(estrinx);
         if c = chr(cright) then (* right$ *)
            for i := 1 to temp[top].int do (* move string left *)
               temp[top-1].bstr.str[i] :=
                  temp[top-1].bstr.str[i+temp[top-1].bstr.len-temp[top].int];
         temp[top-1].bstr.len := temp[top].int; (* set new length left *)
         top := top-1 (* clean stack *)

      end else begin (* mid$ *)

         if not chknxt(',') then prterr(ecmaexp); (* ',' expected *)
         expr; (* parse end expression *)
         if temp[top].typ <> tint then prterr(einte); (* integer expected *)
         skpspc; (* skip spaces *)
         if not chknxt(')') then prterr(erpe); (* ')' expected *)
         (* check requested length > string length *)
         if temp[top].int+temp[top-1].int-1 > temp[top-2].bstr.len then
            prterr(estrinx);
         for i := 1 to temp[top].int do (* move string left *)
            temp[top-2].bstr.str[i] := temp[top-2].bstr.str[i+temp[top-1].int-1];
         temp[top-2].bstr.len := temp[top].int; (* set new length left *)
         top := top-2 (* clean stack *)

      end

   end else if chknxt(chr(cchr)) then begin (* chr *)

      if not chknxt('(') then prterr(elpe); (* '(' expected *)
      expr; (* parse expression *)
      if temp[top].typ <> tstr then prterr(estre); (* string expected *)
      skpspc; (* skip spaces *)
      if not chknxt(')') then prterr(erpe); (* ')' expected *)
      if temp[top].bstr.len < 1 then prterr(estrinx); (* check valid *)
      c := temp[top].bstr.str[1]; (* get the 1st character *)
      temp[top].typ := tint; (* change to integer *)
      temp[top].int := ord(c) (* place result *)

   end else if chknxt(chr(cval)) then begin (* val *)

      if not chknxt('(') then prterr(elpe); (* '(' expected *)
      expr; (* parse expression *)
      if temp[top].typ <> tstr then prterr(estre); (* string expected *)
      skpspc; (* skip spaces *)
      if not chknxt(')') then prterr(erpe); (* ')' expected *)
      i := getbval(temp[top].bstr); (* get string value *)
      temp[top].typ := tint; (* change to integer *)
      temp[top].int := i (* place result *)

   end else if chknxt(chr(cstr)) then begin (* str$ *)

      if not chknxt('(') then prterr(elpe); (* '(' expected *)
      expr; (* parse expression *)
      if temp[top].typ <> tint then prterr(einte); (* integer expected *)
      skpspc; (* skip spaces *)
      if not chknxt(')') then prterr(erpe); (* ')' expected *)
      i := temp[top].int; (* get value *)
      temp[top].typ := tstr; (* change to string *)
      putbval(temp[top].bstr, i) (* place value in ascii *)

   end else prterr(eifact)

end; (* factor *)

begin (* term *)

   factor;
   skpspc;
   while ord(chkchr) in [cmult, cdiv, cmod] do begin

      case ord(getchr) of (* tolken *)

         cmult: begin (* * *)

            factor;
            if (temp[top].typ <> tint) or
               (temp[top - 1].typ <> tint) then prterr(ewtyp);
            temp[top - 1].int := temp[top - 1].int * temp[top].int;
            top := top - 1

         end;

         cdiv: begin (* / *)

            factor;
            if (temp[top].typ <> tint) or
               (temp[top - 1].typ <> tint) then prterr(ewtyp);
            temp[top - 1].int := temp[top - 1].int div temp[top].int;
            top := top - 1

         end;

         cmod: begin (* mod *)

            factor;
            if (temp[top].typ <> tint) or
               (temp[top - 1].typ <> tint) then prterr(ewtyp);
            temp[top - 1].int := temp[top - 1].int mod
               temp[top].int;
            top := top - 1

         end

      end;
      skpspc (* skip spaces *)

   end

end; (* term *)

begin (* sexpr *)

   term;
   skpspc;
   while ord(chkchr) in [cadd, csub] do begin

      case ord(getchr) of (* tolken *)

         cadd: begin

            term;
            if temp[top].typ = tstr then begin

               if temp[top - 1].typ <> tstr then prterr(estyp);
               cat(temp[top - 1].bstr, temp[top].bstr);
               top := top - 1

            end else begin

               if temp[top - 1].typ <> tint then prterr(estyp);
               temp[top - 1].int :=
                  temp[top - 1].int + temp[top].int;
               top := top - 1;

            end

         end;

         csub: begin (* - *)

            term;
            if (temp[top].typ <> tint) or
               (temp[top - 1].typ <> tint) then prterr(ewtyp);
            temp[top - 1].int := temp[top - 1].int - temp[top].int;
            top := top - 1

         end

      end;
      skpspc (* skip spaces *)

   end

end; (* sexpr *)

begin (* expr *)

   sexpr; (* parse simple expression *)
   skpspc; (* skip spaces *)
   while ord(chkchr) in [cequ, cnequ, cltn, cgtn, clequ, cgequ] do begin

      case ord(getchr) of (* tolken *)

         cequ: begin

            sexpr;
            if chkequ then begin top := top - 1; settrue end
            else begin top := top - 1; setfalse end

         end;

         cnequ: begin

            sexpr;
            if chkequ then begin top := top - 1; setfalse end
            else begin top := top - 1; settrue end

         end;

         cltn: begin

            sexpr;
            if chkltn then begin top := top - 1; settrue end
            else begin top := top - 1; setfalse end

         end;

         cgtn: begin

            sexpr;
            if chkgtn then begin top := top - 1; settrue end
            else begin top := top - 1; setfalse end

         end;

         clequ: begin

            sexpr;
            if chkgtn then begin top := top - 1; setfalse end
            else begin top := top - 1; settrue end

         end;

         cgequ: begin

            sexpr;
            if chkltn then begin top := top - 1; setfalse end
            else begin top := top - 1; settrue end

         end

     end;
     skpspc (* skip spaces *)

   end

end; (* expr *)

(* process "let" function *)

procedure let;

begin

   skpspc;
   c := getvar;
   if chknxt('$') then begin

      skpspc;
      if not chknxt(chr(cequ)) then
         prterr(eeque);
      expr;
      if temp[top].typ <> tstr then
         prterr(estyp);
      strs[c] := temp[top].bstr;
      top := top - 1

   end else begin

      skpspc;
      if not chknxt(chr(cequ)) then
         prterr(eeque);
      expr;
      if temp[top].typ <> tint then
         prterr(estyp);
      ints[c] := temp[top].int;
      top := top - 1

   end

end;

begin (* stat *)

   skpspc;
   if ord(chkchr) < ord(' ') then begin

      if ord(chkchr) > cbye then prterr(estate);
      case ord(getchr) of (* statement *)

         cinput:  begin

                     skpspc;
                     c := getvar;
                     if chknxt('$') then inpbstr(strs[c])
                     else begin

                        inpstr(s);
                        ints[c] := getval(s)

                     end

                   end;

         cprint:   begin

                      if not chksend and not chknxt(';') then
                         repeat (* list items *)

                         expr;
                         if temp[top].typ = tstr then prtbstr(temp[top].bstr)
                         else write(temp[top].int);
                         top := top - 1;
                         skpspc

                      until not chknxt(','); (* until not ',' *)
                      if not chknxt(';') then writeln

                    end;

         cgoto:     begin

                       prgmc := schlab(getint);
                       goto 1

                    end;

         cif:       begin

                       expr;
                       if temp[top].typ <> tint then
                          prterr(eexmi);
                       if temp[top].int = 0 then begin

                          top := top - 1;
                          (* go next line *)
                          if prgmc > 0 then prgmc := prgmc + 1;
                          goto 1

                       end;
                       top := top - 1;
                       b := chknxt(chr(cthen));
                       stat

                    end;

         crem:      begin

                       if prgmc > 0 then prgmc := prgmc + 1; (* go next line *)
                       goto 1 (* exit line executive *)

                    end;

         cstop:     goto 88;

         crun:      begin clrvar; prgmc := 1; goto 1 end;

         clist:     begin

                       x := 1; (* set default list swath *)
                       y := maxpgm;
                       if not chksend then begin (* list swath is specified *)

                          x := schlab(getint);
                          skpspc;
                          (* check if end line is specified *)
                          if chknxt(',') then y := schlab(getint)

                       end;
                       for x := x to y do (* print specified lines *)
                          if not null(prgm[x]) then (* line exists in buffer *)
                             prtlin(prgm[x]) (* print *)

                    end;

         cnew:      begin clear; goto 88 end;

         clet:      let;

         cbye:      goto 99

      end

   end else let (* default let *)

end; (* stat *)

begin (* exec *)

   linec := 1;

   { prtlin(prgm[prgmc]); } { uncomment for trace }
   while digit(chkchr) do c := getchr; (* skip label *)
   repeat stat until getchr <> ':';
   skpspc;
   if not chkend then prterr(eedlexp); (* should be at line end *)
   if prgmc > 0 then prgmc := prgmc + 1;
   1:

end; (* exec *)

begin (* executive *)

   clear;
   (* initalize keys *)
   keywd[cinput] := 'input     '; keywd[cprint] := 'print     ';
   keywd[cgoto]  := 'goto      '; keywd[cif]    := 'if        ';
   keywd[crem]   := 'rem       '; keywd[cstop]  := 'stop      ';
   keywd[crun]   := 'run       '; keywd[clist]  := 'list      ';
   keywd[cnew]   := 'new       '; keywd[clet]   := 'let       ';
   keywd[cbye]   := 'bye       '; keywd[clequ]  := '<=        ';
   keywd[cgequ]  := '>=        '; keywd[cequ]   := '=         ';
   keywd[cnequ]  := '<>        '; keywd[cltn]   := '<         ';
   keywd[cgtn]   := '>         '; keywd[cadd]   := '+         ';
   keywd[csub]   := '-         '; keywd[cmult]  := '*         ';
   keywd[cdiv]   := '/         '; keywd[cmod]   := 'mod       ';
   keywd[cleft]  := 'left$     '; keywd[cright] := 'right$    ';
   keywd[cmid]   := 'mid$      '; keywd[cthen]  := 'then      ';
   keywd[cstr]   := 'str$      '; keywd[cval]   := 'val       ';
   keywd[cchr]   := 'chr       ';
   writeln;
   writeln('Tiny basic interpreter vs. 0.1 Copyright (C) 1994 S. A. Moore');
   writeln;
   88: while true do begin

      writeln('Ready');
   77: prgmc := 0;
      linec := 1;
      top := 0;
      (* get user lines until non-blank *)
      repeat inpstr(prgm[0]) until not null(prgm[0]);
      keycom(prgm[0]);
      if lint(prgm[0]) > 0 then begin

         enter(prgm[0]);
         goto 77

      end else repeat

         exec;
         if (prgmc > maxpgm) then prgmc := 0
         else if null(prgm[prgmc]) then prgmc := 0

      until prgmc = 0

   end;
   99: writeln

end.
