{$L-,u-}
{******************************************************************************
*                                                                             *
*                              BASIC VS 0.1                                   *
*                                                                             *
*                            8/94 S. A. MOORE                                 *
*                                                                             *
* Implements the full basic language. Provides a full line oriented edit      *
* system, or accepts external text. The full documentation is found           *
* separately. Also see the "basics" or tiny basic companion.                  *
* Basic is implemented as a tolken based interpreter. Each line is full       *
* scan level tolkenized and stored, and may be listed from that form. The     *
* only information lost in this conversion is case, leading and trailing      *
* zeros, and some real number formatting (reals may be reformatted to a       *
* standard representation).                                                   *
*                                                                             *
******************************************************************************}

program basic(input, output);

label 88, 77, 99;

const
 
   maxpwr  = 1000000000; { maximum power of 10 that fits into integer }
   maxnum  = 9999;  { maximum line number }
   maxstr  = 250;   { maximum length of string }
   maxstk  = 100;   { maximum temp count }
   maxexp  = 308;   { maximum exponent of real }
   maxvar  = 255;   { maximum number of variables in program }
   maxvec  = 1000;  { maximum number of vector indexes }
   maxfil  = 100;   { maximum number of files available }
   maxfln  = 250;   { maximum length of filename (must be >= maxstr) }
   intdig  = 11;    { number of digits in integer (to get around SVS bug) }
   hasnfio = true{false}; { implements named file I/O procedures }
 
type    

   string12 = packed array [1..12] of char;   { key }
   bstring  = packed array [1..maxstr] of char; { basic string component }
   bstptr   = ^bstringt; { pointer to basic string }
   bstringt = record { basic string }

                   next: bstptr; { next entry }
                   len : integer; { length }
                   str : packed array [1..maxstr] of char { data }

                end;
   filnam   = packed array [1..maxfln] of char; { filename }
   ext = packed array [1..3] of char; { filename extention }
   { key codes. these codes mark all the objects possible in the
     intermediate code. because of the unterminated nature of
     basic keys, you must list possibly nested strings
     like '<=' and '<') in order of most characters }
   keycod = (cinput,       { input string }
             cprint,       { print }
             cgoto,        { goto line }
             con,          { on..goto }
             cif,          { if conditional }
             cthen,        { then }
             celse,        { else }
             cendif,       { end of if }
             crem,         { remark }
             crema,        { alternate remark (!) }
             cstop,        { stop program }
             crun,         { run program }
             clist,        { list program }
             cnew,         { clear program }
             clet,         { assign variable }
             cload,        { load program }
             csave,        { save program }
             cdata,        { define data }
             cread,        { read data }
             crestore,     { restore data }
             cgosub,       { go subroutine }
             creturn,      { return }
             cfor,         { for }
             cnext,        { next }
             cstep,        { step }
             cto,          { to }
             cwhile,       { while }
             cwend,        { end of while }
             crepeat,      { repeat }
             cuntil,       { until }
             cselect,      { select case }
             ccase,        { case }
             cother,       { other }
             cendsel,      { end select }
             copen,        { open file }
             cclose,       { close file }
             cend,         { end program }
             cdumpv,       { dump variables diagnostic }
             cdumpp,       { dump program diagnostic }
             cdim,         { demension variable }
             cdef,         { define function }
             cfunction,    { define multiline function }
             cendfunc,     { end function }
             cprocedure,   { define multiline procedure }
             cendproc,     { end procedure }
             crand,        { randomize number generator }
             ctrace,       { trace on }
             cnotrace,     { trace off }
             cas,          { as noise word }
             coutput,      { output mode word }
             cbye,         { exit basic }
             cmod,         { mod }
             cidiv,        { div }
             cand,         { and }
             cor,          { or }
             cxor,         { exclusive or }
             cnot,         { not }
             cleft,        { left$ }
             cright,       { right$ }
             cmid,         { mid$ }
             cstr,         { str$ }
             cval,         { val }
             cchr,         { chr$ }
             casc,         { asc }
             clen,         { length of string }
             csqr,         { square }
             cabs,         { absolute value }
             csgn,         { sign of }
             crnd,         { random number }
             cint,         { integer portion }
             csin,         { sine of angle }
             ccos,         { cosine of angle }
             ctan,         { tangent of angle }
             catn,         { artangent }
             clog,         { base e logarithim }
             cexp,         { exponential }
             ctab,         { tab to position }
             cusing,       { using }
             ceof,         { eof }
             clcase,       { lower case string }
             cucase,       { upper case string }
             clequ,        { <= }
             cgequ,        { >= }
             cequ,         { = }
             cnequ,        { <> }
             cltn,         { < }
             cgtn,         { > }
             cadd,         { + }
             csub,         { - }
             cmult,        { * }
             cdiv,         { / }
             cexpn,        { ^ }
             cscn,         { ; }
             ccln,         { : }
             clpar,        { ( }
             crpar,        { ) }
             ccma,         { , }
             cpnd,         { # }
             cperiod,      { . }
             cintc,        { integer constant }
             cstrc,        { string constant }
             crlc,         { real constant }
             cintv,        { integer variable }
             cstrv,        { string variable }
             crlv,         { real variable }
             cspc,         { single space }
             cspcs,        { multiple spaces }
             cpend,        { program end }
             clend);       { line end }
   vartyp = (vtint, vtrl, vtstr); { variable type }
   { control entry type }
   ctltyp = (ctif, ctfor, ctgosub, ctwhile, ctrepeat, ctselect);
   ctlptr = ^ctlrec; { pointer to control record }
   ctlrec = record { control stack record }

       next:  ctlptr;  { next entry }
       typ:   ctltyp;  { type }
       line:  bstptr;  { line position }
       chrp:  integer; { character position }
       vtyp:  vartyp;  { 'for' variable type }
       vinx:  char;    { 'for' variable index }
       endi:  integer; { end value for integer }
       endr:  real;    { end value for integer }
       stepi: integer; { step value for integer }
       stepr: real;    { step value for real }
       sov:   boolean; { sequence of values flag }
       sif:   boolean  { single line 'if' }

   end;
   vecinx = 0..maxvec; { index for vectors }
   vvctyp = (vvint, vvrl, vvstr, vvvec); { vector types }
   vvcptr = ^varvec; { variables vector pointer }
   varvec = record { variables vector }

      next: vvcptr; { next list entry }
      inx:  integer; { number of index levels }
      case vt: vvctyp of { vector type }

         vvint: (int: array [vecinx] of integer); { integer values }
         vvrl:  (rl:  array [vecinx] of real);    { real values }
         vvstr: (str: array [vecinx] of bstptr);  { string values }
         vvvec: (vec: array [vecinx] of vvcptr)   { vector values }

      { end }

   end;   
   varinx = 0..maxvar; { index for variables table } 
   varptr = ^varety; { pointer to variable entry }
   varety = record { variable }

      next:  varptr;   { next entry }
      typ:   vartyp;   { type (where important) }
      ref:   integer;  { reference count }
      nam:   string12; { name of variable }
      str:   bstptr;   { value as string }
      int:   integer;  { value as integer }
      rl:    real;     { value as real }
      strv:  vvcptr;   { value as string vector }
      intv:  vvcptr;   { value as integer vector }
      rlv:   vvcptr;   { value as real vector }
      rec:   varptr;   { record element list }
      fnc:   boolean;  { variable denotes function }
      prc:   boolean;  { variable denotes procedure }
      ml:    boolean;  { multiline function }
      par:   varptr;   { parameter list }
      line:  bstptr;   { line position of function }
      chrp:  integer;  { character position of function }
      inx:   varinx;   { index of original parameter variable }
      sys:   boolean;  { is a system variable }
      gto:   boolean;  { is a goto label }
      glin:  bstptr;   { line position of goto }

   end;
   fnsptr = ^fnsrec; { function suspend entry pointer }
   fnsrec = record

      vs:   varptr;  { variable save list }
      lin:  bstptr;  { position save }
      chr:  integer;
      endf: boolean; { end of function was found }
      next: fnsptr   { next list entry }

   end;
   frcptr = ^frcety; { pointer to file record }
   frcety = record { file record }

      st:   (stclose, stopenrd, stopenwr); { current status }
      cp:   integer; { character number on current line }
      f:    text; { read or write file }
      ty:   (tyinp, tyout, tyoth); { input, output, other file }
      next: frcptr; { next list }

   end;
   filinx = 1..maxfil; { index for files table }
   filtbl = array [filinx] of frcptr; { table of files }
   tmprec = record { temp stack entry }

      typ:  (tint, tstr, trl);
      int:  integer;
      bstr: bstringt;
      rl:   real

   end;
   tmptbl = array [1..maxstk] of tmprec; { table of temps for stack }
   varttp = array [1..maxvar] of varptr; { table of variables }
   { error codes }
   errcod     = (eitp, estate, eexmi, eeque, estyp, epbful, eiovf, evare,
                 elabnf, einte, econv, elntl, ewtyp, erpe, eexc, emqu, 
                 eifact, elintl, estrovf, eedlexp, elpe, ecmaexp, estre,
                 estrinx, ecodtl, einvchr, ethnexp, eoutdat, erdtyp, 
                 ecstexp, edbr, enovf, erfmt, eexpovf, enfmt, erlexp,
                 eunimp, enoret, enofor, etoexp, etostr, ecmscexp, 
                 enumexp, efnfnd, egtgsexp, eedwhexp, emsgnxt, enowhil,
                 enorpt, enosel, evarovf, elabtl, edim, esubrng, earddim,
                 edimtl, edupdef, epartyp, efncret, eparnum, echrrng, 
                 esccmexp, eopnfil, ecurpos, eclrval, escnval, einsinp,
                 encstexp, eforexp, efmdexp, easexp, epndexp, einvfnum,
                 esysfnum, einvmod, efilnop, epmtfil, efilnfd, eeofenc,
                 enulusg, efmtnf, ebadfmt, einvexp, ezerdiv, enoafor,
                 einvimm, enoif, emismif, ecasmat, enfact, enoendf,
                 enpact, enoendp, eprctyp, einvfld, etypfld, esysvar,
                 eglbdef, elabimm, eevtexp, eeleiexp, enonfio, ebolneg,
                 esyserr);
 
var
     
   keywd:  array [keycod] of string12; { keywords }
   temp:   tmptbl; { temp stack }
   top:    integer; { current temps top }
   linec:  integer; { character position }
   linecs: integer; { character position save }
   linbuf: bstptr; { input buffer for program loads }
   ktrans: array[char] of keycod; { keycode translation array }
   ki:     keycod; { index for above }
   datac:  bstptr; { data read counter }
   datal:  integer; { data read character }
   ctlstk: ctlptr;  { controls stack }
   ctlfre: ctlptr;  { free controls }
   rndseq: integer; { random number seed }
   rndsav: integer; { random number save }
   { increasingly a misnomer, the variables table is the central names table,
     and multiple interpretations of each name (overloading) is possible }
   vartbl: varttp; { variables table }
   varfre: varptr;  { free variables list }
   vi:     varinx; { index for variables table }
   {vp:     varptr;} { pointer for variables }
   strfre: bstptr; { free strings pointer }
   vecfre: vvcptr; { free vectors pointer }
   prglst: bstptr; { program source list }
   immprg: bstptr; { program immediate line pointer }
   curprg: bstptr; { currently executing program line }
   newlin: boolean; { printing on new line }
   trace:  boolean; { trace execution flag }
   source: text; { source file for reading programs }
   fsrcop: boolean; { source file is open flag }
   filtab: filtbl; { files in use array }
   fi:     filinx; { index for files table }
   filfre: frcptr; { free file entries }
   fnsstk: fnsptr; { function context stack }
   fnsfre: fnsptr; { free function context stack }
   i:     varinx; { index for variables table }
   keycodc: array [0..255] of keycod; { keycod converter array }

procedure prterr(err: errcod); forward;

{******************************************************************************

The following functions allow for file access by name in supported systems.
Leaving them as errors allows compilation on ISO 7185 systems, but without
file I/O based functions.

******************************************************************************}

{******************************************************************************

Check file exists

Checks if the given file exists with right padded filename. Used to wrap the
operating system function.

******************************************************************************}

function existsfile(var fn: filnam): boolean;
    
{
var f: bindable text;
    b: bindingtype;
    i, l: integer;
    fe: boolean;
}

begin
  
   { GPC }

   {
   unbind(f);
   b := binding(f);
   l := maxstr;
   while (fn[l] = ' ') and (l > 1) do l := l-1;
   b.name := '';
   for i := 1 to l do b.name := b.name+fn[i];
   bind(f, b);
   fe := b.bound;
   unbind(f);
   existsfile := fe;
   }

   fn[1] := fn[1]; { shut up compiler }
   existsfile := true
   
end;

{******************************************************************************

Open file for reading

Opens a given file by name for reading.

******************************************************************************}

procedure openread(var f: text; var fn: filnam);

{
var s: string(maxstr);
    i, l: integer;
}

begin

   { GPC/FPC/Borland }

   {
   l := maxstr;
   while (fn[l] = ' ') and (l > 1) do l := l-1;
   s := '';
   for i := 1 to l do s := s+fn[i];
   assign(f, s);
   reset(f)
   }
   
   fn[1] := fn[1]; { shut up compiler }
   reset(f)

end;

{******************************************************************************

Open file for writing

Opens a given file by name for writing.

******************************************************************************}

procedure openwrite(var f: text; var fn: filnam);

{
var s: string(maxstr);
    i, l: integer;
}

begin

   { GPC }

   {   
   l := maxstr;
   while (fn[l] = ' ') and (l > 1) do l := l-1;
   s := '';
   for i := 1 to l do s := s+fn[i];
   assign(f, s);
   rewrite(f)
   }
   
   fn[1] := fn[1]; { shut up compiler }
   reset(f)

end;

{******************************************************************************

Close file

Closes the given file so it can be reused.

******************************************************************************}

procedure closefile(var f: text);

begin

   { GPC/FPC/Borland/IP Pascal }
   
   {
   close(f)
   }
   
   reset(f); { shut up compiler }
   
end;

{******************************************************************************

End of named file functions.

******************************************************************************}

{******************************************************************************

Bitwise integer instructions. These can be replaced by direct operations if 
your Pascal installation has them.

******************************************************************************}

{******************************************************************************

Find bit 'not' of integer.

Finds bit 'not' of an integer. Only works on positive integers.

******************************************************************************}

function bnot(a: integer): integer;
 
var i, r, p: integer;
 
begin
 
   r := 0; { clear result }
   p := 1; { set 1st power }
   i := maxint; { set maximium positive number }
   while i <> 0 do begin
 
      if not odd(a) then r := r+p; { add in power }
          a := a div 2; { set next bits of operands }
          i := i div 2; { count bits }
      if i > 0 then p := p*2; { find next power }
 
   end;
   bnot := r { return result }
 
end;

{******************************************************************************

Find bit 'or' of integers.

Finds bit 'or' of two integers. Only works on positive integers.

******************************************************************************}

function bor(a, b: integer): integer;
 
var i, r, p: integer;
 
begin
 
   r := 0; { clear result }
   p := 1; { set 1st power }
   i := maxint; { set maximium positive number }
   while i <> 0 do begin
 
      if odd(a) or odd(b) then r := r+p; { add in power }
          a := a div 2; { set next bits of operands }
          b := b div 2;
          i := i div 2; { count bits }
      if i > 0 then p := p*2; { find next power }
 
   end;
   bor := r { return result }
 
end;

{******************************************************************************

Find bit 'and' of integers.

Finds bit 'and' of two integers. Only works on positive integers.

******************************************************************************}

function band(a, b: integer): integer;
 
var i, r, p: integer;
 
begin
 
   r := 0; { clear result }
   p := 1; { set 1st power }
   i := maxint; { set maximium positive number }
   while i <> 0 do begin
 
      if odd(a) and odd(b) then r := r+p; { add in power }
          a := a div 2; { set next bits of operands }
          b := b div 2;
          i := i div 2; { count bits }
      if i > 0 then p := p*2; { find next power }
 
   end;
   band := r { return result }
 
end;

{******************************************************************************

Find bit 'xor' of integers.

Finds bit 'xor' of two integers. Only works on positive integers.

******************************************************************************}

function bxor(a, b: integer): integer;
 
var i, r, p: integer;
 
begin
 
   r := 0; { clear result }
   p := 1; { set 1st power }
   i := maxint; { set maximium positive number }
   while i <> 0 do begin
 
      if odd(a) <> odd(b) then r := r+p; { add in power }
          a := a div 2; { set next bits of operands }
          b := b div 2;
          i := i div 2; { count bits }
      if i > 0 then p := p*2; { find next power }
 
   end;
   bxor := r { return result }
 
end;

{******************************************************************************

End of bitwise integer operations.

******************************************************************************}

{******************************************************************************

Find lower case character

Finds the lower case equivalent of a character.

******************************************************************************}

function lcase(c: char): char;

begin

   if c in ['A'..'Z'] then c := chr(ord(c)-ord('A')+ord('a'));
   lcase := c
  
 end;

{******************************************************************************

Get a vector

Gets a vector, either allocating it anew, or recovering one from the
free vectors list.

******************************************************************************}

procedure getvec(var vp: vvcptr); { string return }

begin

   if vecfre <> nil then begin { recover used entry }

      vp := vecfre; { index top of stack }
      vecfre := vecfre^.next { gap from list }

   end else new(vp); { else get a new entry }
   vp^.next := nil; { clear next }
   vp^.inx := 0; { clear index }
   vp^.vt := vvint { set integer }

end;      

{******************************************************************************

Put vector

Releases the vector to free storage.

******************************************************************************}

procedure putvec(vp: vvcptr); { string to release }

begin

   vp^.next := vecfre; { insert to free list }
   vecfre := vp

end;      

{******************************************************************************

Get file entry

Gets a file entry, either from new or from free files list.

******************************************************************************}

procedure getfil(var fp: frcptr);

begin

   if filfre <> nil then begin { recover used entry }

      fp := filfre; { index top entry }
      filfre := filfre^.next { gap from list }

   end else new(fp); { get new entry }
   fp^.st := stclose; { set closed }
   fp^.ty := tyoth; { default to not input or output }
   fp^.cp := 1; { set current character }
   fp^.next := nil { clear next }

end;

{******************************************************************************

Put file entry

Releases the given file entry to the free list.

******************************************************************************}

procedure putfil(fp: frcptr);

begin

   fp^.next := filfre; { insert to free list }
   filfre := fp

end;

{******************************************************************************

Get a basic string

Gets a basic string, either allocating it anew, or recovering one from the
free strings list.

******************************************************************************}

procedure getstr(var sp: bstptr); { string return }

begin

   if strfre <> nil then begin { recover used entry }

      sp := strfre; { index top of stack }
      strfre := strfre^.next { gap from list }

   end else new(sp); { else get a new entry }
   sp^.next := nil; { clear next }
   sp^.len := 0 { set 0 length string }

end;      

{******************************************************************************

Put string

Releases the string entry to free storage.

******************************************************************************}

procedure putstr(sp: bstptr); { string to release }

begin

   sp^.next := strfre; { insert to free list }
   strfre := sp

end;      

{******************************************************************************

Push new control stack entry

Gets a new control stack record and places that on the control stack.

******************************************************************************}

procedure pshctl;

var cp: ctlptr; { control pointer entry }

begin

   if ctlfre <> nil then begin { recover used entry }

      cp := ctlfre; { index top of stack }
      ctlfre := ctlfre^.next { gap from list }

   end else new(cp); { else get a new entry }
   cp^.next := ctlstk; { push onto control stack }
   ctlstk := cp

end;      

{******************************************************************************

Pop control stack

Removes the top entry from the control stack and places it into free storage.

******************************************************************************}

procedure popctl;

var cp: ctlptr; { control pointer entry }

begin

   cp := ctlstk; { index top of stack }
   ctlstk := ctlstk^.next; { gap from list }
   cp^.next := ctlfre; { insert to free list }
   ctlfre := cp

end;      

{******************************************************************************

Purge control stack

Removes all entries from the control stack and places them into free storage.

******************************************************************************}

procedure prgctl;

begin

   while ctlstk <> nil do popctl { remove all entries }

end;      

{******************************************************************************

Find stacked type

Removes all entries from the control stack until the top one matches the
requested type. If the requested type is NOT found, the stack is simply
emptied, which should be checked for, and a proper error executed.

******************************************************************************}

procedure fndctl(ct: ctltyp); { control type to search for }

var cp: ctlptr; { control pointer entry }

begin

   cp := ctlstk; { index top of stack }
   while cp <> nil do begin { search entries }

      if cp^.typ = ct then cp := nil { found, terminate }
      else begin { purge an entry }

         popctl; { purge top entry }
         cp := ctlstk { index that }

      end

   end

end;      

{******************************************************************************

Cancel single line 'if's

Checks if the top of the stack is a single line 'if', and removes that if so.
Used to drop a single line 'if' when the current line is finished. Repeats
for all such 'if's.

******************************************************************************}

procedure cansif;

var fnd: boolean; { found a single line if }

begin

   repeat { for all single line 'if's stacked }

      fnd := false; { set no single line 'if's found }
      if ctlstk <> nil then { there is stack contents }
         if ctlstk^.typ = ctif then { its an if }
            if ctlstk^.sif then begin

         popctl; { remove top of stack on single line 'if' }
         fnd := true { set one was found }

      end

   until not fnd { until no more }

end;      

{******************************************************************************

Append file extention

Appends a given extention, in place, to the given file name. The extention is 
usually in the form: 'ext'. The extention is placed within the file name at the
first space or period from the left hand side. This allows extention of either
an unextended filename or an extended one (in which case the new extention
simply overlays the old). The overlay is controlled via flag: if overwrite is
true, the extention will overwrite any existing, if not, any existing extention
will be left in place.
No checking is performed for a new filename that will overflow the allotted
filename length.
In the case of overflow, the filename will simply be truncated to the 8:3 
format.
Note: this routine is MS-DOS dependent.

******************************************************************************}

procedure addext(var fn: bstringt; { filename to extend }
                 e: ext);           { filename extention }

var i : 1..maxstr; { filename index }
    x : 1..3; { extention index }
    ep: boolean; { extention present flag }

begin

   { check already has an extention }
   ep := false; { set no extention }
   for i := 1 to fn.len do if fn.str[i] = '.' then ep := true; { found }
   if not ep then begin { add extention }

      fn.len := fn.len+1; { add '.' }
      fn.str[fn.len] := '.';
      for x := 1 to 3 do if e[x] <> ' ' then begin { add extention }

         fn.len := fn.len+1; { add character }
         fn.str[fn.len] := e[x]

      end

   end

end;

{******************************************************************************

Read encoded integer

Reads an integer out of the given string, in big endian, signed magnitude
format.

******************************************************************************}

procedure dcdint(var str: bstringt; { string to read from }
                 var i:   integer;  { position to read from }
                 var v:   integer); { integer to read }

var s: integer; { sign of result }
    b: 0..255;  { read holder }
    t: integer; { temp for SVS bug }

begin

   s := 1; { set positive }
   b := ord(str.str[i]); { get high byte }
   i := i+1; { next }
   { check signed, and set if so }
   if b > 127 then begin s := -1; b := b -128 end;
   t := b; { place in buffer (this is stupid, and non-standard SVS) }
   v := t*16777216;
   b := ord(str.str[i]); { get high mid byte }
   i := i+1; { next }
   t := b;
   v := v+t*65536;
   b := ord(str.str[i]); { get low mid byte }
   i := i+1; { next }
   t := b;
   v := v+t*256;
   b := ord(str.str[i]); { get low byte }
   i := i+1; { next }
   v := v+b

end;

{******************************************************************************

Read encoded real

Reads a real out of the given string.

******************************************************************************}

procedure dcdrl(var str: bstringt; { string to read from }
                var i:   integer;  { position to read from }
                var fv:  real);    { integer to read }

var fc:  record case boolean of { float convertion }

            false: (r: real);
            true:  (c: packed array [1..8] of char)

         end; 
    fci: 1..8; { index for same }

begin

   for fci := 1 to 8 do begin { get real bytes }

      fc.c[fci] := str.str[i]; { get byte }
      i := i+1 { next }

   end;
   fv := fc.r { return result }

end;

{******************************************************************************

Print character

Prints a single character, and counts characters output, allowing tabbing on
output. Since only a single tab counter is used, use of the output counter
is only reliable when outputting to the console.

******************************************************************************}

procedure prtchr(var fr: frcety; c: char);

begin

   if fr.ty = tyout then write(c) { output file }
   else write(fr.f, c); { output character }
   fr.cp := fr.cp+1 { count tab positions }

end;

{******************************************************************************

Reset tabbing

Resets the character counter. Used when a new line is output.

******************************************************************************}

procedure rsttab(var fr: frcety);

begin

   fr.cp := 1 { clear tab counter }

end;

{******************************************************************************

Tab to position

Tabs to the given position on a line.

******************************************************************************}

procedure tab(var fr: frcety; n: integer);

begin

   while n > fr.cp do prtchr(fr, ' ') { space till tab position }

end;

{******************************************************************************

Put real in string

Outputs a real in "economy" format. The accuracy of the output is limited to
the number of whole ('0'..'9') digits that will fit into an integer, which may
not be enough to contain all the accuracy of a real.
The number is printed in the smallest number of characters possible, with
leading or trailing zeros, or trailing decimal point removed.
If possible, the decimal point is placed in the number and the exponent not
printed. Otherwise, the number will be output in the form:

    digit.digit...ex

If the field width specified is larger than the total characters in the number,
then trailing spaces will be output to fill.
The number is placed in the given string.
Note: a really stupid algorithim is used to scale and find the exponent, which
should be improved for speed and accuracy reasons.

******************************************************************************}

procedure putrl(var s:     bstringt; { string to output to }
                    r:     real;     { number to print }
                    field: integer); { field to print in }

const maxnum =  999999999; { maximum number usable in integer }
      maxpwr =  100000000; { maximum power of 10 for integer }

var e: integer; { exponent }
    m: integer; { mantissa }
    p: integer; { power holder }
    digits: array [1..9] of char; { digits save }
    i, ld: 1..9; { indexes for digits }
    leading: boolean; { leading zero output }
    c: char; { character holder }
    dec: 1..10; { decimal point position }
    cnt: integer; { number of characters output }

{ place character in string }

procedure putchr(c: char);

begin

   if s.len >= maxstr then prterr(estrovf); { string overflow }
   s.len := s.len+1; { add character }
   s.str[s.len] := c { place character }

end;

begin

   s.len := 0; { clear output string }
   if r = 0.0 then putchr('0') { number is zero }
   else begin { output number }

      if r < 0.0 then putchr('-'); { set sign of number }
      r := abs(r); { find signless real }
      e := 8; { set our "pseudo" exponent }
      { place number so that it just fits in an integer }
      while (r < maxnum) and (e >= -308) do begin r := r*10.0; e := e-1 end;
      while (r > maxnum) and (e <= 308) do begin r := r/10.0; e := e+1 end;
      { check invalid exponent }
      if abs(e) > 308 then prterr(einvexp); { invalid real number }
      p := maxpwr; { set maximum power }
      m := round(r); { get mantissa }
      { make sure that we did not round over limit }
      if m > maxnum then begin m := m div 10; e := e+1 end;
      for i := 1 to 9 do begin { get digits }
   
         digits[i] := chr(m div p+ord('0')); { place digit }
         m := m mod p; { remove that digit }
         p := p div 10 { next digit }
   
      end;
      ld := 9; { find last digit }
      while (ld > 1) and (digits[ld] = '0') do ld := ld-1;
      dec := 2; { set default decimal position }
      { see if we can represent the exponent by placing the decimal point
        within the number }
      if (e >= -1) and (e <= 8) then begin dec := e+2; e := 0 end;
      if dec-1 > ld then ld := dec-1; { increase digit output as required }
      for i := 1 to ld do begin { write digits of number }
   
         { if this is the decimal position, output } 
         if i = dec then putchr('.');
         putchr(digits[i]) { output digit }
   
      end;
      if e <> 0 then begin { output exponent }
   
         putchr('e'); { output exponent marker }
         if e >= 0 then putchr('+') else putchr('-');
         e := abs(e); { find signless exponent }
         p := 100; { set maximum power }
         leading := false; { set no leading digit output }
         while p <> 0 do begin { print digits }
   
            c := chr(e div p+ord('0')); { get digit }
            if leading or (c <> '0') then begin 

               { non-zero or leading was output }
               putchr(c); { output digit }
               leading := true { set leading digit output }
   
            end;
            e := e mod p; { remove that digit }
            p := p div 10 { next digit }
   
         end
   
      end

   end;
   cnt := field-s.len; { find remaining field to output }
   while cnt > 0 do begin putchr(' '); cnt := cnt-1 end { output }

end;

{******************************************************************************

Print basic string 

Outputs the given string to the console, without an eoln.

******************************************************************************}
 
procedure prtbstr(var fr:   frcety;    { file to output to }
                  var bstr: bstringt); { string to output }
 
var i: integer;
 
begin

   for i := 1 to bstr.len do prtchr(fr, bstr.str[i]);

end;

{******************************************************************************

Print real

Prints the given real with the given width to the file.

******************************************************************************}

procedure prtrl(var fr:    frcety;   { file to output to }
                    r:     real;     { number to print }
                    field: integer); { field to print in }

var ts: bstringt; { temp string }

begin

   putrl(ts, r, field); { place number in buffer }
   prtbstr(fr, ts) { print }

end;

{******************************************************************************

Place integer to string

Converts the given integer to ascii and places it into the given string.

******************************************************************************}

procedure putbval(var str: bstringt; { string to place result }
                      v:   integer;  { value to convert }
                      f:   integer); { field width }

var p:       integer; { power holder }
    leading: boolean; { leading zero output }

begin

   str.len := 0; { clear result string }
   p := maxpwr; { set maximum power }
   if v < 0 then begin

      str.len := str.len+1; { add character }
      str.str[1] := '-' { place minus sign }

   end;
   v := abs(v); { find signless value }
   leading := false; { set no leading digit output }
   while p <> 0 do begin { fit powers }

      if ((v div p) <> 0) or leading or (p = 1) then begin

         { non-zero, or leading already output, or last digit }
         str.len := str.len+1; { add character }
         str.str[str.len] := chr(v div p+ord('0')); { place digit }
         leading := true { set leading digit output }

      end;
      v := v mod p; { remove from value }
      p := p div 10 { find next power }

   end;
   while str.len < f do begin { pad field }

      str.len := str.len+1; { next character }
      str.str[str.len] := ' '

   end

end;

{******************************************************************************

Print integer

Prints an integer in left justified mode.

******************************************************************************}

procedure prtint(var fr:    frcety;   { file to print to }
                     v:     integer;  { integer to print }
                     field: integer); { field to print in }

var ts: bstringt; { output buffer }

begin

   putbval(ts, v, field); { place number in buffer }
   prtbstr(fr, ts) { print }

end;

{******************************************************************************

Print label

Prints the non-space characters in a label.

******************************************************************************}

procedure prtlab(var f: text;        { file to print to }
                 var lab: string12); { label to print }

var li: 1..12; { index for label }

begin

   { output non-space }
   for li := 1 to 12 do if lab[li] <> ' ' then write(f, lab[li])

end;

{******************************************************************************

Print tolkenized line

Expands each tolken on the given line and prints it in the original, or close
to the original, form.

******************************************************************************}
 
procedure prtlin(var f: text; var str: bstptr);
 
var i, j: integer;
    k:    keycod; { key }
    v:    integer; { integer value holder }
    fv:   real; { real value holder }
 
begin { prtlin }

   if not newlin then writeln; { if not at line start, force it }
   newlin := true;
   i := 1; { index 1st tolken }
   while str^.str[i] <> chr(ord(clend)) do begin { while not end of line }

      k := ktrans[str^.str[i]]; { find next key }
      i := i+1; { next }
      case k of { tolken }

         { print keywords and printing tolkens }
         cinput, cprint, cgoto, con, cif, cthen, cendif, celse, cstop, crun,
         clist, cnew, clet, cload, csave, cdata, cread, crestore,
         cgosub, creturn, cfor, cnext, cstep, cto, cwhile, cwend, crepeat,
         cuntil, cselect, ccase, cother, cendsel, copen, cclose, cend, cdumpv,
         cdumpp, cdim, cdef, cfunction, cendfunc, cprocedure, cendproc, crand,
         ctrace, cnotrace, cas, coutput, cbye, clequ, cgequ, cequ, cnequ, cltn, 
         cgtn, cadd, csub, cmult, cdiv, cexpn, cmod, cidiv, cand, cor, cxor, cnot, 
         cleft, cright, cmid, cstr, cval, cchr, casc, clen, csqr, cabs, csgn, crnd, cint, 
         csin, ccos, ctan, catn, clog, cexp, ctab, cusing, ceof, clcase, cucase,
         cscn, ccln, clpar, crpar, ccma,
         cpnd, cperiod: for j := 1 to 12 do { print key characters }
                  if keywd[k][j] <> ' ' then { non-space }
                     write(f, keywd[k][j]); { print }
         cintc: begin dcdint(str^, i, v);
                      write(f, v:1) end; { integer constant }
         crlc: begin dcdrl(str^, i, fv);
                     prtrl(filtab[2]^, fv, 1) end; { real constant }
         cstrc: begin { string constant }

            write(f, '"');
            for j := 1 to ord(str^.str[i]) do begin { print string characters }

               i := i+1; { next character }
               write(f, str^.str[i]); { write character }
               { if it is '"', write again to create quote image }
               if str^.str[i] = '"' then write(f, '"')

            end;
            write(f, '"');
            i := i+1

         end;
         cintv: begin { integer variable }
            
            prtlab(f, vartbl[ord(str^.str[i])]^.nam); { print label }
            write(f, '%'); { print '%' (integer) type marker }
            i := i+1 { next }
   
         end;
         cstrv: begin { string variable }

            prtlab(f, vartbl[ord(str^.str[i])]^.nam); { print label }
            write(f, '$'); { print '$' (string) type marker }
            i := i+1 { next }

         end;
         crlv:  begin { real variable }

            prtlab(f, vartbl[ord(str^.str[i])]^.nam); { print label }
            i := i+1 { next }

         end;
         cspc:  write(f, ' '); { single space }
         cspcs: { multiple spaces }
            begin for j := 1 to ord(str^.str[i]) do write(f, ' '); i := i+1 end;
         crem, crema:  begin { rem, skip rem'ed characters }

            for j := 1 to 12 do { print key characters }
               if keywd[k][j] <> ' ' then write(f, keywd[k][j]); { print }
            for j := 1 to ord(str^.str[i]) do begin { print rem characters }

               i := i+1; { next character }
               write(f, str^.str[i]) { write character }

            end;
            i := i+1 { skip last }

         end;
         clend: { line end }

      end
      
   end;
   writeln(f)

end; { prtlin }

{******************************************************************************

Print error

If the program counter is other than 0, which means that we are executing from
within the program (and not from the immediate command line), we print the
offending line. Then, the error code is printed.
This routine should print the standard "^" bit, but needs to be adjusted by
tolken output counts.

******************************************************************************}
 
procedure prterr {(err: errcod)};
 
var li: integer;

begin

   if not newlin then writeln; { if not at line start, force it }
   newlin := true;
   if linbuf <> nil then begin { print raw ascii }
      
      { this mode is provided for when loading files, which, unlike immediate
        lines, must have errors printed out }
      for li := 1 to linbuf^.len do write(linbuf^.str[li]); { print line }
      writeln; { terminate }
      { since we won't be returning, we release the line buffer here }
      putstr(linbuf); { release line buffer }
      linbuf := nil { flag clear }

   end else if (curprg <> immprg) and (curprg <> nil) then 
      { not immediate line, and line valid }
      prtlin(output, curprg); { print line }
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
      estrinx:  writeln('String indexing error');
      ecodtl:   writeln('Line to large to encode');
      einvchr:  writeln('Invalid character in line');
      ethnexp:  writeln('''then'' expected');
      eoutdat:  writeln('Out of data');
      erdtyp:   writeln('Next data does not match variable type');
      ecstexp:  writeln('Constant expected');
      edbr:     writeln('Digit beyond radix');
      enovf:    writeln('Numeric overflow');
      erfmt:    writeln('Invalid real format');
      eexpovf:  writeln('Exponent overflow');
      enfmt:    writeln('Invalid numeric format');
      erlexp:   writeln('Real expected');
      eunimp:   writeln('Unimplemented command');
      enoret:   writeln('Nothing to return to');
      enofor:   writeln('No matching "for" found');
      etoexp:   writeln('"to" expected');
      etostr:   writeln('"to"/"step" cannot be applied to string');
      ecmscexp: writeln('","/";" expected');
      enumexp:  writeln('Expression must be numeric');
      efnfnd:   writeln('File not found');
      egtgsexp: writeln('''goto''/''gosub'' expected');
      eedwhexp: writeln('"wend" expected');
      emsgnxt:  writeln('No matching "next" found');
      enowhil:  writeln('No matching "while" found');
      enorpt:   writeln('No matching "repeat" found');
      enosel:   writeln('No matching "select" found');
      evarovf:  writeln('Too many variables in program');
      elabtl:   writeln('Label to long');
      edim:     writeln('Wrong number of demensions');
      esubrng:  writeln('Subscript out of range');
      earddim:  writeln('Variable already demensioned');
      edimtl:   writeln('Demension too large');
      edupdef:  writeln('Duplicate function definition');
      epartyp:  writeln('Parameter does not match declared type');
      efncret:  writeln('Function return does not match declared type');
      eparnum:  begin

         write('Number of parameters does not match function ');
         writeln('declaration')

      end;
      echrrng:  writeln('Value out of range for character');
      esccmexp: writeln('";" or "," expected');
      eopnfil:  writeln('Cannot open file');
      ecurpos:  writeln('Invalid cursor position');
      eclrval:  writeln('Invalid color code');
      escnval:  writeln('Invalid screen number');
      einsinp:  writeln('Insufficient input');
      encstexp: writeln('Numeric constant expected');
      eforexp:  writeln('"for" expected');
      efmdexp:  writeln('File mode expected');
      easexp:   writeln('"as" expected');
      epndexp:  writeln('"#" expected');
      einvfnum: writeln('Invalid file number');
      esysfnum: writeln('System file number cannot be opened or closed');
      einvmod:  writeln('File mode inappropriate for operation');
      efilnop:  writeln('File is not open');
      epmtfil:  writeln('Prompt not allowed with file');
      efilnfd:  writeln('File not found');
      eeofenc:  writeln('End of file encountered');
      enulusg:  writeln('End of format in "using"');
      efmtnf:   writeln('No valid format found in "using" string');
      ebadfmt:  writeln('Invalid "using" format specification');
      einvexp:  writeln('Invalid exponent on real');
      ezerdiv:  writeln('Divide by zero');
      enoafor:  writeln('No "for" is active');
      einvimm:  writeln('Statement invalid as immediate');
      enoif:    writeln('No "if" is active');
      emismif:  writeln('Mismatched "if"s');
      ecasmat:  writeln('Case expression does not match type of selector');
      enfact:   writeln('No function active');
      enoendf:  writeln('No "endfunc" found');
      enpact:   writeln('No procedure active');
      enoendp:  writeln('No "endproc" found');
      eprctyp:  writeln('Procedure must not have type');
      einvfld:  writeln('Invalid record field specification');
      etypfld:  writeln('Record cannot have type');
      esysvar:  writeln('Cannot modify system variable');
      eglbdef:  writeln('Goto label references multiple positions');
      elabimm:  writeln('Goto label cannot appear in immediate line');
      eevtexp:  writeln('"nextevent" variable expected');
      eeleiexp: writeln('"else" or "endif" expected');
      enonfio:  writeln('No named file I/O implemented this version');
      ebolneg:  writeln('Cannot use bit operation on negative');
      esyserr:  writeln('System error: contact program vendor');
 
   end;
   if fsrcop then closefile(source); { close source file }
   goto 88 { loop to ready }

end;

{******************************************************************************

Get new variable entry pointer

Gets a new variable entry to the specified pointer.

******************************************************************************}

procedure getvarp(var vp: varptr); { variable pointer }

begin

   if varfre <> nil then begin { recover used entry }

      vp := varfre; { index top of stack }
      varfre := varfre^.next { gap from list }

   end else new(vp); { else get a new entry }
   vp^.next := nil; { clear next/function list }
   vp^.typ  := vtint; { default to integer }
   vp^.ref  := 1; { set 1st reference }
   vp^.nam  := '            '; { clear name }
   vp^.str  := nil; { set no string allocated }
   vp^.int  := 0; { clear integer }
   vp^.rl   := 0.0; { clear real }
   vp^.strv := nil; { set no string vector allocated }
   vp^.intv := nil; { set no integer vector allocated }
   vp^.rlv  := nil; { set no real vector allocated }
   vp^.rec  := nil; { set no record elements }
   vp^.fnc  := false; { set not function }
   vp^.prc  := false; { set not procedure }
   vp^.ml   := false; { set not multiline }
   vp^.par  := nil; { clear parameter list }
   vp^.line := nil; { clear position }
   vp^.chrp := 0;
   vp^.inx  := 0; { clear variable link }
   vp^.sys  := false; { set not system }
   vp^.gto  := false; { set not goto }

end;      

{******************************************************************************

Release variable pointer

Removes the given variable from the variables list, and places it onto the free
list.

******************************************************************************}

procedure putvarp(vp: varptr); { variable to free }

begin

   vp^.next := varfre; { insert to free list }
   varfre := vp
   
end;      

{******************************************************************************

Get new variable entry

Gets a new variable entry, and places that at the first free variable entry.
If there are no more variable entries available, an error is returned.

******************************************************************************}

procedure getvar(var vi: varinx); { index of resulting variable }

begin

   vi := 1; { set 1st variable entry }
   { search for first free entry }
   while (vartbl[vi] <> nil) and (vi < maxvar) do vi := vi+1;
   if vartbl[vi] <> nil then prterr(evarovf); { variables overflow }
   getvarp(vartbl[vi]) { get a variable entry }

end;      

{******************************************************************************

Release variable

Removes the given variable from the variables list, and places it onto the free
list.

******************************************************************************}

procedure putvar(vi: varinx); { variable to free }

begin

   if vartbl[vi] <> nil then putvarp(vartbl[vi]); { free variable entry }
   vartbl[vi] := nil { clear entry }
   
end;      

{******************************************************************************

Push new function context level

Gets a new function context stack record and places that on the function 
context stack.

******************************************************************************}

procedure pshfns;

var fp: fnsptr; { control pointer entry }

begin

   if fnsfre <> nil then begin { recover used entry }

      fp := fnsfre; { index top of stack }
      fnsfre := fnsfre^.next { gap from list }

   end else new(fp); { else get a new entry }
   fp^.next := fnsstk; { push onto control stack }
   fnsstk := fp;
   fp^.vs := nil; { clear variables list }
   fp^.next := nil; { clear next }
   fp^.endf := false { set end of function not found }

end;      

{******************************************************************************

Pop function context level

Removes the top entry from the function context stack and places it into free
storage.

******************************************************************************}

procedure popfns;

var fp: fnsptr; { control pointer entry }

begin

   fp := fnsstk; { index top of stack }
   fnsstk := fnsstk^.next; { gap from list }
   fp^.next := fnsfre; { insert to free list }
   fnsfre := fp

end;      

{******************************************************************************

Purge function stack

Removes all entries from the function stack.

******************************************************************************}

procedure prgfns;

var vp: varptr; { variable entry pointer }

begin

   while fnsstk <> nil do begin { purge stack }

      { release all variable saves }
      while fnsstk^.vs <> nil do begin

         vp := fnsstk^.vs; { index top entry }
         fnsstk^.vs := fnsstk^.vs^.next; { gap }
         putvarp(vp) { free entry }

      end;
      popfns { remove function level }

   end

end;

{******************************************************************************

Check character

Returns the next character code in the currently executing program line.

******************************************************************************}
 
function chkchr: char;
 
begin

   if curprg = nil then chkchr := chr(ord(cpend)) { off end, return end }
   else chkchr := curprg^.str[linec] { return next character }

end;
 
{******************************************************************************

Get character

Skips the current character in the program line.

******************************************************************************}
 
procedure getchr;
 
begin

   if linec < maxstr then linec := linec+1 { if not end, skip }

end;

{******************************************************************************

Skip space

Simply skips any space tolken at the present input position. This need only
be done once, as there will never be multiple space tolkens back to back.

******************************************************************************}
 
procedure skpspc;
 
begin

   if chkchr = chr(ord(cspc)) then getchr { skip single space }
   else if chkchr = chr(ord(cspcs)) then 
      begin getchr; getchr end { skip multiple spaces }

end;

{******************************************************************************

Check end of statement

Checks an ':', end of line, else, endif, wend or until is next after any
spaces.

******************************************************************************}

function chksend: boolean;

begin

   skpspc; { skip spaces }
   { check eoln, ':' or 'else' }
   chksend := ktrans[chkchr] in [cpend, clend, ccln, celse, cendif, cwend,
                                 cuntil, cendsel, crema]

end;

{******************************************************************************

Skip tolken multiline

Skips the present tolken. Will not skip the end of program tolken. Skips to the
next line on end of line.

******************************************************************************}

procedure skptlkl;

begin

   case ktrans[chkchr] of { tolken }

      { simple tolkens }
      cinput, cprint, cgoto, con, cif, cthen, celse, cendif, cstop, crun,
      clist, cnew, clet, cload, csave, cdata, cread, crestore, cgosub, creturn,
      cfor, cnext, cstep, cto, cwhile, cwend, crepeat, cuntil, cselect, ccase,
      cother, cendsel, copen, cclose, cend, cdumpv, cdumpp, cdim, cdef, 
      cfunction, cendfunc, cprocedure, cendproc, crand,
      ctrace, cnotrace, cas, coutput, cbye, clequ, cgequ, cequ, cnequ, cltn, 
      cgtn, cadd, csub, cmult, cdiv, cexpn, cmod, cidiv, cand, cor, cxor, cnot,
      cleft, cright, cmid, cstr, cval, cchr, casc, clen, csqr, cabs, csgn, crnd,
      cint, csin, ccos, ctan, catn, clog, cexp, ctab, cusing, ceof, clcase, 
      cucase, cscn, ccln, clpar, crpar, ccma, cpnd, cperiod,
      cspc: linec := linec+1; { skip tolken }
      cintc: linec := linec+5; { skip integer constant }
      crlc: linec := linec+9; { skip real constant }
      cstrc, 
      crem, crema: linec := linec+2+ord(curprg^.str[linec+1]); { skip string }
      cintv, crlv, cstrv, cspcs: linec := linec+2; { skip variable/spaces }
      clend: begin curprg := curprg^.next; linec := 1 end; { go to next line } 
      cpend: { do nothing }

   end

end;

{******************************************************************************

Skip tolken

Skips the present tolken. Will not skip the end of line tolken.

******************************************************************************}

procedure skptlk;

begin

   if chkchr <> chr(ord(clend)) then skptlkl { if not end of line, skip }

end;

{******************************************************************************

Check null string 

Checks that the entire basic string is empty, or spaces only.

******************************************************************************}
 
function null(var str: bstptr): boolean;
 
var i: integer;
    f: boolean;
 
begin

   f := true;
   for i := 1 to str^.len do if str^.str[i] <> ' ' then f := false;
   null := f

end;

{******************************************************************************

Parse leading integer 

Gets the leading integer off the given line. If no leading integer is present, 
returns 0.

******************************************************************************}
 
function lint(var str: bstringt): integer;
 
var i, v: integer;
 
begin

   v := 0; { clear result }
   i := 1; { set 1st character }
   while ((str.str[i] = chr(ord(cspc))) or (str.str[i] = chr(ord(cspcs)))) and
         (str.str[i] <> chr(ord(clend))) do { skip spaces }
      if str.str[i] = chr(ord(cspcs)) then i := i+2 else i := i+1;      
   { if next is integer constant, load that }
   if str.str[i] = chr(ord(cintc)) then begin

      i := i+1; { skip tolken code }
      dcdint(str, i, v)

   end;
   lint := v

end;

{******************************************************************************

Search label

Finds a program line by the given number. If no program line by the number
is found, an error results.

******************************************************************************}
 
function schlab(lab: integer): bstptr;
 
var pp, fp: bstptr; { program line pointers }
 
begin

   pp := prglst; { index top program line }
   fp := nil; { clear found line }
   while pp <> nil do { search for line }
      if lab = lint(pp^) then begin fp := pp; pp := nil end { found }
      else pp := pp^.next; { next line }
   if fp = nil then prterr(elabnf);
   schlab := fp { return line found }

end;

{******************************************************************************

Register labels

Registers all undefined goto labels.

******************************************************************************}
 
procedure reglab;
 
var pp:      bstptr; { program line pointers }
    vi:      varinx; { variable table index }
    curprgs: bstptr; { current program line save }
 
begin

   curprgs := curprg; { save current program line }
   pp := prglst; { index top program line }
   while pp <> nil do begin { search lines }

      curprg := pp; { set as current line for scanning }
      linec := 1; { reset to line start }
      if chkchr = chr(ord(cintc)) then skptlk; { skip line number }
      skpspc; { skip spaces }
      { as a label looking thing, symbolic goto labels will appear to be real
        variables in source }
      if chkchr = chr(ord(crlv)) then begin { check for line label }

         getchr; { skip }
         vi := ord(chkchr); { get variable index }
         getchr; { skip }
         { the syntax of a label is identical to a parameterless procedure
           followed by a statement separator (':'). because labels get declared
           immediately on encounter, the procedure interpretation must take
           precidence }
         if not vartbl[vi]^.prc or (vartbl[vi]^.par <> nil) then begin

            { possible label, now we find the ':' }
            skpspc; { skip spaces }
            if chkchr = chr(ord(ccln)) then begin { its a label }

               getchr; { get ':' }
               if vartbl[vi]^.gto then prterr(eglbdef); { already defined }
               vartbl[vi]^.gto := true; { set as goto }
               vartbl[vi]^.glin := curprg { set target line }

            end

         end

      end;
      pp := pp^.next; { next line }

   end;
   curprg := curprgs { restore current program line }

end;

{******************************************************************************

Free program line

Scans a program line, and if variables are found, decrements the reference
count for them. If the reference count becomes 0, then the variable entry is
cleared and the variable returned to free storage.
This routine is used prior to deleting a program line, so that all variable
references are accounted for.

******************************************************************************}

procedure frelin(var str: bstptr);

var li: 1..maxstr;  { index for line }
    vi: varinx; { variables index }

begin

   li := 1; { set start of line }
   while str^.str[li] <> chr(ord(clend)) do { traverse tolkens }
      case ktrans[str^.str[li]] of { tolken }

      { simple tolkens }
      cinput, cprint, cgoto, con, cif, cthen, celse, cendif, cstop, crun,
      clist, cnew, clet, cload, csave, cdata, cread, crestore, cgosub, creturn,
      cfor, cnext, cstep, cto, cwhile, cwend, crepeat, cuntil, cselect, ccase,
      cother, cendsel, copen, cclose, cend, cdumpv, cdumpp, cdim, cdef, 
      cfunction, cendfunc, cprocedure, cendproc, crand,
      ctrace, cnotrace, cas, coutput, cbye, clequ, cgequ, cequ, cnequ, cltn,
      cgtn, cadd, csub, cmult, cdiv, cexpn, cmod, cidiv, cand, cor, cxor, cnot,
      cleft, cright, cmid, cstr, cval, cchr, casc, clen, csqr, cabs, csgn,
      crnd, cint, csin, ccos, ctan, catn, clog, cexp, ctab, cusing, ceof,
      clcase, cucase, cscn, ccln, clpar, crpar, ccma, cpnd, cperiod,
      cspc: li := li+1; { skip tolken }
      cintc: li := li+5; { skip integer constant }
      crlc: li := li+9; { skip real constant }
      cstrc, 
      crem, crema: li := li+2+ord(str^.str[li+1]); { skip string }
      cintv, crlv, cstrv: begin { variables }

         { count references }
         vi := ord(str^.str[li+1]); { get variable index }
         vartbl[vi]^.ref := vartbl[vi]^.ref-1;
         { if no references, release variable }
         if vartbl[vi]^.ref = 0 then putvar(vi);
         li := li+2; { skip variable/spaces }

      end;
      cspcs: li := li+2; { skip spaces }
      cpend: { this should not really happen }

   end

end;

{******************************************************************************

Enter line to store 

Enters the top program line to a sorted position in the program store, moving 
lines to create space as required. The line number for the string should not be
missing or zero.

******************************************************************************}
 
procedure enter(il: bstptr); { line to enter }
 
var line, i, j: integer;
    f:          boolean;
    pp, lp, fp: bstptr; { program line pointers }

{ insert incoming line to program list }
 
procedure insert;

begin

   if lp <> nil then begin { insert mid list }

      il^.next := lp^.next; { point to next }
      lp^.next := il { point last to this }

   end else begin { insert at root }

      il^.next := prglst; { point to next }
      prglst := il { point last to this }

   end

end;

begin

   line := lint(il^); { get line number }
   if line > maxnum then prterr(elintl); { input line number to large }
   i := 1; { set 1st program line }
   f := false; { clear found flag }
   pp := prglst; { index 1st line }
   lp := nil; { clear last pointer }
   fp := nil; { set no line found }
   while pp <> nil do begin { search program store }

      if line <= lint(pp^) then begin { found }

         fp := pp; { save found entry }
         pp := nil { stop search }

      end else begin { next }

         lp := pp; { set last entry }
         pp := pp^.next { link next entry }

      end

   end;
   if fp <> nil then begin { found a line equal to or greater than us }

      if line = lint(fp^) then begin { same line }

         frelin(fp); { remove old references }
         { check incoming is line number only, in which case delete line }
         j := 1;
         if il^.str[j] = chr(ord(cintc)) then j := j+5; { skip integer }
         while ((il^.str[j] = chr(ord(cspc))) or 
                (il^.str[j] = chr(ord(cspcs)))) and
               (il^.str[j] <> chr(ord(clend))) do { skip spaces }
            if il^.str[j] = chr(ord(cspcs)) then j := j+2 else j := j+1;
         { remove target line from program list }
         if lp <> nil then lp^.next := fp^.next { gap over entry }
         else prglst := fp^.next; { gap over 1st entry }
         if il^.str[j] = chr(ord(clend)) then { delete }
            putstr(il) { free new line }
         else insert; { replace }
         putstr(fp) { free deleted line }
      
      end else insert { insert before this line }

   end else insert { not found, must be at program end }

end;

{******************************************************************************

Search for variable entry

Searches for the given label in the variable table, then returns the index
for the variable. If no variable by the name is found, 0 is returned.

******************************************************************************}

function fndvar(var lab: string12) { label to find }
               :varinx;            { index of variable }

var fi, vi: varinx; { variable indexes }

begin

   fi := 0; { set no variable found }
   for vi := 1 to maxvar do { search table }
      if vartbl[vi] <> nil then { entry occupied }
         if vartbl[vi]^.nam = lab then fi := vi; { set variable found }
   fndvar := fi { return variable index }

end;

{******************************************************************************

Parse and convert numeric

Reads a number from the given position in a string. The number can be integer
in any of the following radixes:

   <none>decimal
   $hex
   %binary
   &octal

The number can also be a floating point, with or without an exponent and
decimal point.

******************************************************************************}

procedure parnum(var s:      bstringt; { string to read from }
                 var pos:    integer;  { parse position }
                     psign:  boolean;  { allow sign }
                 var isreal: boolean;  { result is real/result is integer }
                 var nxtint: integer;  { integer return }
                 var nxtflt: real);    { real return} 

var c:     char;
    r:     1..16;   { radix }
    v:     0..36;   { integer value holder, enough for 10+(a-z) }
    rm:    boolean; { radix mark encountered flag }
    exp:   integer; { exponent of real }
    sgn:   integer; { sign holder }
    zero:  boolean; { number consists of zeros }
    p:     real;    { power }
    dummy: real;    { used as a dummy parameter }
    sign:  boolean; { number is signed (negative) }
    

{ check end of string }

function chkend: boolean;

begin

   chkend := pos > s.len { is reference off end ? }

end;

{ check next character in string }

function chkchr: char;

var c: char; { character buffer }

begin

   if chkend then c := ' ' { replace off end with space }
   else c := s.str[pos]; { else return next character }
   chkchr := c

end;

{ get next character }

procedure getchr;

begin

   if not chkend then pos := pos+1 { if not end, then next character }

end;

{ skip spaces }

procedure skpspc;

begin

   while (chkchr = ' ') and not chkend do getchr { skip any spaces }

end;

{ find power of ten effciently }

function pwrten(e: integer): real;

var t: real; { accumulator }
    p: real; { current power }

begin

   p := 1.0e+1; { set 1st power }
   t := 1.0; { initalize result }
   repeat 

      if odd(e) then t := t*p; { if bit set, add this power }
      e := e div 2; { index next bit }
      p := sqr(p) { find next power }

   until e = 0;
   pwrten := t

end;

begin

   skpspc; { skip spaces }
   rm := false; { set no radix mark }
   r := 10; { set default radix decimal}
   exp := 0; { clear real exponent }
   nxtint := 0; { initalize result }
   isreal := false; { set integer }
   sign := false; { set positive }
   if psign and (chkchr = '+') then { positive }
      getchr
   else if psign and (chkchr = '-') then
      begin sign := true; getchr end
   { check binary }
   else if chkchr = '%' then { binary }
      begin r := 2; rm := true; getchr end
   { check octal }
   else if chkchr = '&' then { octal }
      begin r := 8; rm := true; getchr end
   { check hexadecimal }
   else if chkchr = '$' then { hexadecimal }
      begin r := 16; rm := true; getchr end;
   if not (((chkchr in ['0'..'9', 'a'..'z', 'A'..'Z']) and (r = 16)) or
           (chkchr in ['0'..'9', '.'])) then prterr(enfmt); { invalid digit }
   while (((chkchr in ['a'..'z', 'A'..'Z']) and (r = 16)) or
      (chkchr in ['0'..'9'])) do begin { parse digits }

         { count significant digits to exponent (used on real only) }
         if (chkchr <> '0') or (exp <> 0) then exp := exp+1;
         { convert '0'..'9' }
         if (chkchr in ['0'..'9']) then v := ord(chkchr) - ord('0')
         else v := ord(lcase(chkchr)) - ord('a') + 10; { convert 'a'..'z' }
         if v >= r then prterr(edbr) { digit beyond radix }
         else begin { ok }

            { check for overflow }
            if (nxtint > maxint div r) or 
               ((nxtint = maxint div r) and (v > maxint mod r)) then
               prterr(enovf) { overflow }
            else nxtint := nxtint * r + v { scale and add in }

         end;
         getchr { next }

   end;
   if exp <> 0 then exp := exp - 1; { adjust exponent }
   nxtflt := nxtint; { move integer to real }
   if chkchr = '.' then begin { decimal point }

      getchr; { skip '.' }
      zero := nxtint = 0; { check number is zero (so far) }
      if rm then prterr(erfmt); { radix mark on real }
      p := 1.0; { initalize power }
      while chkchr in ['0'..'9'] do begin { parse digits }

         if zero then exp := exp-1; { adjust the 'virtual exponent' }
         if chkchr <> '0' then zero := false; { set leading digit found }
         p := p / 10.0; { find next scale }
         { add and scale new digit }
         nxtflt := nxtflt + (p * (ord(chkchr) - ord('0')));
         getchr { next }

      end;
      isreal := true { set is a real }

   end;
   if lcase(chkchr) = 'e' then begin { exponent }

      getchr; { skip 'e' }
      sgn := 1; { set sign of exponent }
      c := chkchr; { check next }
      if c = '-' then sgn := -sgn; { set negative }
      if (c = '+') or (c = '-') then getchr; { skip sign }
      if not (chkchr in ['0'..'9']) then prterr(erfmt) { bad format }
      else begin

         parnum(s, pos, false, isreal, nxtint, dummy); { parse integer only }
         if isreal then prterr(erfmt); { bad format }
         if (nxtint > maxexp) or (abs(sgn*nxtint+exp) > maxexp) then
            prterr(eexpovf); { exponent too large }
         { find with exponent }
         if c = '-' then nxtflt := nxtflt / pwrten(nxtint)
         else nxtflt := nxtflt * pwrten(nxtint)

      end;
      isreal := true { set is a real }

   end;
   if sign then begin { process negate }

      { negate proper type }
      if isreal then nxtflt := -nxtflt else nxtint := -nxtint

   end

end;

{******************************************************************************

Convert line to intermediate code 

Tolkenize the given string, placing the result back into the same string. The
encode sequence consists of a series of <code><param> entries, without
rearrangement, and therefore is essentially a scanner pass. This kind of
encoding has the advantage that it is recreatable, and can therefore serve as
the replacement for the source. The only disadvantage is that we cannot
reasonably preserve case, and therefore the source will appear in all lower
case.
All control characters besides line terminators are turned into spaces
for tolkenization. This allows input with control characters embedded, but
does not preserve them.

******************************************************************************}
 
procedure keycom(var str: bstptr); { string to compress }
 
var ts:     bstringt;
    i1, li: integer;
    f:      boolean;
    c:      char;
    k:      keycod; { key search code }
    v:      integer; { integer value }
    isreal: boolean; { real/integer flag }
    s:      integer; { sign holder }
    t:      integer; { temp }
    spc:    integer; { spaces count }
    fc:     record case boolean of { float convertion }

                       false: (r: real);
                       true:  (c: packed array [1..8] of char)

                    end; 
    fci:    1..8; { index for same }
    lab:    string12; { label holder }
    l:      1..12; { index for label }       
    vi:     varinx; { index for variables table }
 
{ check end of string }

function chkend: boolean;

begin

   chkend := i1 > str^.len { is reference off end ? }

end;

{ check next character in string }

function chkchr: char;

var c: char; { character buffer }

begin

   if chkend then c := ' ' { replace off end with space }
   else c := str^.str[i1]; { else return next character }
   if c < ' ' then c := ' '; { all control characters turn to spaces }
   chkchr := c

end;

{ get next character }

procedure getchr;

begin

   if not chkend then i1 := i1+1 { if not end, then next character }

end;

{ skip spaces }

procedure skpspc;

begin

   while (chkchr = ' ') and not chkend do getchr { skip any spaces }

end;

{ place next tolken character }

procedure putchr(c: char);

begin

   if ts.len >= maxstr then prterr(ecodtl); { line to large to encode }
   ts.len := ts.len+1; { increase string length }
   ts.str[ts.len] := c { place next character }

end;

function matstr(var stra: bstptr; var i: integer;
                 var strb: string12): boolean;
 
var i1, i2: integer;
    f:      boolean;
 
begin { matstr }

   i1 := i;
   i2 := 1;
   repeat

      if strb[i2] = ' ' then f := false
      else if lcase(stra^.str[i1]) = lcase(strb[i2]) then begin

         f := true;
         i1 := i1 + 1;
         i2 := i2 + 1

      end
      else f := false

   until not f or (i1 > stra^.len) or (i2 > 12);
   if i2 > 12 then begin f := true; i := i1 end
   else if strb[i2] = ' ' then begin f := true; i := i1 end
   else f := false;
   matstr := f

end; { matstr }

{ get label }

procedure getlab(var lab: string12; { label to get }
                     str: boolean); { allow '$' or '%' sign }

var li: 1..11; { index for label }

begin

   lab := '            '; { clear label }
   li := 1; { set 1st label character }
   while (chkchr in ['a'..'z', 'A'..'Z', '0'..'9', '_']) or
         ((chkchr in ['$', '%']) and str) do begin 

      { get label characters }
      if li > 12 then prterr(elabtl); { label too long }
      lab[li] := lcase(chkchr); { place next character lower case }
      getchr; { skip }
      li := li+1 { next character position }

   end

end;

begin { keycom }

   i1 := 1; { index start of source string }
   ts.len := 0; { clear destination string }
   { check for leading line number }
   skpspc; { skip any leading spaces }
   { if there is a line number, remove leading spaces, otherwise leave it
     for formatting purposes }
   if not (chkchr in ['0'..'9', '$', '&', '%']) then i1 := 1;
   while not chkend do begin { read string characters }

      if chkchr = ' ' then begin { space or spaces }

         getchr; { skip space }
         if (chkchr = ' ') and not chkend then begin { spaces }

            spc := 1; { count space already passed }
            while (chkchr = ' ') and not chkend do begin { count spaces }

               getchr; { next character }
               spc := spc+1 { count }

            end;   
            putchr(chr(ord(cspcs))); { place spaces code }
            putchr(chr(spc)) { place space count }

         end else putchr(chr(ord(cspc))) { place single space code }
   
      end else if chkchr = '!' then begin { comment }

         getchr; { skip '!' }
         putchr(chr(ord(crema))); { place rem tolken }
         putchr(chr(0)); { set length }
         li := ts.len; { save location of length }
         while not chkend do begin
   
            putchr(chkchr); 
            getchr; 
            ts.str[li] := succ(ts.str[li]) { count characters }
   
         end

      end else if chkchr in ['0'..'9', '$', '&', '%', '.'] then begin 

         c := chkchr; { save lead character }
         getchr; { skip }
         { because a single '.' can look like a number, we have to check for
           a lone '.' and change that to a tolken }
         if (c = '.') and not (chkchr in ['0'..'9']) then { its '.' tolken }
            putchr(chr(ord(cperiod))) { place period tolken }
         else begin

            i1 := i1-1; { return to leadin character }
            { number }
            parnum(str^, i1, false, isreal, v, fc.r); { parse number }
            if isreal then begin { place real constant }

               putchr(chr(ord(crlc))); { place tolken }
               for fci := 1 to 8 do putchr(fc.c[fci]) { place real }

            end else begin { place integer constant }

               putchr(chr(ord(cintc))); { place tolken }
               { set sign }
               if v < 0 then s := 128 else s := 0;
               v := abs(v); { remove sign }
               t := v div 16777216; { high byte }
               putchr(chr(t+s)); { with sign }
               v := v - (t * 16777216); { high middle }
               t := v div 65536;
               putchr(chr(t));
               v := v - (t * 65536); { low middle }
               t := v div 256;
               putchr(chr(t));
               v := v - (t * 256); { low }
               putchr(chr(v))

            end

         end
      
      end else if chkchr = '"' then begin { string sequence }

         getchr; { skip '"' }
         putchr(chr(ord(cstrc))); { set string constant tolken }
         putchr(chr(0)); { set string length }
         li := ts.len; { save location of string length }
         repeat

            while not chkend and (chkchr <> '"') do begin

               { enter string characters }
               putchr(chkchr); { copy character to destination }
               getchr; { next character }
               ts.str[li] := succ(ts.str[li]) { count string characters }

            end;
            if chkend then prterr(emqu); { missing quote }
            getchr; { skip '"' }
            c := chkchr; { save next character }
            if c = '"' then begin { quote image, enter single quote }

               putchr(c); { copy character to destination }
               getchr; { next character }
               ts.str[li] := succ(ts.str[li]) { count string characters }
            
            end

         until c <> '"' { not a quote image }

      end else begin { search for key string }

         k := clequ; { set first key }
         f := false; { set not found }
         while (k <= cperiod) and not f do begin { search for keys }

            f := matstr(str, i1, keywd[k]); { check key matches }
            k := succ(k) { next key }

         end;
         if f then begin { found }

            k := pred(k); { back up one }
            putchr(chr(ord(k))) { place key code }

         end else if chkchr in ['a'..'z', 'A'..'Z', '_'] then begin

            { variable or keyword }
            li := i1; { save line position }
            getlab(lab, true); { get label as keyword }
            { convert label to lower case }
            for l := 1 to 12 do lab[l] := lcase(lab[l]);
            k := cinput; { set first key }
            f := false; { set not found }
            while (k <= cucase) and not f do begin { search for keys }

               f := lab = keywd[k]; { check key matches }
               k := succ(k) { next key }

            end;
            if f then begin { found }
   
               k := pred(k); { back up one }
               putchr(chr(ord(k))); { place key code }
               { if the key was rem, then copy the rest of the line }
               if k = crem then begin
   
                  putchr(chr(0)); { set length }
                  li := ts.len; { save location of length }
                  while not chkend do begin
   
                     putchr(chkchr); 
                     getchr; 
                     ts.str[li] := succ(ts.str[li]) { count characters }
   
                  end
   
               end

            end else begin { try variable }

               i1 := li; { restore label position }
               getlab(lab, false); { get the variable label }
               vi := fndvar(lab); { find that in the table }
               if vi <> 0 then { variable exists }
                  vartbl[vi]^.ref := vartbl[vi]^.ref+1 { count references }
               else begin { establish new variable }
               
                  getvar(vi); { get a new variable entry }
                  vartbl[vi]^.nam := lab; { place label }
                  vartbl[vi]^.inx := vi { place index }
               
               end;
               if chkchr = '$' then begin { string variable }
               
                  getchr; { skip '$' }
                  putchr(chr(ord(cstrv))) { set string variable tolken }
               
               end else if chkchr = '%' then begin { integer variable }
               
                  getchr; { skip '%' }
                  putchr(chr(ord(cintv))) { set integer variable tolken }
               
               end else putchr(chr(ord(crlv))); { real variable }
               putchr(chr(vi)) { place variable index }

            end

         end else prterr(einvchr) { invalid character }

      end

   end;
   putchr(chr(ord(clend))); { place line end }
   str^.str := ts.str; { place encoded string back to original }
   str^.len := ts.len

{ this diagnostic prints the resulting tolken sequence }

{
 ;for i1 := 1 to str^.len do write(ord(str^.str[i1]):3, ' '); writeln
}

end; { keycom }

{******************************************************************************

Get integer 

Returns the next integer. Errors if the next tolken is not an integer constant.

******************************************************************************}
 
function getint: integer;
 
var v: integer;
 
begin

   skpspc;
   if chkchr <> chr(ord(cintc)) then prterr(einte);
   getchr; { skip code }
   dcdint(curprg^, linec, v); { get integer constant }
   getint := v

end;

{******************************************************************************

Get real 

Returns the next real. Errors if the next tolken is not a real constant.

******************************************************************************}
 
function getrl: real;
 
var v: real;
 
begin

   skpspc;
   if chkchr <> chr(ord(crlc)) then prterr(erlexp);
   getchr; { skip code }
   dcdrl(curprg^, linec, v); { get integer constant }
   getrl := v

end;

{******************************************************************************

Get real from basic string 

Gets a real value from the given basic string. Any leading spaces are
skipped. Errors if there is anything else besides the real in the string.

******************************************************************************}
 
function getrval(var str: bstringt): real;
 
var pos, i: integer;
    r:      real; 
    isreal: boolean; { real number flag }
 
begin

   pos := 1;
   parnum(str, pos, false, isreal, i, r); { parse number }
   if not isreal then r := i; { convert integer to real } 
   { skip trailing spaces }
   while (pos <= str.len) and (str.str[pos] = ' ') do pos := pos+1;
   if pos <= str.len then prterr(econv);
   getrval := r { return result }

end;

{******************************************************************************

Get integer from basic string 

Gets an integer value from the given basic string. any leading spaces are
skipped. Errors if there is anything else besides the integer in the string.

******************************************************************************}
 
function getbval(var str: bstringt): integer;
 
var pos, i: integer;
    r:      real; 
    isreal: boolean; { real number flag }
 
begin

   pos := 1;
   parnum(str, pos, false, isreal, i, r); { parse number }
   if isreal then i := round(r); { convert real to integer } 
   { skip trailing spaces }
   while (pos <= str.len) and (str.str[pos] = ' ') do pos := pos+1;
   if pos <= str.len then prterr(econv);
   getbval := i { return result }

end;
 
{******************************************************************************

Input basic string

Reads a basic string from the console. Reads all characters up until the next
eoln to the string. Errors on string overflow.

******************************************************************************}
 
procedure inpbstr(var f:    text;      { file to input from }   
                  var bstr: bstringt); { string to input }
 
var i: integer;
 
begin

   for i := 1 to maxstr do bstr.str[i] := ' ';
   bstr.len := 0;
   while (bstr.len < maxstr) and not eoln(f) do begin

      if eof(f) then prterr(eeofenc); { eof encountered }
      bstr.len := bstr.len+1; { count characters }
      read(f, bstr.str[bstr.len])

   end;
   if (bstr.len > maxstr) and not eoln(f) then prterr(eiovf);
   readln(f)

end;
 
{******************************************************************************

Concatenate basic strings

Concatenates the source string to the destination string, and places the
result into the destination string.

*******************************************************************************}
 
procedure catbs(var bstra, bstrb: bstringt);
 
var i: integer; { index for string }

begin 

   if (bstra.len + bstrb.len) > maxstr then prterr(estrovf); { string overflow }
   { copy source after destination }
   for i := 1 to bstrb.len do bstra.str[bstra.len+i] := bstrb.str[i];
   bstra.len := bstra.len + bstrb.len { set new length }

end;

{******************************************************************************

Check strings equal

Checks if string a equals string b. Returns true if so.

*******************************************************************************}
 
function strequ(var bstra, bstrb: bstringt): boolean;
 
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

{******************************************************************************

Check string less than

Checks if string a is less than string b. Returns true if so.

*******************************************************************************}
 
function strltn(var bstra, bstrb: bstringt): boolean;
 
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

{******************************************************************************

Convert top of stack to integer

If the top of stack is real, it is converted to integer.

******************************************************************************}

procedure cvtint;

var v: integer;

begin

   if temp[top].typ = trl then begin { real, convert }

      v := round(temp[top].rl); { load as integer }
      temp[top].typ := tint; { set entry as integer }
      temp[top].int := v { place value }

   end

end;

{******************************************************************************

Convert second on stack to integer

If the second on stack is real, it is converted to integer.

******************************************************************************}

procedure cvtints;

var v: integer;

begin

   if temp[top-1].typ = trl then begin { real, convert }

      v := round(temp[top-1].rl); { load as integer }
      temp[top-1].typ := tint; { set entry as integer }
      temp[top-1].int := v { place value }

   end

end;

{******************************************************************************

Convert top of stack to real

If the top of stack is integer, it is converted to real.

******************************************************************************}

procedure cvtrl;

var v: real;

begin

   if temp[top].typ = tint then begin { integer, convert }

      v := temp[top].int; { load as real }
      temp[top].typ := trl; { set entry as real }
      temp[top].rl := v { place value }

   end

end;

{******************************************************************************

Convert second on stack to real

If the second on stack is integer, it is converted to real.

******************************************************************************}

procedure cvtrls;

var v: real;

begin

   if temp[top-1].typ = tint then begin { integer, convert }

      v := temp[top-1].int; { load as real }
      temp[top-1].typ := trl; { set entry as real }
      temp[top-1].rl := v { place value }

   end

end;

{******************************************************************************

Binary math preparation

Prepares the top and second on stack for a binary math operation. If either is
real, then the other must be also converted to real.

******************************************************************************}

procedure binprp;

var v: real;

begin

   if (temp[top].typ = trl) or (temp[top-1].typ = trl) then begin 

      { one is real, convert }
      if temp[top].typ = tint then begin { convert top }
 
         v := temp[top].int; { load as real }
         temp[top].typ := trl; { set entry as real }
         temp[top].rl := v { place value }

      end;
      if temp[top-1].typ = tint then begin { convert second }
 
         v := temp[top-1].int; { load as real }
         temp[top-1].typ := trl; { set entry as real }
         temp[top-1].rl := v { place value }

      end;

   end

end;

{******************************************************************************

Check stack items equal 

Checks if the integer tos is equal to the integer sos. Errors if both are not
integer. Should be extended for string case.

******************************************************************************}
 
function chkequ : boolean;
 
begin

   binprp; { prepare binary }
   if (temp[top].typ = trl) and (temp[top-1].typ = trl) then { real }
      chkequ := temp[top-1].rl = temp[top].rl
   else begin { must be integer }

      if (temp[top].typ <> tint) or (temp[top - 1].typ <> tint) then 
         prterr(ewtyp);
      chkequ := temp[top-1].int = temp[top].int

   end

end;
 
{******************************************************************************

Check stack items less than

Checks if the integer sos is less than the integer tos. Errors if both are not
integer. Should be extended for string case.

******************************************************************************}
 
function chkltn: boolean;
 
begin

   binprp; { prepare binary }
   if (temp[top].typ = trl) and
      (temp[top-1].typ = trl) then { real }
      chkltn := temp[top-1].rl < temp[top].rl
   else begin { must be integer }

      if (temp[top].typ <> tint) or
         (temp[top - 1].typ <> tint) then 
         prterr(ewtyp);
      chkltn := temp[top-1].int < temp[top].int

   end

end;
 
{******************************************************************************

Check stack items greater than

Checks if the integer sos is greater than the integer tos. Errors if both are 
not integer. Should be extended for string case.

******************************************************************************}
 
function chkgtn: boolean;
 
begin

   binprp; { prepare binary }
   if (temp[top].typ = trl) and
      (temp[top-1].typ = trl) then { real }
      chkgtn := temp[top-1].rl > temp[top].rl
   else begin { must be integer }

      if (temp[top].typ <> tint) or
         (temp[top - 1].typ <> tint) then 
         prterr(ewtyp);
      chkgtn := temp[top-1].int > temp[top].int

   end

end;
 
{******************************************************************************

Set tos true 

Simply sets the tos to an integer 1, or true.

******************************************************************************}
 
procedure settrue;
 
begin

   temp[top].typ := tint;
   temp[top].int := -1

end;
 
{******************************************************************************

Set tos false

Simply sets the tos to an integer 0, or false.

******************************************************************************}
 
procedure setfalse;
 
begin

   temp[top].typ := tint;
   temp[top].int := 0

end;
 
{******************************************************************************

Clear single variable

Clears the given variable and its substructure. The substructures are sent
back to free, and the variable entry itself is set back to zero and initalized.

******************************************************************************}
 
procedure clrvar(vp: varptr); { variable to clear }
 
{ clear vector }

procedure clrvec(var vp: vvcptr);

var vi: vecinx; { vector index }

begin

   if vp <> nil then begin { vector is allocated }

      { clear any subvectors }
      if vp^.vt = vvvec then for vi := 1 to maxvec do clrvec(vp^.vec[vi])
      else if vp^.vt = vvstr then for vi := 1 to maxvec do putstr(vp^.str[vi]);
      putvec(vp); { release the vector entry }
      vp := nil { flag clear }

   end

end;

{ clear out any function definition and parameters }

procedure clrfnc(var vp: varptr);

var pp: varptr; { parameter pointer }

begin

   if vp^.fnc or vp^.prc then begin { is a function/procedure }

      while vp^.par <> nil do begin { clear parameters }

         pp := vp^.par; { index parameter }
         vp^.par := pp^.par; { gap out }
         putvarp(pp) { release variable }

      end;
      vp^.fnc := false; { set not function }
      vp^.prc := false { set not procedure }

   end

end;   

{ clear record field lists }

procedure clrrec(var vp: varptr); { record list head }

var vp1: varptr; { variable pointer }

begin

   while vp <> nil do begin { clear }

      vp1 := vp; { index top of list }
      vp := vp^.next; { link next }
      clrvar(vp1); { clear out subentries }
      putvarp(vp1) { free entry }

   end

end;

begin

   if not vp^.sys then begin { not a protected system variable }

      { release any string entry }
      if vp^.str <> nil then putstr(vp^.str);
      vp^.str := nil; { clear }
      { clear values }
      vp^.int := 0;
      vp^.rl := 0;
      vp^.gto := false; { reset label status }
      clrvec(vp^.strv); { clear string vector }
      clrvec(vp^.intv); { clear integer vector }
      clrvec(vp^.rlv); { clear real vector }
      clrrec(vp^.rec); { clear record fields }
      clrfnc(vp) { clear function and parameters }

   end

end;
 
{******************************************************************************

Clear variable store

All variables, and substructures of those variables, are sent back to free
space. This is the same as clearing all variables to zero, as a program will
reallocate these from scratch.
Note that the variable base entries in the table cannot be totally freed,
because the tolkenized source references these entries.

******************************************************************************}
 
procedure clrvars;
 
var vi: varinx; { variable index }
 
begin

   for vi := 1 to maxvar do { clear variables }
      if vartbl[vi] <> nil then clrvar(vartbl[vi]); { variable exists }
   curprg := prglst; { index 1st program line }
   linec := 1; { set 1st character }
   top := 0; { clear stack }
   datac := prglst; { set data position to program start }
   datal := 1;
   prgctl; { purge control stack }
   prgfns { purge function stack }

end;

{******************************************************************************

Clear program store

Clears all the available program lines to empty.
The program line is set to immediate, and the stack is set empty.

******************************************************************************}
 
procedure clear;
 
var p: bstptr; { pointer to program lines }
 
begin

   while prglst <> nil do begin

      p := prglst; { index top entry }
      prglst := prglst^.next; { gap from list }
      putstr(p) { release line }

   end;
   clrvars { clear variables and run actions }

end;

{******************************************************************************

Skip data spaces

Advances the data read pointer over any spaces.

******************************************************************************}

procedure spcdat;

begin

   if datac^.str[datal] = chr(ord(cspc)) then 
      datal := datal+1 { skip single space }
   else if datac^.str[datal] = chr(ord(cspcs)) then 
      datal := datal+2 { skip multiple spaces } 

end;

{******************************************************************************

Find next data statement

Advances the data read pointer until the next data statement, or the end of the
program is reached.

******************************************************************************}

procedure nxtdat;

var f: boolean; { stop flag }

begin

   f := false; { set no stop }
   while not f do begin { search for next data statement }

      if datac = nil then f := true { terminate if past end of program }
      else case ktrans[datac^.str[datal]] of { tolken }

         { simple tolkens }
         cinput, cprint, cgoto, con, cif, cthen, celse, cendif, cstop, crun,
         clist, cnew, clet, cload, csave, cread, crestore, cgosub,
         creturn, cfor, cnext, cstep, cto, cwhile, cwend, crepeat, cuntil,
         cselect, ccase, cother, cendsel, copen, cclose, cend, cdumpv, cdumpp,
         cdim, cdef, cfunction, cendfunc, cprocedure, cendproc, crand, ctrace,
         cnotrace, cas, coutput, cbye, clequ, cgequ, cequ, cnequ, cltn, cgtn, 
         cadd, csub, cmult, cdiv, cexpn, cmod, cidiv, cand, cor, cxor, cnot,
         cleft, cright, cmid, cstr, cval, cchr, casc, clen, csqr, cabs, csgn, 
         crnd, cint, csin, ccos, ctan, catn, clog, cexp, ctab, cusing, ceof, 
         clcase, cucase, cscn, ccln, clpar, crpar, ccma, cpnd, cperiod,
         cspc: datal := datal+1; { skip tolken }
         cintc: datal := datal+5; { skip integer constant }
         crlc: datal := datal+9; { skip real constant }
         cstrc, 
         crem, crema: datal := datal+2+ord(datac^.str[datal+1]); { skip string }
         cintv, crlv, cstrv, cspcs: datal := datal+2; { skip variable/spaces }
         clend: { line end, skip to next line } 
            begin datac := datac^.next; datal := 1 end;
         cdata: begin { found data statement }

            datal := datal + 1; { skip to data section }
            spcdat; { skip any spaces }
            f := true { terminate }

         end

      end

   end

end;

{******************************************************************************

Find random number

Finds a random number between 0 and 1.

******************************************************************************}

function rand: real;
 
const a = 16807;
      m = 2147483647;

var gamma: integer;

begin

   gamma := a*(rndseq mod (m div a))-(m mod a)*(rndseq div (m div a));
   if gamma > 0 then rndseq := gamma else rndseq := gamma+m;
   rndsav := rndseq; { save the last random number }
   rand := rndseq*(1.0/m) { return scaled real number }

end;

{******************************************************************************

Make string exist

Given a string pointer, will ensure that the pointer contains a valid string.
If the pointer is nil, then a new (empty) string will be allocated to the
pointer.

******************************************************************************}

procedure makstr(var sp: bstptr); { string to allocate }

begin

   if sp = nil then getstr(sp) { if nil, then get a new string }

end;

{******************************************************************************

Clear vector

Applies whatever clear is appropriate to a typed vector.

******************************************************************************}

procedure clrvec(vp: vvcptr); { vector to clear }

var vi: vecinx; { index for vectors }

begin

   if vp^.vt = vvint then for vi := 1 to maxvec do vp^.int[vi] := 0
   else if vp^.vt = vvrl then for vi := 1 to maxvec do vp^.rl[vi] := 0.0
   else if vp^.vt = vvstr then for vi := 1 to maxvec do vp^.str[vi] := nil
   else if vp^.vt = vvvec then for vi := 1 to maxvec do vp^.vec[vi] := nil

end;

{******************************************************************************

Load constant string

Loads a string constant onto the stack.

*****************************************************************************}

procedure loadstr;

var i: integer;

begin

   getchr; { skip tolken start }
   top := top + 1;
   if top > maxstk then prterr(eexc);
   temp[top].typ := tstr;
   temp[top].bstr.len := ord(chkchr); { set string length }
   for i := 1 to ord(chkchr) do begin { load string }

      getchr; { skip to next character }
      temp[top].bstr.str[i] := chkchr { load string character }

   end;
   getchr { skip last character }

end;

{******************************************************************************

Skip forward for multiline nested 'if'

Skips forward to the next structure terminating symbol, which is one of program
end, endif, wend, until, next, and by option, else. Leaves the terminal symbol
as the next symbol.
Used to skip over non-active 'if'ed statements.
An error is flagged if the ifs found don't match up properly.

******************************************************************************}

procedure skpif(soe: boolean; { stop on 'else' }
                ssl: boolean); { stop single line }

var nest:  integer;       { multiline nesting count }
    nestl: integer;       { single line nesting count }
    ss:    set of keycod; { stop set }
    ts:    set of keycod; { terminal set }
    c:     char;          { tolken holder }

begin

   nest := 0; { set nesting count }
   nestl := 0; { set line only nesting count }
   ts := [cpend, cwend, cuntil, cendsel, cnext]; { create terminal set }
   ss := [cendif]; { create stop set }
   if soe then ss := ss+[celse]; { add 'else' if selected }
   if ssl then ss := ss+[clend]; { add end of line if selected }
   c := chr(ord(clend)); { set holding to neutral tolken }
   while (not (ktrans[chkchr] in ss) or (nest <> 0)) and
         not (ktrans[chkchr] in ts) do begin

      if chkchr = chr(ord(cif)) then begin

         nest := nest+1; { start a nested 'if' }
         nestl := nestl+1 { also 'if's on line only }

      end;
      if chkchr = chr(ord(cendif)) then begin

         nest := nest-1; { end a nested 'if' }
         { also remove active 'if's on line if terminated }
         if nestl > 0 then nestl := nestl-1

      end;
      if nest < 0 then prterr(emismif); { mismatched 'if's }
      if (chkchr = chr(ord(cpend))) and
         (nest <> 0) then prterr(emismif); { end of program, mismatch }
      if chkchr = chr(ord(clend)) then begin { end of line crossing }

         { now the locals nest count contains a count of all 'if's
           started within this line, which could be any number. Of these,
           only one can be a multiline, because only one dangling 'then'
           or 'else' can exist. So we check if the last was one of these,
           subtract that from the line start count, then subtract the
           remaining line count from the total nesting count }
         if ((c = chr(ord(cthen))) or (c = chr(ord(celse)))) and
            (nestl > 0) then nestl := nestl-1;
         nest := nest-nestl; { remove line terminates from total }
         nestl := 0 { restart line nesting count }

      end;
      c := chkchr; { save last tolken }
      skptlkl { skip next tolken }

   end;
   if nest <> 0 then prterr(emismif) { mismatched occurred }

end;

{******************************************************************************

Find record field

Given the head link and the name, finds a given record field in the field list.
If the record field is not found, then a new field entry is created, and that
is returned.

******************************************************************************}

procedure fndfld(var  vp: varptr;  { head linkage }
                      tn: varinx;  { index of field }
                 var  fp: varptr); { found entry pointer }

var vp1: varptr; { variable pointer }

begin

   vp1 := vp; { index head }
   fp := nil; { set no entry found }
   while vp1 <> nil do begin { search }

      { check for matching name }
      if tn = vp1^.inx then { found }
         begin fp := vp1; vp1 := nil end { set found and terminate }
      else { not found }
         vp1 := vp1^.next { link next }

   end;
   if fp = nil then begin { nothing found, create a new field }

      getvarp(fp); { get a new variable entry }
      fp^.inx := tn; { set to field name }
      fp^.nam := vartbl[tn]^.nam; { place name }
      fp^.next := vp; { push field onto field list }
      vp := fp

   end

end;

{******************************************************************************

Execute line

Executes program statements in the current line.

******************************************************************************}
 
procedure execl;
 
label 2, { exit procedure }
      1; { restart line }
 
var c: char;
 
{ execute statement }
 
procedure stat;
 
var cmd: keycod; { command save }

procedure expr; forward;

{******************************************************************************

Parse array reference

Parses a (x, y, ...) form array reference. Returns the vector entry and index
of the resulting access. If there is no vector allocated, it is allocated and
given the type implied by the access.

******************************************************************************}

procedure parvec(var rp:  vvcptr;  { root of reference }
                 var vp:  vvcptr;  { vectors pointer }
                 var inx: integer; { vector index } 
                     typ: vvctyp); { type of vector }

var newv: boolean; { new vector flag }
    news: boolean; { new vector set flag }
    vi:   vecinx;  { vector index }
    ic:   integer; { index count }

begin

   getchr; { skip '(' }
   ic := 1; { set index count = 1 }
   newv := false; { set not new vector }
   news := false; { set not new vector set }
   { if no vector exists, make one }
   if rp = nil then begin

      getvec(rp); { get new vector }
      newv := true; { set new vector }
      news := true; { set new vector set }

   end;
   expr; { parse expression }
   cvtint; { convert to integer }
   if temp[top].typ <> tint then prterr(einte); { integer expected }
   vp := rp; { point to vector }
   skpspc; { skip spaces }
   while chkchr = chr(ord(ccma)) do begin { parse indexes }

      getchr; { skip ',' }
      if newv then begin { working on a new vector }

         vp^.vt := vvvec; { new vector, set demension }
         clrvec(vp) { clear that vector }

      end;
      { check proper demensions }
      if vp^.vt <> vvvec then prterr(edim);
      { check index in range }
      if (temp[top].int > maxvec) or (temp[top].int < 0) then 
         prterr(esubrng); { subscript out of range }
      vi := temp[top].int; { get vector index }
      top := top-1; { remove index }
      if vp^.vec[vi] = nil then begin

         { we allocate arrays "sparsely", or allocating elements only as 
           actually referenced. So if a new branch is referenced, we allocate
           it and type it according to whether or not we have reached bottom }
         getvec(vp^.vec[vi]); { allocate this vector }
         newv := true { from here down, this will act as a new vector }

      end;
      vp := vp^.vec[vi]; { find next index }
      expr; { parse expression }
      cvtint; { convert to integer }
      if temp[top].typ <> tint then prterr(einte); { integer expected }
      ic := ic+1; { count index levels }
      if not news and (ic > rp^.inx) then prterr(edim) { bad index count }
      
   end;
   skpspc; { skip spaces }
   if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
   getchr; { skip ')' }
   if newv then begin { new vector }

      vp^.vt := typ; { set end of this chain }
      rp^.inx := ic; { set number of indexes in general }
      clrvec(vp) { clear vector }

   end;
   if (vp^.vt <> typ) or (ic <> rp^.inx) then 
      prterr(edim); { must be the end now }
   { check index in range }
   if (temp[top].int > maxvec) or (temp[top].int < 0) then 
      prterr(esubrng); { subscript out of range }
   inx := temp[top].int; { place index }
   top := top-1 { remove index }

end;

{******************************************************************************

Parse function reference

Parses a (x, y...) function reference. Expects the function variable entry,
and the expected return type. The return type must match the declared type
of the function. 
A list of parameters with their parsed expressions is prepared and checked
against the declaration types. Then, this list is moved into the variable
table, replacing identical names there. Then, the function body code is
executed, and the variables backed out.
Note that since functions take precidence over arrays (which are syntactically
identical), the array version of a variable is essentially unusable after
a function is declared.

******************************************************************************}

procedure parfnc(fp:  varptr;  { function entry }
                 typ: keycod); { result type }

var pp, vp: varptr; { variable pointers }
    vs, vl: varptr; { variable holding stack }

procedure parpar(pp: varptr); { parameter pointer }

var vp: varptr; { variable entry pointer }

begin

   if pp = nil then prterr(eparnum); { wrong number of parameters }
   getvarp(vp); { get a new variable entry }
   { place on holding list }
   if vs = nil then vs := vp { link to head }
   else vl^.next := vp; { link to next }
   vl := vp; { set last }
   vp^.inx := pp^.inx; { place variable index }
   vp^.nam := pp^.nam; { place variable name }
   expr; { parse parameter }
   case pp^.typ of { parameter type }

      vtint: begin { integer }

         cvtint; { convert to integer }
         if temp[top].typ <> tint then
            prterr(epartyp); { parameter type }
         vp^.int := temp[top].int { place value }

      end;

      vtrl: begin { real }

         cvtrl; { convert to real }
         if temp[top].typ <> trl then
            prterr(epartyp); { parameter type }
         vp^.rl := temp[top].rl { place value }

      end;

      vtstr: begin { string }

         if temp[top].typ <> tstr then
            prterr(epartyp); { parameter type }
         makstr(vp^.str); { make sure string exists }
         vp^.str^ := temp[top].bstr { place value }

      end

   end;
   top := top-1 { clean stack }

end;
   
begin

   case fp^.typ of { result type }

      vtint: if typ <> cintv then prterr(efncret); { wrong type }
      vtstr: if typ <> cstrv then prterr(efncret);
      vtrl:  if typ <> crlv then prterr(efncret)

   end;
   pshfns; { start new function level }
   { save all variables that are parameters }
   pp := fp^.par; { index 1st parameter }
   vs := nil; { clear holding stack }
   skpspc; { skip spaces }
   if chkchr = chr(ord(clpar)) then begin { parameters exist }

      getchr; { skip '(' }
      parpar(pp); { parse parameter }
      pp := pp^.par; { link next parameter }
      skpspc; { skip spaces }
      while chkchr = chr(ord(ccma)) do begin { parse parameters }

         getchr; { skip ',' }
         parpar(pp); { parse parameter }
         pp := pp^.par; { link next parameter }
         skpspc { skip spaces }
         
      end;
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr { skip ')' }

   end;
   if pp <> nil then prterr(eparnum); { wrong number of parameters }
   { save all variables under parameters }
   pp := fp^.par; { index parameters }
   while pp <> nil do begin { traverse }

      vp := vartbl[pp^.inx]; { index the entry }
      if vp <> nil then begin { variable exists }

         vp^.next := fnsstk^.vs; { push onto variable save list }
         fnsstk^.vs := vp

      end;
      pp := pp^.par { next parameter }

   end;
   { move holding variables to real }
   while vs <> nil do begin { move entry }

      vp := vs; { index top entry }
      vs := vs^.next; { gap from list }
      vartbl[vp^.inx] := vp

   end;
   fnsstk^.lin := curprg; { save current position }
   fnsstk^.chr := linec;
   curprg := fp^.line; { set position back for function }
   linec := fp^.chrp;
   { if multiline, go back to interpret. the function will get terminated
     at endfunc }
   if fp^.ml then begin { execute multiple lines }

      repeat { execute lines } 

         execl; { execute single lines }
         linec := 1 { set 1st character }

      until curprg = nil; { execute lines }
      if not fnsstk^.endf then 
         if fp^.fnc then prterr(enoendf) { no 'endfunc' found }
         else prterr(enoendp); { no 'endproc' found }

   end else expr; { parse function expression }
   curprg := fnsstk^.lin; { restore position }
   linec := fnsstk^.chr;
   { restore old variables }
   while fnsstk^.vs <> nil do begin

      vp := fnsstk^.vs; { index top entry }
      fnsstk^.vs := fnsstk^.vs^.next; { gap }
      clrvar(vartbl[vp^.inx]); { clear out subentries }
      putvarp(vartbl[vp^.inx]); { dispose old entry }
      vartbl[vp^.inx] := vp; { replace }
      vp^.next := nil

   end;
   popfns { release function stack entry }
   { now we exit with function value on stack }

end;

{******************************************************************************

Parse factor

Parses a factor, and leaves the result on the stack.

******************************************************************************}

procedure factor;
 
var i:          integer;
    r:          real;
    c:          char;
    vp:         vvcptr;
    x, y:       integer;
    fn:         integer; { file number }
    vap, vap1:  varptr;  { variable pointer }
    k:          keycod;  { keycode save }

begin { factor }

   skpspc;
   k := keycodc[ord(chkchr)]; { save starting tolken }
   if k = clpar then begin

      getchr; { skip '(' }
      expr;
      if chkchr <> chr(ord(crpar)) then prterr(erpe);
      getchr { skip '(' }

   end else if k = cadd then begin

      getchr; { skip '+' }
      factor;
      if (temp[top].typ <> tint) and
         (temp[top].typ <> trl) then prterr(ewtyp)

   end else if k = csub then begin

      getchr; { skip '-' }
      factor;
      { check real operand }
      if temp[top].typ = trl then temp[top].rl := -temp[top].rl
      { check integer operand }
      else if temp[top].typ = tint then
         temp[top].int := -temp[top].int
      else prterr(ewtyp) { wrong type }

   end else if k = cnot then begin

      getchr; { skip 'not' }
      factor;
      cvtint; { convert to integer }
      if temp[top].typ <> tint then prterr(einte); { integer expected }
      if (temp[top-1].int < 0) or (temp[top].int < 0) then prterr(ebolneg);
      temp[top].int := bnot(temp[top].int)

   end else if k = cstrc then loadstr { load string constant }
   else if k = cintc then begin

      top := top + 1;
      if top > maxstk then prterr(eexc);
      temp[top].typ := tint;
      temp[top].int := getint

   end else if k = crlc then begin

      top := top + 1;
      if top > maxstk then prterr(eexc);
      temp[top].typ := trl;
      temp[top].rl := getrl

   end else if k in [crlv, cintv, cstrv] then begin

      { variable access }
      getchr; { skip start tolken }
      y := ord(chkchr); { get variable character }
      getchr; { skip }
      if vartbl[y]^.fnc then { is a function }
         parfnc(vartbl[y], crlv) { parse function reference }
      else begin { variable }

         { record handles (r.) are typeless, and so become default reals by
           appearance. so we only process them here. any number of record
           handles can appear, but arrays must follow records. this limitation
           will disappear in time }
         vap := vartbl[y]; { get variable pointer }
         while chkchr = chr(ord(cperiod)) do begin { record fields }

            getchr; { skip '.' }
            if k <> crlv then prterr(etypfld);
            if not (ktrans[chkchr] in [cintv, crlv, cstrv]) then prterr(einvfld);
            k := keycodc[ord(chkchr)]; { save variable tolken }
            getchr; { skip start tolken }
            x := ord(chkchr); { get field index }
            getchr; { skip }
            fndfld(vap^.rec, x, vap1); { lookup or make field }
            vap := vap1 { copy back }
            
         end;
         if chkchr = chr(ord(clpar)) then begin { array reference }

            { parse array reference by type }
            if k = cstrv then { string }
               parvec(vap^.strv, vp, x, vvstr)
            else if k = cintv then { integer } 
               parvec(vap^.intv, vp, x, vvint)
            else { real }
               parvec(vap^.rlv, vp, x, vvrl);
            top := top+1; { add stack level }
            if top > maxstk then prterr(eexc); { stack overflow }
            { fetch operand by type }
            if k = cstrv then begin { string }

               makstr(vp^.str[x]); { make sure string exists }
               temp[top].typ := tstr; { type as string }
               temp[top].bstr := vp^.str[x]^ { place value }

            end else if k = cintv then begin { integer }

               temp[top].typ := tint; { type as integer }
               temp[top].int := vp^.int[x] { place value }

            end else begin { real }

               temp[top].typ := trl; { type as real }
               temp[top].rl := vp^.rl[x] { place value }

            end

         end else begin { ordinary reference }

            top := top+1; { add stack level }
            if top > maxstk then prterr(eexc); { stack overflow }
            if k = cstrv then begin { string }

               makstr(vap^.str); { make sure string exists }
               temp[top].typ := tstr; { type as string }
               temp[top].bstr := vap^.str^; { place value }

            end else if k = cintv then begin { integer }

               temp[top].typ := tint; { type as integer }
               temp[top].int := vap^.int { place value }

            end else begin { real }

               temp[top].typ := trl; { type as real }
               temp[top].rl := vap^.rl { place value }

            end

         end

      end

   end else if k in [cleft, cright, cmid] then begin

      { left$, right$ }
      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if temp[top].typ <> tstr then prterr(estre); { string expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(ccma)) then prterr(ecmaexp); { ',' expected }
      getchr; { skip ',' }
      expr; { parse expression }
      cvtint; { convert to integer }
      if temp[top].typ <> tint then prterr(einte); { integer expected }
      if temp[top].int < 0 then prterr(estrinx); { index is negative }
      skpspc; { skip spaces }
      if c <> chr(ord(cmid)) then begin { left$ or right$ }

         if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
         getchr; { skip ')' }
         if temp[top].int > temp[top-1].bstr.len then prterr(estrinx);
         if c = chr(ord(cright)) then { right$ }
            for i := 1 to temp[top].int do { move string left }
               temp[top-1].bstr.str[i] := 
                  temp[top-1].bstr.str[i+temp[top-1]
                    .bstr.len-temp[top].int];
         { set new length left }
         temp[top-1].bstr.len := temp[top].int;
         top := top-1 { clean stack }

      end else begin { mid$ }

         if temp[top].int = 0 then prterr(estrinx); { index is zero }
         if chkchr <> chr(ord(ccma)) then prterr(ecmaexp); { ',' expected }
         getchr; { skip ',' }
         expr; { parse end expression }
         cvtint; { convert to integer }
         if temp[top].typ <> tint then prterr(einte); { integer expected }
         if temp[top].int < 0 then prterr(estrinx); { index is negative }
         skpspc; { skip spaces }
         if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
         getchr; { skip ')' }
         { check requested length > string length }
         if temp[top].int+temp[top-1].int-1 >
            temp[top-2].bstr.len then 
            prterr(estrinx);
         for i := 1 to temp[top].int do { move string left }
            temp[top-2].bstr.str[i] :=
               temp[top-2].bstr.str[i+temp[top-1].int-1];
         { set new length left }
         temp[top-2].bstr.len := temp[top].int;
         top := top-2 { clean stack }

      end
    
   end else if k = cchr then begin { chr$ }

      getchr; { skip 'chr$' }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtint; { convert to integer }
      if temp[top].typ <> tint then prterr(einte); { integer expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr; { skip ')' }
      x := temp[top].int; { save integer }
      if (x > 255) or (x < 0) then 
         prterr(echrrng); { out of range for character }
      temp[top].typ := tstr; { change to string type }
      temp[top].bstr.len := 1; { set single character }
      temp[top].bstr.str[1] := chr(x) { place result }

   end else if k = casc then begin { asc }

      getchr; { skip 'asc' }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if temp[top].typ <> tstr then prterr(estre); { string expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr; { skip ')' }
      if temp[top].bstr.len < 1 then prterr(estrinx); { check valid }
      c := temp[top].bstr.str[1]; { get the 1st character }
      temp[top].typ := tint; { change to integer }
      temp[top].int := ord(c) { place result }

   end else if k = clen then begin { len }

      getchr; { skip 'len' }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if temp[top].typ <> tstr then prterr(estre); { string expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr; { skip ')' }
      x := temp[top].bstr.len; { save length }
      temp[top].typ := tint; { change to integer }
      temp[top].int := x { place result }

   end else if k = cval then begin { val }

      getchr; { skip tolken }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if temp[top].typ <> tstr then prterr(estre); { string expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr; { skip ')' }
      i := getbval(temp[top].bstr); { get string value }
      temp[top].typ := tint; { change to integer }
      temp[top].int := i { place result }

   end else if k = cstr then begin { str$ }

      getchr; { skip tolken }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr; { skip ')' }
      if temp[top].typ = tint then begin { integer }

         i := temp[top].int; { get value }
         temp[top].typ := tstr; { change to string }
         putbval(temp[top].bstr, i, 0) { place value in ascii }

      end else if temp[top].typ = trl then begin { real }

         r := temp[top].rl; { get value }
         temp[top].typ := tstr; { change to string }
         putrl(temp[top].bstr, r, 0) { place value in ascii }

      end else prterr(ewtyp) { wrong type }

   end else if k = crnd then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr = chr(ord(clpar)) then begin { parameter exists }

         { the possible parameter to rnd is ignored, as per spec }
         getchr; { skip '(' }
         expr; { parse expression }
         cvtrl; { convert to real }
         if temp[top].typ <> trl then prterr(enumexp); { number expected }
         skpspc; { skip spaces }
         if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
         getchr; { skip ')' }
         top := top-1 { purge value }

      end;
      top := top+1; { add stack level }
      if top > maxstk then prterr(eexc); { stack overflow }
      temp[top].typ := trl; { type as real }
      temp[top].rl := rand { place random number }
      
   end else if k = cint then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      { truncation would seem to dictate the convertion of the operand
        to integer. But the accuracy of integer is not as great as
        that of float, and some programs rely on the basic leaving it
        as a float }
      if ((temp[top].rl - trunc(temp[top].rl)) <> 0) and
         (temp[top].rl < 0) then { process negative case }
         temp[top].rl := trunc(temp[top].rl)-1 { find truncation }
      else { positive case }
         temp[top].rl := trunc(temp[top].rl) { find truncation }
          
   end else if k = csqr then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      temp[top].rl := sqrt(temp[top].rl) { find square root }
    
   end else if k = cabs then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if (temp[top].typ <> trl) and
         (temp[top].typ <> tint) then 
         prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      { find square }
      if temp[top].typ = tint then
         temp[top].int := abs(temp[top].int)
      else temp[top].rl := abs(temp[top].rl)
    
   end else if k = csgn then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if (temp[top].typ <> trl) and (temp[top].typ <> tint) then 
         prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      { find square }
      if temp[top].typ = tint then begin { integer }

         if temp[top].int > 0 then x := 1
         else if temp[top].int = 0 then x := 0
         else x := -1

      end else begin { real }

         if temp[top].rl > 0 then x := 1
         else if temp[top].rl = 0 then x := 0
         else x := -1

      end;
      temp[top].typ := tint; { set integer }
      temp[top].int := x { place result }

   end else if k = csin then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      temp[top].rl := sin(temp[top].rl) { find sin(x) }
    
   end else if k = ccos then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      temp[top].rl := cos(temp[top].rl) { find cos(x) }
    
   end else if k = ctan then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      temp[top].rl := sin(temp[top].rl)/cos(temp[top].rl) { find tan(x) }
    
   end else if k = catn then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      temp[top].rl := arctan(temp[top].rl) { find atn(x) }
    
   end else if k = clog then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      temp[top].rl := ln(temp[top].rl) { find log(x) }
    
   end else if k = cexp then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      cvtrl; { convert to real }
      if temp[top].typ <> trl then prterr(enumexp); { number expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr; { skip ')' }
      temp[top].rl := exp(temp[top].rl) { find exp(x) }
    
   end else if k = clcase then begin

      getchr; { skip 'lcase' }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if temp[top].typ <> tstr then prterr(estre); { string expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr; { skip ')' }
      for i := 1 to temp[top].bstr.len do begin { convert case }

         { check upper case }
         if (ord(temp[top].bstr.str[i]) >= ord('A')) and
            (ord(temp[top].bstr.str[i]) <= ord('Z')) then { convert }
            temp[top].bstr.str[i] :=
               chr(ord(temp[top].bstr.str[i])-ord('A')+ord('a'))

      end

   end else if k = cucase then begin

      getchr; { skip 'ucase' }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      expr; { parse expression }
      if temp[top].typ <> tstr then prterr(estre); { string expected }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr; { skip ')' }
      for i := 1 to temp[top].bstr.len do begin { convert case }

         { check lower case }
         if (ord(temp[top].bstr.str[i]) >= ord('a')) and
            (ord(temp[top].bstr.str[i]) <= ord('z')) then { convert }
            temp[top].bstr.str[i] :=
               chr(ord(temp[top].bstr.str[i])-ord('a')+ord('A'))

      end

   end else if k = ceof then begin

      getchr; { skip tolken }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
      getchr; { skip '(' }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(cpnd)) then prterr(epndexp); { '#' expected }
      getchr; { skip '#' }
      expr; { parse expression }
      cvtint; { convert to integer }
      fn := temp[top].int; { get file number }
      if (fn < 1) or (fn > maxfil) then prterr(einvfnum); { invalid file number }
      if filtab[fn] = nil then prterr(efilnop); { file not open }
      if filtab[fn]^.st = stclose then prterr(efilnop); { file not open }
      if eof(filtab[fn]^.f) then temp[top].int := -1 else temp[top].int := 0;
      skpspc; { skip spaces }
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { '(' expected }
      getchr { skip ')' }

   end else prterr(eifact)

end; { factor }

{******************************************************************************

Parse exponent factor

Parses an exponent factor. This is separated out because many programs
rely on exponentiation being a high priority operator.

******************************************************************************}

procedure expf;
 
var r: real;
    a: real;
    i: integer;

begin { exponent factor }

   factor;
   skpspc;
   while ktrans[chkchr] = cexpn do begin { ^ }

      getchr; { skip '^' }
      factor;
      { convert both operands to real }
      cvtrl;
      cvtrls;
      if (temp[top].typ <> trl) or (temp[top - 1].typ <> trl) then
         prterr(ewtyp); { wrong type }
      { exp is not implemented in the encoder, do it the stupid way.
        This will not work for fractioned powers }
      {temp[top-1].rl := exp(temp[top].rl*ln(temp[top-1].rl));} { find ^ }
      cvtint;
      r := temp[top-1].rl;
      a := r;
      for i := 2 to temp[top].int do a := a*r;
      temp[top-1].rl := a;
      top := top-1;
      skpspc { skip spaces }

   end;

end;

{******************************************************************************

Parse term

Parses a term, and leaves the result on the stack.

******************************************************************************}

procedure term;
 
begin { term }

   expf;
   skpspc;
   while ktrans[chkchr] in [cmult, cdiv, cmod, cidiv] do 
      begin

      case ktrans[chkchr] of { tolken }

         cmult: begin { * }
   
            getchr; { skip '*' }
            expf;
            binprp; { prepare binary }
            if (temp[top].typ = trl) and (temp[top-1].typ = trl) then
               { perform in real }
               temp[top-1].rl := temp[top-1].rl*temp[top].rl
            else if (temp[top].typ <> tint) or (temp[top - 1].typ <> tint) then
               prterr(ewtyp) { wrong type }
            { perform in integer }
            else temp[top-1].int := temp[top-1].int*temp[top].int;
            top := top-1

         end;
   
         cdiv: begin { / }
   
            getchr; { skip '/' }
            expf;
            { convert both operands to real }
            cvtrl;
            cvtrls;
            if (temp[top].typ <> trl) or (temp[top - 1].typ <> trl) then
               prterr(ewtyp); { wrong type }
            if temp[top].rl = 0.0 then prterr(ezerdiv); { divide by zero }
            temp[top-1].rl := temp[top-1].rl/temp[top].rl; { find / }
            top := top-1

         end;
   
         cmod: begin { mod }
   
            getchr; { skip 'mod' }
            expf;
            cvtint; { convert top to integer }
            cvtints; { convert second to integer }
            if (temp[top].typ <> tint) or
               (temp[top-1].typ <> tint) then prterr(ewtyp);
            if temp[top].int = 0 then prterr(ezerdiv); { divide by zero }
            temp[top-1].int := temp[top-1].int mod temp[top].int;
            top := top-1

         end;

         cidiv: begin { integer divide }
   
            getchr; { skip 'div' }
            expf;
            cvtint; { convert top to integer }
            cvtints; { convert second to integer }
            if (temp[top].typ <> tint) or
               (temp[top-1].typ <> tint) then prterr(ewtyp);
            temp[top-1].int := temp[top-1].int div temp[top].int;
            top := top-1

         end

      end;
      skpspc { skip spaces }

   end

end; { term }

{******************************************************************************

Parse simple expression

Parses a simple expression, and leaves the result on the stack.

******************************************************************************}

procedure sexpr;

begin { sexpr }

   term;
   skpspc;
   while ktrans[chkchr] in [cadd, csub] do begin

      case ktrans[chkchr] of { tolken }

         cadd: begin

            getchr; { skip '+' }
            term;
            if temp[top].typ = tstr then begin
   
               if temp[top - 1].typ <> tstr then prterr(estyp);
               catbs(temp[top - 1].bstr, temp[top].bstr);
               top := top - 1
   
            end else begin
   
               binprp; { prepare binary }
               if (temp[top].typ = trl) and (temp[top-1].typ = trl) then
                  { perform in real }
                  temp[top-1].rl := temp[top-1].rl+temp[top].rl
               else if (temp[top].typ <> tint) or (temp[top - 1].typ <> tint) then
                  prterr(estyp) { wrong type }
               { perform in integer }
               else temp[top-1].int := temp[top-1].int+temp[top].int;
               top := top-1;
   
            end

         end;
   
         csub: begin { - }
   
            getchr; { skip '-' }
            term;
            binprp; { prepare binary }
            if (temp[top].typ = trl) and (temp[top-1].typ = trl) then
               { perform in real }
               temp[top-1].rl := temp[top-1].rl-temp[top].rl
            else if (temp[top].typ <> tint) or (temp[top - 1].typ <> tint) then
               prterr(estyp) { wrong type }
            { perform in integer }
            else temp[top-1].int := temp[top-1].int-temp[top].int;
            top := top-1
   
         end

      end;
      skpspc { skip spaces }

   end

end; { sexpr }

{******************************************************************************

Parse equality expression

Parses an equality expression, and leaves the result on the stack.

******************************************************************************}
 
procedure eexpr;

begin { eexpr }

   sexpr; { parse simple expression }
   skpspc; { skip spaces }
   while ktrans[chkchr] in [cequ, cnequ, cltn, cgtn, clequ, cgequ] do begin

      case ktrans[chkchr] of { tolken }

         cequ: begin
   
            getchr; { skip '=' }
            sexpr;
            if temp[top].typ = tstr then begin { string }

               if temp[top-1].typ <> tstr then prterr(estyp); { not same type }
               if strequ(temp[top-1].bstr, temp[top].bstr) then { set result }
                     begin top := top-1; settrue end
                  else 
                     begin top := top-1; setfalse end

            end else { integer/real }
               if chkequ then begin top := top-1; settrue end 
               else begin top := top-1; setfalse end
   
         end;
   
         cnequ: begin
   
            getchr; { skip '<>' }
            sexpr;
            if temp[top].typ = tstr then begin { string }

               if temp[top-1].typ <> tstr then prterr(estyp); { not same type }
               if strequ(temp[top-1].bstr, temp[top].bstr) then { set result }
                     begin top := top-1; setfalse end
                  else 
                     begin top := top-1; settrue end

            end else { integer/real }
               if chkequ then begin top := top-1; setfalse end
               else begin top := top-1; settrue end
   
         end;
   
         cltn: begin
   
            getchr; { skip '<' }
            sexpr;
            if temp[top].typ = tstr then begin { string }

               if temp[top-1].typ <> tstr then prterr(estyp); { not same type }
               if strltn(temp[top-1].bstr, temp[top].bstr) then { set result }
                     begin top := top-1; settrue end
                  else 
                     begin top := top-1; setfalse end 

            end else { integer/real }
               if chkltn then begin top := top-1; settrue end
               else begin top := top-1; setfalse end
   
         end;
   
         cgtn: begin
   
            getchr; { skip '>' }
            sexpr;
            if temp[top].typ = tstr then begin { string }

               if temp[top-1].typ <> tstr then prterr(estyp); { not same type }
               if strltn(temp[top].bstr, temp[top-1].bstr) then { set result }
                     begin top := top-1; settrue end
                  else 
                     begin top := top-1; setfalse end

            end else { integer/real }
               if chkgtn then begin top := top-1; settrue end
               else begin top := top-1; setfalse end
   
         end;
   
         clequ: begin
   
            getchr; { skip '<=' }
            sexpr;
            if temp[top].typ = tstr then begin { string }

               if temp[top-1].typ <> tstr then prterr(estyp); { not same type }
               if strltn(temp[top].bstr, temp[top-1].bstr) then { set result }
                     begin top := top-1; setfalse end
                  else 
                     begin top := top-1; settrue end

            end else { integer/real }
               if chkgtn then begin top := top-1; setfalse end
               else begin top := top-1; settrue end
   
         end;

         cgequ: begin
   
            getchr; { '>=' }
            sexpr;
            if temp[top].typ = tstr then begin { string }

               if temp[top-1].typ <> tstr then prterr(estyp); { not same type }
               if strltn(temp[top-1].bstr, temp[top].bstr) then { set result }
                     begin top := top-1; setfalse end
                  else 
                     begin top := top-1; settrue end

            end else { integer/real }
               if chkltn then begin top := top-1; setfalse end
               else begin top := top-1; settrue end
   
         end

      end;
      skpspc { skip spaces }

   end

end; { eexpr }

{******************************************************************************

Parse expression

Parses an expression, and leaves the result on the stack.

******************************************************************************}
 
procedure expr;

begin { expr }

   eexpr; { parse equality expression }
   skpspc; { skip spaces }
   while ktrans[chkchr] in [cand, cxor, cor] do begin

      case ktrans[chkchr] of { tolken }

         cand: begin { and }
   
            getchr; { skip 'and' }
            eexpr;
            cvtint; { convert top to integer }
            cvtints; { convert second to integer }
            if (temp[top].typ <> tint) or
               (temp[top-1].typ <> tint) then prterr(ewtyp);
            if (temp[top-1].int < 0) or (temp[top].int < 0) then prterr(ebolneg);
            temp[top-1].int := band(temp[top-1].int, temp[top].int);
            top := top-1

         end;

         cxor: begin { xor }
   
            getchr; { skip 'xor' }
            eexpr;
            cvtint; { convert top to integer }
            cvtints; { convert second to integer }
            if (temp[top].typ <> tint) or
               (temp[top-1].typ <> tint) then prterr(ewtyp);
            if (temp[top-1].int < 0) or (temp[top].int < 0) then prterr(ebolneg);
            temp[top-1].int := bxor(temp[top-1].int, temp[top].int);
            top := top-1

         end;

         cor: begin { or }
   
            getchr; { skip 'or' }
            eexpr;
            cvtint; { convert top to integer }
            cvtints; { convert second to integer }
            if (temp[top].typ <> tint) or
               (temp[top-1].typ <> tint) then prterr(ewtyp);
            if (temp[top-1].int < 0) or (temp[top].int < 0) then prterr(ebolneg);
            temp[top-1].int := bor(temp[top-1].int, temp[top].int);
            top := top-1

         end

      end;
      skpspc { skip spaces }

   end

end; { expr }

{******************************************************************************

Input variable

Handles the 'input' statement.

******************************************************************************}

procedure sinput;

var c:       char;     { character save }
    v:       char;     { variable save }
    ts, ts1: bstringt; { temp string }
    i:       integer;  { index for temp strings }
    ti:      integer;  { temp integer }
    tr:      real;     { temp real }
    isreal:  boolean;  { real/integer flag }
    vp:      vvcptr;   { vector pointer }
    x:       integer;  { vector index }
    fn:      integer;  { file number to use }
    fp:      boolean;  { file was parsed }

begin

   fn := 1; { set default input file }
   fp := false; { set no filenumber parsed }
   getchr; { skip 'input' }
   skpspc; { skip spaces }
   if chkchr = chr(ord(cpnd)) then begin { there is a file number }

      getchr; { skip '#' }
      expr; { parse file number }
      cvtint; { convert to integer }
      fn := temp[top].int; { get file number }
      top := top-1; { clean stack }
      if (fn < 1) or (fn > maxfil) then
         prterr(einvfnum); { invalid file number }
      if filtab[fn] = nil then prterr(efilnop); { file is not open }
      if filtab[fn]^.st = stclose then prterr(efilnop); { file is not open }
      if filtab[fn]^.st <> stopenrd then prterr(einvmod); { invalid file mode }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(ccma)) then prterr(ecmaexp); { ',' expected }
      getchr; { skip ',' }
      fp := true { set file was parsed }

   end;
   skpspc; { skip spaces }
   if chkchr = chr(ord(cstrc)) then begin { prompt is specified }

      if fp then prterr(epmtfil); { no prompt allowed on file }
      loadstr; { load the string }
      prtbstr(filtab[2]^, temp[top].bstr); { print it }
      skpspc; { skip spaces }
      if chkchr = chr(ord(cscn)) then write('? '); { add ? postamble }
      if (chkchr <> chr(ord(cscn))) and (chkchr <> chr(ord(ccma))) then
         prterr(esccmexp); { ';' or ',' expected }
      getchr { skip ';' or ',' }
      
   end else if not fp then write('? '); { ouput prompt by default }
   { input user string to get input from }
   if filtab[fn]^.ty = tyinp then inpbstr(input, ts)
   else inpbstr(filtab[fn]^.f, ts);
   if fn = 1 then begin { default input, process reset line on default output }

      rsttab(filtab[2]^); { reset tabbing }
      newlin := true; { set on new line }

   end;
   i := 1; { set 1st input character }
   repeat { parse destination variables }

      skpspc; { skip spaces }
      { check next is variable }
      if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then prterr(evare);
      c := chkchr; { save head type }
      getchr; { skip }
      v := chkchr; { get variable }
      getchr; { skip }
      vp := nil; { set ordinary access }
      skpspc; { skip spaces }
      if chkchr = chr(ord(clpar)) then { array reference }
         { parse array reference by type }
         if c = chr(ord(cstrv)) then { string }
            parvec(vartbl[ord(v)]^.strv, vp, x, vvstr)
         else if c = chr(ord(cintv)) then { integer } 
            parvec(vartbl[ord(v)]^.intv, vp, x, vvint)
         else { real }
            parvec(vartbl[ord(v)]^.rlv, vp, x, vvrl);
      skpspc; { skip spaces }
      if c = chr(ord(cstrv)) then begin
      
         { parse string out of input }
         if chkchr = chr(ord(ccma)) then begin

            { string is part of multiple input, read up to ',' }
            ts1.len := 0; { clear destination string }
            while (i <= ts.len) and (ts.str[i] <> ',') do begin

               { transfer characters }
               ts1.len := ts1.len+1; { count characters }
               ts1.str[ts1.len] := ts.str[i]; { place next character }
               i := i+1 { next character }

            end

         end else begin { use the whole string }

            ts1.len := 0; { clear destination string }
            while i <= ts.len do begin

               { transfer characters }
               ts1.len := ts1.len+1; { count characters }
               ts1.str[ts1.len] := ts.str[i]; { place next character }
               i := i+1 { next character }

            end

         end;
         if vp <> nil then begin { array assign }

            makstr(vp^.str[x]); { make sure string exists }
            vp^.str[x]^ := ts1 { place value }
            
         end else begin { ordinary reference }

            makstr(vartbl[ord(v)]^.str); { make sure string exists }
            vartbl[ord(v)]^.str^ := ts1 { place value }

         end
      
      end else if c = chr(ord(cintv)) then begin
      
         parnum(ts, i, true, isreal, ti, tr); { get number from string }
         { if the number is real, convert to integer }
         if isreal then ti := round(tr);
         if vp <> nil then { array assign }
            vp^.int[x] := ti { place value }
         else { ordinary reference }
            vartbl[ord(v)]^.int := ti
      
      end else begin
      
         parnum(ts, i, true, isreal, ti, tr); { get number from string }
         { if the number is integer, convert to real }
         if not isreal then tr := ti;
         if vp <> nil then { array assign }
            vp^.rl[x] := tr { place value }
         else { ordinary reference }
            vartbl[ord(v)]^.rl := tr
      
      end;
      skpspc; { skip spaces }
      c := chkchr; { check next character }
      if c = chr(ord(ccma)) then begin

         getchr; { skip ',' }
         { skip spaces in input string }
         while (i <= ts.len) and (ts.str[i] = ' ') do i := i+1;
         if (i > ts.len) or (ts.str[i] <> ',') then prterr(einsinp);
         i := i+1 { skip ',' }

      end

   until c <> chr(ord(ccma)) { until not ',' }

end;

{******************************************************************************

Print

Handles the 'print' statement.

print [using <str>;] x

The "using" string is registered once, and used from left to right until there
are no more parameters.
Using format characters are:

'&': Output a string. If a string argument appears, otherwise error.
'$': Output leading dollar sign/digit. If a significant digit occupys the
position, it is placed there. If the msd is next, a '$' is placed there.
Otherwise, a space is printed.
'#': Output digit. If a significant digit appears, output it. Otherwise, a
space is output.
'0': Output digit. If a significant digit appears, output it. Otherwise, a
'0' is output.
'.': Decimal point. The decimal point is aligned with this. Msds are output
right, and as many fraction digits as are specified are output.
',': Comma. If there are msds to the left of this, a comma is output,
otherwise a space.
'+': Any sign is output. Must be placed at any possible sign position.
'-': Only negative signs are output, otherwise space.
'\': Force next character.

There must be no spaces between format characters. Successive parameters are
separated by spaces in the "using" string.

******************************************************************************}

procedure sprint;

var c:    char;
    fn:   integer;   { file number to use }
    fs:   packed array [1..maxstr] of char; { holder for format string }
    fl:   0..maxstr; { length of string }
    fi:   1..maxstr; { index for same }
    i:    1..maxstr;

{ print parameter with formatted string }

procedure prtfmt(var fr: frcety);

var deci:   boolean; { decimal point was seen }
    sigd:   integer; { number of significant digits before decimal point or end }
    { storage for parts of a number }
    manstr: bstring; { buffer for mantissa/integer }
    manlen: integer; { length of mantissa }
    frcstr: bstring; { buffer for fraction }
    expstr: bstring; { buffer for exponent }
    sign:   char;      { sign of number }
    fmterr: boolean; { format error was encountered }
    ldig:   boolean; { leading digit was output }
    dlrcnt: integer; { '$' count }
    sgncnt: integer; { sign count }

{i: integer;}

{ check next character in format string }

function fchkchr: char;

var c: char; { character buffer }

begin

   if fi > maxstr then c := ' ' { replace off end with space }
   else c := fs[fi]; { else return next character }
   fchkchr := c

end;

{ extract character from string }

procedure extchr(var s: bstring; var c: char);

var i: 1..maxstr;

begin

   c := s[1]; { return the first character in the string }
   for i := 1 to maxstr-1 do s[i] := s[i+1] { gap string }

end;

{ print a single digit }

procedure prtdig(var fr: frcety; zero: boolean); { print leading zeros }

var tc: char;

begin

   if deci then begin { past decimal point, process as fraction }

      if fmterr then prtchr(fr, '?') { output error fill }
      else begin { normal output }

         { if the fraction is empty, then output '0' }
         if frcstr[1] = ' ' then prtchr(fr, '0') { print filler }
         else begin

            extchr(frcstr, tc); { extract digit }
            prtchr(fr, tc); { print }

         end

      end

   end else begin { before decimal point, process as number }

      if fmterr then prtchr(fr, '?') { output error fill }
      else if sigd > manlen then begin { we are to the left of number yet }

         if zero then prtchr(fr, '0') { zero pad }
         else prtchr(fr, ' ') { space pad }

      end else begin { we are printing digits }

         extchr(manstr, tc); { extract digit }
         prtchr(fr, tc); { print the leftmost digit }
         manlen := manlen-1; { count }
         ldig := true { set leading digit output }

      end;
      sigd := sigd-1 { count off digits }

   end

end;

{ print formatted character from parameters }

procedure prtfchr(var fr: frcety; { file to print }
                      c:  char);  { special character }

var tc: char;

begin

   case c of { special }

      '&': begin { output string }

         if temp[top].typ <> tstr then prterr(estre); { string expected }
         prtbstr(filtab[fn]^, temp[top].bstr) { print string }
 
      end;

      '$': begin { dollar sign }

         if (temp[top].typ <> tint) and (temp[top].typ <> trl) then
            prterr(enumexp); { numeric expected }
         if deci then prterr(ebadfmt); { already passed decimal point }
         { if the leading digit is about to be printed, output '$' }
         if (sigd > manlen) and (dlrcnt > 1) then begin { pad left }

            prtchr(fr, ' ');
            sigd := sigd-1

         end else if dlrcnt = 1 then prtchr(fr, '$') { output }
         else prtdig(fr, false); { output digit }
         dlrcnt := dlrcnt-1 { count '$' }

      end;

      ',': begin { comma divider }

         if (temp[top].typ <> tint) and (temp[top].typ <> trl) then
            prterr(enumexp); { numeric expected }
         if ldig then prtchr(fr, ',') { output comma }
         else prtchr(fr, ' ') { replace with space }

      end;

      '#', '0': begin { output digit or space }

         if (temp[top].typ <> tint) and (temp[top].typ <> trl) then
            prterr(enumexp); { numeric expected }
         prtdig(fr, c = '0') { print digit }

      end;

      '-': begin { output sign if negative }

         if (temp[top].typ <> tint) and (temp[top].typ <> trl) then
            prterr(enumexp); { numeric expected }
         if deci then prterr(ebadfmt); { already passed decimal point }
         { if the leading digit is about to be printed, output sign }
         if (sigd > manlen) and (sgncnt > 1) then begin { pad left }

            prtchr(fr, ' ');
            sigd := sigd-1

         end else if sgncnt = 1 then begin { output sign }

            if sign = '-' then prtchr(fr, sign) { print }
            else prtchr(fr, ' ');

         end else prtdig(fr, false); { output digit }
         sgncnt := sgncnt-1 { count signs }

      end;

      '+': begin { output any sign }

         if (temp[top].typ <> tint) and (temp[top].typ <> trl) then
            prterr(enumexp); { numeric expected }
         if deci then prterr(ebadfmt); { already passed decimal point }
         { if the leading digit is about to be printed, output sign }
         if sigd = manlen+1 then prtchr(fr, sign)
         else prtdig(fr, false) { output digit }

      end;

      '.': begin { decimal point }

         if (temp[top].typ <> tint) and (temp[top].typ <> trl) then
            prterr(enumexp); { numeric expected }
         if deci then prterr(ebadfmt); { already passed decimal point }
         deci := true; { set we have crossed decimal point }
         prtchr(fr, c); { print }

      end;

      '^': begin { exponent }

         if (temp[top].typ <> tint) and (temp[top].typ <> trl) then
            prterr(enumexp); { numeric expected }
         extchr(expstr, tc); { get next exponent character }
         prtchr(fr, tc) { print }

      end

   end

end;

{ find significant digits }

procedure fndsig;

var fis:    integer; { save for format index }
    dollar: boolean; { dollar sign was seen }
    sign:   boolean; { sign was seen }

begin

   sigd := 0; { clear significant digits }
   fis := fi; { save format index }
   dollar := false; { set no '$' }
   sign := false; { set no '-'/'=' }
   while fchkchr in ['$', ',', '#', '-', '+', '0'] do begin

      if fchkchr = '$' then dlrcnt := dlrcnt+1; { count '$' }
      if (fchkchr = '-') or (fchkchr = '+') then
         sgncnt := sgncnt+1; { count signs }
      if fchkchr in ['#', '0'] then sigd := sigd+1; { count significant digits }
      { if more than one dollar sign or sign appears in a row, count the excess
        over one as digit positions }
      if (fchkchr = '$') and dollar then sigd := sigd+1;
      if ((fchkchr = '-') or (fchkchr = '+')) and sign then sigd := sigd+1;
      if fchkchr = '$' then dollar := true; { set passed '$' }
      if (fchkchr = '+') or (fchkchr = '-') then sign := true;
      fi := fi+1 { next }

   end;
   fi := fis { restore index }

end;

{ unpack integer }

procedure unpint;

var p: bstptr; { temp holding string }
    i: integer;

begin

   getstr(p); { get a temp string }
   putbval(p^, temp[top].int, 1); { place value in string }
   { place back in mantissa }
   for i := 1 to maxstr do manstr[i] := ' '; { clear target }
   { copy }
   for i := 1 to p^.len do manstr[i] := p^.str[i];
   manlen := p^.len;
   putstr(p); { release string }
   sign := '+'; { default to positive } 
   if manstr[1] = '-' then begin { its negative }

      extchr(manstr, sign); { remove the sign }
      manlen := manlen-1

   end;
   { clear fraction }
   for i := 1 to maxstr do frcstr[i] := ' '; { clear target }
   { clear exponent }
   for i := 1 to maxstr do expstr[i] := ' '; { clear target }
   { place "default" exponent }
   expstr[1] := 'e';
   expstr[2] := '+';
   expstr[3] := '0';
   expstr[4] := '0';
   expstr[5] := '0';

end;

{ unpack real }

procedure unprl;

var p:    bstptr; { temp holding string }
    i, t: integer;

begin

   getstr(p); { get a temp string }
   putrl(p^, temp[top].rl, 1); { place value in string }
   { place back in mantissa }
   for i := 1 to maxstr do manstr[i] := ' '; { clear target }
   { copy }
   for i := 1 to p^.len do manstr[i] := p^.str[i];
   manlen := p^.len;
   putstr(p); { release string }
   sign := '+'; { default to positive } 
   if manstr[1] = '-' then begin { its negative }

      extchr(manstr, sign); { remove the sign }
      manlen := manlen-1

   end;
   { clear fraction }
   for i := 1 to maxstr do frcstr[i] := ' '; { clear target }
   { find any fraction }
   t := 1;
   while (t < maxstr) and (manstr[t] <> '.') do t := t+1;
   if manstr[t] = '.' then begin { there was a fraction }

      manstr[t] := ' '; { blank out '.' in mantissa }
      manlen := manlen-1;
      t := t+1; { skip '.' }
      i := 1; { set 1st destination }
      while (t < maxstr) and (manstr[t] <> 'e') and (manstr[t] <> ' ') do begin

         frcstr[i] := manstr[t]; { move fraction characters }
         manstr[t] := ' '; { blank out in mantissa }
         manlen := manlen-1;
         t := t+1; { advance }
         i := i+1

      end

   end;
   { clear exponent }
   for i := 1 to maxstr do expstr[i] := ' '; { clear target }
   { place "default" exponent }
   expstr[1] := 'e';
   expstr[2] := '+';
   expstr[3] := '0';
   expstr[4] := '0';
   expstr[5] := '0';
   { find any exponent }
   t := 1;
   while (t < maxstr) and (manstr[t] <> 'e') do t := t+1;
   if manstr[t] = 'e' then begin { there was an exponent }

      i := 1; { set 1st destination }
      while (t < maxstr) and (manstr[t] <> ' ') do begin

         expstr[i] := manstr[t]; { move exponent characters }
         manstr[t] := ' '; { blank out in mantissa }
         manlen := manlen-1;
         t := t+1; { advance }
         i := i+1

      end

   end

end;

{ print format filler }

procedure prtfil(var fr: frcety);

begin

   while not (fchkchr in ['&', '$', ',', '#', '-', '+', '0', '.', '^']) and 
         (fi <= fl) do begin

      if fchkchr = '\' then begin { process force sequence }

         fi := fi+1;
         if fi <= fl then begin

            prtchr(fr, fchkchr); { output escaped character }
            fi := fi+1 { next }

         end

      end else begin

         prtchr(fr, fchkchr); { output character }
         fi := fi+1 { next }

      end

   end

end;

begin { prtfmt }

   if fi > fl then prterr(enulusg); { end of format string }
   deci := false; { set no decimal point seen }
   ldig := false; { set no leading digit output }
   dlrcnt := 0; { clear '$' count }
   sgncnt := 0; { clear sign count }
   { unpack integer or real operands }
   if temp[top].typ = tint then unpint { integer }
   else if temp[top].typ = trl then unprl; { real }

   { check if there are enough significant digits to contain the mantissa }
   fmterr := false; { set no formatting error }
   prtfil(fr); { print leading filler }
   if (temp[top].typ = trl) or (temp[top].typ = tint) then begin

      fndsig; { find significant digits in format }
      if sigd < manlen then fmterr := true { set formatting error if not }

   end;

   if not (fchkchr in ['&', '$', ',', '#', '-', '+', '0', '.', '^']) then
      prterr(efmtnf);
   { output characters for formatting }
   while fchkchr in ['&', '$', ',', '#', '-', '+', '0', '.', '^'] do begin

      prtfchr(fr, fchkchr);
      fi := fi+1; { next }
      prtfil(fr) { print any filler }

   end

end;

begin { sprint }

   fn := 2; { set default output file }
   fl := 0; { set no format string }
   fi := 1; { index start of format string }
   getchr; { skip 'print' }
   skpspc; { skip spaces }
   if chkchr = chr(ord(cpnd)) then begin { there is a file number }

      getchr; { skip '#' }
      expr; { parse file number }
      cvtint; { convert to integer }
      fn := temp[top].int; { get file number }
      top := top-1; { clean stack }
      if (fn < 1) or (fn > maxfil) then
         prterr(einvfnum); { invalid file number }
      if filtab[fn] = nil then prterr(efilnop); { file is not open }
      if filtab[fn]^.st = stclose then prterr(efilnop); { file is not open }
      if filtab[fn]^.st <> stopenwr then prterr(einvmod); { invalid file mode }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(ccma)) then prterr(ecmaexp); { ',' expected }
      getchr { skip ',' }

   end;
   c := chr(ord(clend)); { set next to nothing }
   while not chksend do begin { list items }

      skpspc; { skip spaces }
      if chkchr = chr(ord(ctab)) then begin { parse tab }

         getchr; { skip 'tab' }
         skpspc; { skip spaces }
         if chkchr <> chr(ord(clpar)) then prterr(elpe); { '(' expected }
         getchr; { skip '(' }
         expr; { parse tab expression }
         cvtint; { convert to integer }
         if temp[top].typ <> tint then 
            prterr(einte); { integer expected }
         skpspc; { skip spaces }
         if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
         getchr; { skip ')' }
         tab(filtab[fn]^, temp[top].int); { tab to position }
         top := top-1; { remove from stack }
         newlin := false { set mid line }

      end else if chkchr = chr(ord(cusing)) then begin { parse using }

         getchr; { skip 'using' }
         expr; { parse using expression }
         if temp[top].typ <> tstr then prterr(estre); { string expected }
         { store format string, copy basic string to common string }
         for i := 1 to maxstr do fs[i] := ' '; { clear target }
         { copy }
         for i := 1 to temp[top].bstr.len do fs[i] := temp[top].bstr.str[i];
         fl := temp[top].bstr.len; { set length }
         fi := 1; { index start of format string }
         top := top-1 { clean stack }

      end else if (chkchr <> chr(ord(ccma))) and
                  (chkchr <> chr(ord(cscn))) then begin { normal parameter }

         expr; { parse expression to print }
         skpspc; { skip to terminator }
          { if a format string exits, use that }
         if fl <> 0 then prtfmt(filtab[fn]^) { print formatted }
         else if temp[top].typ = tstr then {string }
            prtbstr(filtab[fn]^, temp[top].bstr) { print string }
         else if temp[top].typ = tint then begin { integer }

            prtchr(filtab[fn]^, ' '); { space off }
            prtint(filtab[fn]^, temp[top].int, 1); { print integer }
            prtchr(filtab[fn]^, ' ') { space off }

         end else begin { real }

            prtchr(filtab[fn]^, ' '); { space off }
            prtrl(filtab[fn]^, temp[top].rl, 1); { print real }
            prtchr(filtab[fn]^, ' ') { space off }

         end;

         top := top-1; { remove from stack }
         newlin := false { set mid line }

      end;
      skpspc;
      c := chkchr; { save termination character }
      { check fielded processing }
      if c = chr(ord(ccma)) then { move to next 15th tab stop }
         repeat prtchr(filtab[fn]^, ' ') until (filtab[fn]^.cp-1) mod 15 = 0;
      if (c = chr(ord(ccma))) or (c = chr(ord(cscn))) then 
         getchr { skip ',' or ';' }
   
   end;
   if (c <> chr(ord(cscn))) and (c <> chr(ord(ccma))) then begin

      if filtab[fn]^.ty = tyout then writeln { terminate print line }
      else writeln(filtab[fn]^.f); 
      rsttab(filtab[fn]^); { reset tabbing }
      newlin := true { set on new line }

   end

end; { sprint }

{******************************************************************************

Open

Handles the 'open' statement.

******************************************************************************}

procedure sopen;

var io: boolean; { input/output flag }
    ts: filnam; { holder for filename }
    i:  1..maxfln; { index for same }
    fn: integer; { file number }

begin

   if not hasnfio then prterr(enonfio); { no named FIO this version}
   
   getchr; { skip 'open' }
   expr; { parse filename expression }
   if temp[top].typ <> tstr then prterr(estre); { string expected }
   { copy basic string to common string }
   for i := 1 to maxfln do ts[i] := ' '; { clear target }
   { copy }
   for i := 1 to temp[top].bstr.len do ts[i] := temp[top].bstr.str[i];
   top := top-1; { clean stack }
   skpspc; { skip spaces }
   if chkchr <> chr(ord(cfor)) then prterr(eforexp); { 'for' expected }
   getchr; { skip 'for' }
   skpspc; { skip spaces }
   if (chkchr <> chr(ord(cinput))) and (chkchr <> chr(ord(coutput))) then
      prterr(efmdexp); { file mode expected }
   io := chkchr = chr(ord(cinput)); { set mode }
   getchr; { skip file mode }
   skpspc; { skip spaces }
   if chkchr <> chr(ord(cas)) then prterr(easexp); { 'as' expected }
   getchr; { skip 'as' }
   skpspc; { skip spaces }
   if chkchr <> chr(ord(cpnd)) then prterr(epndexp); { '#' expected }
   getchr; { skip '#' }
   expr; { parse file number }
   cvtint; { convert to integer }
   fn := temp[top].int; { get file number }
   top := top-1; { clean stack }
   if (fn < 1) or (fn > maxfil) then prterr(einvfnum); { invalid file number }
   if fn < 3 then prterr(esysfnum); { cannot open system file }
   { if the file slot is empty, get a new file entry for that, otherwise we use
     the active file }
   if filtab[fn] = nil then getfil(filtab[fn]);
   if filtab[fn]^.st <> stclose then begin { file open, close it automatically }

      closefile(filtab[fn]^.f); { close the file }
      filtab[fn]^.st := stclose { set closed }

   end;
   if io then { input mode }
      if not existsfile(ts) then prterr(efilnfd); { file not found }
   if io then begin { input mode }

      openread(filtab[fn]^.f, ts); { open file }
      filtab[fn]^.st := stopenrd { set open for read }

   end else begin { output mode }

      openwrite(filtab[fn]^.f, ts); { open file }
      filtab[fn]^.st := stopenwr { set open for write }

   end

end;

{******************************************************************************

Close

Handles the 'close' statement.

******************************************************************************}

procedure sclose;

var fn: integer; { file number }

begin

   getchr; { skip 'close' }
   skpspc; { skip spaces }
   if chkchr <> chr(ord(cpnd)) then prterr(epndexp); { '#' expected }
   getchr; { skip '#' }
   expr; { parse file number }
   cvtint; { convert to integer }
   fn := temp[top].int; { get file number }
   top := top-1; { clean stack }
   if (fn < 1) or (fn > maxfil) then prterr(einvfnum); { invalid file number }
   if fn < 3 then prterr(esysfnum); { cannot close system file }
   if filtab[fn] = nil then prterr(efilnop); { file already closed }
   if filtab[fn]^.st = stclose then prterr(efilnop); { file already closed }
   closefile(filtab[fn]^.f); { close the file }
   putfil(filtab[fn]) { release file entry }

end;

{******************************************************************************

Goto

Handles the 'goto' statement.

******************************************************************************}

procedure sgoto;

var vi: varinx; { index for variable }

begin

   getchr; { skip 'goto' }
   skpspc; { skip spaces }
   { symbolic labels will look like real variables }
   if chkchr = chr(ord(crlv)) then begin { search for symbolic label }

      getchr; { skip }
      vi := ord(chkchr); { get variable index }
      getchr; { skip }
      if vartbl[vi]^.gto then curprg := vartbl[vi]^.glin { set line }
      else prterr(elabnf) { not found }

   end else { numeric label }
      curprg := schlab(getint); { find the target label }
   goto 2 { go next line }

end;

{******************************************************************************

If

Handles the 'if' statement. Implements "dangling then-else". This rule is used
to extend single line ifs into multiple lines. After a then or else clause, 
if nothing else exists on the line, we then go on a multi-line search for the
next else or endif.

******************************************************************************}

procedure sif;

var c: char;

begin

   getchr; { skip 'if' }
   pshctl; { push a control block }
   ctlstk^.typ := ctif; { set 'if' type }
   ctlstk^.sif := true; { set single line }
   expr;
   c := chkchr; { save tolken }
   if (chkchr <> chr(ord(cthen))) and (chkchr <> chr(ord(cgoto))) then 
      prterr(ethnexp); { 'then' expected }
   getchr; { skip 'then/goto' }
   cvtint; { convert to integer }
   if temp[top].typ <> tint then prterr(eexmi);
   if temp[top].int = 0 then begin { false }

      top := top-1; { purge value }
      skpspc; { skip to end of line }
      if (ktrans[chkchr] in [cpend, clend]) and
         (c = chr(ord(cthen))) then begin

         { perform multiline processing }
         ctlstk^.sif := false; { set as multiline 'if' }
         skpif(true, false); { skip dead section }
         { skip 'else' or 'endif' }
         if chkchr = chr(ord(celse)) then begin

            getchr; { skip 'else' }
            skpspc; { skip spaces }
            if chkchr = chr(ord(cintc)) then begin { it's a default goto }

               curprg := schlab(getint); { find the target label }
               goto 2 { go next line }

            end;
            { if the 'else' dangles, set as multiple line 'if' }
            skpspc; { skip to end }
            if ktrans[chkchr] in [cpend, clend] then ctlstk^.sif := false
            else goto 1 { execute next statement }

         end else if chkchr = chr(ord(cendif)) then begin

            getchr; { skip 'endif' }
            popctl { remove 'if' }

         end else prterr(eeleiexp); { not terminated }

      end else begin { process single line 'if' }

         skpif(true, true); { skip in line }
         if chkchr = chr(ord(celse)) then begin { found the 'else' }

            getchr; { skip 'else' }
            skpspc; { skip spaces }
            if chkchr = chr(ord(cintc)) then begin { it's a default goto }

               curprg := schlab(getint); { find the target label }
               goto 2 { go next line }

            end;
            { if the 'else' dangles, set as multiple line 'if' }
            skpspc; { skip to end }
            if ktrans[chkchr] in [cpend, clend] then ctlstk^.sif := false
            else goto 1 { execute next statement }

         end else if chkchr = chr(ord(cendif)) then begin

            getchr; { skip 'endif' }
            popctl { remove 'if' }
      
         end else begin { not found, skip to next line }

            { go next line }
            curprg := curprg^.next;
            goto 2

         end

      end

   end else begin { true }

      top := top - 1;
      skpspc; { skip spaces }
      if chkchr = chr(ord(cintc)) then begin { it's a default goto }

         curprg := schlab(getint); { find the target label }
         popctl; { remove 'if' }
         goto 2 { go next line }

      end else if c = chr(ord(cgoto)) then 
         prterr(einte); { integer expected }
      { if the 'then' dangles, set as multiple line 'if' }
      if chksend then ctlstk^.sif := false
      else goto 1 { execute next statement on line }

   end

end;

{******************************************************************************

Else

Handles the 'else' statment as actively executed. If an else statement is
encountered while executing, then axiomatically the if statement matching it
must have been true. Therefore, all we must do is skip forward until an end
of line is found (single line if), or 'endif' is found (multiple line 'if').

******************************************************************************}

procedure selse;

begin

   getchr; { skip 'else' }
   fndctl(ctif); { find our matching 'if' }
   if ctlstk = nil then prterr(enoif); { no matching 'if' }
   skpspc; { check dangling }
   if ktrans[chkchr] in [clend, cpend] then begin { multiple line 'if' }

      skpif(false, false); { skip dead section }
      popctl { remove 'if' level }
   
   end else begin { search for single line terminates }

      popctl; { remove 'if' level }
      skpif(false, true); { skip dead section }
      if chksend then begin { end of line }

         { go next line }
         curprg := curprg^.next;
         goto 2

      end

   end;
   if chkchr = chr(ord(cendif)) then getchr { skip 'endif' }

end;

{******************************************************************************

Endif

Handles the 'endif' statement as actively executed. If an 'endif' statement is
encountered while executing, its a no-op, except that we check if a matching
'if' is activated.

******************************************************************************}

procedure sendif;

begin

   getchr; { skip 'else' }
   fndctl(ctif); { find our matching 'if' }
   if ctlstk = nil then prterr(enoif); { no matching 'if' }
   popctl { remove 'if' level }

end;

{******************************************************************************

Rem

Handles the 'rem' statement.

******************************************************************************}

procedure srem;

begin

   curprg := curprg^.next; { go next line }
   goto 2 { exit line executive }

end;

{******************************************************************************

Run

Handles the 'run' statement. Used without argument, the present program is run.
Used with argument, the program specified is loaded and run.

******************************************************************************}

procedure srun;

var pp: bstptr; { program line pointer }
    ts: filnam; { holder for filename }
    i:  1..maxfln; { index for same }

begin

   getchr; { skip 'run' }
   if not chksend then begin { there is a "load" file specified }

      if not hasnfio then prterr(enonfio); { no named FIO this version}

      expr; { parse file expression }
      if temp[top].typ <> tstr then prterr(estre); { must be string }
      addext(temp[top].bstr, 'bas'); { add '.bas' extention }
      { copy basic string to common string }
      for i := 1 to maxfln do ts[i] := ' '; { clear target }
      { copy }
      for i := 1 to temp[top].bstr.len do ts[i] := temp[top].bstr.str[i];
      if not existsfile(ts) then prterr(efnfnd); { not found }
      openread(source, ts); { open the file }
      fsrcop := true; { set source file is open }
      top := top-1; { clean stack }
      clear; { clear present program }
      { get user lines. Note that we load everything, even blank lines
        and lines without numbers. Such lines may be executed, but
        not editted }
      pp := nil; { set no last line }
      while not eof(source) do begin { read file lines }

         getstr(linbuf); { get a load string }
         inpbstr(source, linbuf^); { input line from file }
         keycom(linbuf); { compress }
         if pp = nil then prglst := linbuf { set as first line }
         else pp^.next := linbuf; { set mid line }
         pp := linbuf; { index last }
         linbuf := nil { clear line buffer }

      end;
      closefile(source); { close input file }
      fsrcop := false; { set source file is closed }

   end;
   clrvars; { clear variables and data position }
   nxtdat; { find first data statement }
   reglab; { find all symbolic labels }
   curprg := prglst; { set execute first program line }
   rndseq := 1; { start random number generator }
   rsttab(filtab[2]^); { reset tabbing }
   goto 2 

end;

{******************************************************************************

List/save

Handles the 'list' and 'save' statements.

******************************************************************************}

procedure slstsav(cmd: keycod); { command type }

var sl, el: bstptr; { start and end line for list }
    source: text;   { source file }
    ts:     filnam; { holder for filename }
    i:      1..maxfln; { index for same }

begin

   getchr; { skip 'list'/'save' }
   if cmd = csave then begin { open save file }

      if not hasnfio then prterr(enonfio); { no named FIO this version}
      expr; { parse file expression }
      if temp[top].typ <> tstr then prterr(estre); { must be string }
      addext(temp[top].bstr, 'bas'); { add '.bas' extention }
      { copy basic string to common string }
      for i := 1 to maxstr do ts[i] := ' '; { clear target }
      { copy }
      for i := 1 to temp[top].bstr.len do ts[i] := temp[top].bstr.str[i];
      openwrite(source, ts); { open the file }
      top := top-1 { clean stack }

   end;
   sl := prglst; { set default list swath (whole program) }
   el := nil;
   if not chksend then begin { list swath is specified }

      sl := schlab(getint); { get starting line }
      skpspc; { skip spaces }
      { check if end line is specified }
      if chkchr = chr(ord(ccma)) then begin { get line end }

         getchr; { skip ',' }
         el := schlab(getint) { get line end }

      end

   end;
   while sl <> nil do begin { print specified lines }

      if cmd = clist then { list command specified }
         prtlin(output, sl) { print to console }
      else { save command specified }
         prtlin(source, sl); { print to source file }
      if sl = el then sl := nil { end of list, stop }
      else sl := sl^.next { next line }

   end;
   if cmd = csave then closefile(source)

end;

{******************************************************************************

Load

Handles the 'load' statement.

******************************************************************************}

procedure sload;

var pp: bstptr; { program line pointer }
    ts: filnam; { holder for filename }
    i:  1..maxfln; { index for same }

begin

   if not hasnfio then prterr(enonfio); { no named FIO this version}
   
   getchr; { skip 'load' }
   expr; { parse file expression }
   if temp[top].typ <> tstr then prterr(estre); { must be string }
   addext(temp[top].bstr, 'bas'); { add '.bas' extention }
   { copy basic string to common string }
   for i := 1 to maxfln do ts[i] := ' '; { clear target }
   { copy }
   for i := 1 to temp[top].bstr.len do ts[i] := temp[top].bstr.str[i];
   if not existsfile(ts) then prterr(efnfnd); { not found }
   openread(source, ts); { open the file }
   fsrcop := true; { set source file is open }
   top := top-1; { clean stack }
   clear; { clear present program }
   { get user lines. Note that we load everything, even blank lines
     and lines without numbers. Such lines may be executed, but
     not editted }
   pp := nil; { set no last line }
   while not eof(source) do begin { read file lines }

      getstr(linbuf); { get a load string }
      inpbstr(source, linbuf^); { input line from file }
      keycom(linbuf); { compress }
      if pp = nil then prglst := linbuf { set as first line }
      else pp^.next := linbuf; { set mid line }
      pp := linbuf; { index last }
      linbuf := nil { clear line buffer }

   end;
   closefile(source); { close input file }
   fsrcop := false; { set source file is closed }
   goto 88 { return to command }
   
end;

{******************************************************************************

New

Handles the 'new' statement.

******************************************************************************}

procedure snew;

begin

   getchr; { skip 'new' }
   clear; 
   goto 88 { return to command }

end;

{******************************************************************************

Data

Handles the 'data' statement.

******************************************************************************}

procedure sdata;

var c: char;

begin

   getchr; { skip 'data' }
   { when executed as a statement, we simply skip over any constants
     defined }
   repeat

      skpspc; { skip spaces }
      if not (ktrans[chkchr] in [cintc, crlc, cstrc, csub, cadd]) then 
         prterr(ecstexp); { constant expected }
      if ktrans[chkchr] in [csub, cadd] then begin { sign }

         skptlk; { skip sign }
         if not (ktrans[chkchr] in [cintc, crlc]) then 
            prterr(encstexp) { numeric constant expected }
         
      end;
      skptlk; { skip over constant }
      skpspc; { skip spaces }
      c := chkchr; { save next tolken }
      if c = chr(ord(ccma)) then getchr; { it's ':', skip }
      skpspc { skip spaces }

   until c <> chr(ord(ccma)); { until no more data }

end;

{******************************************************************************

Read

Handles the 'read' statement.

******************************************************************************}

procedure sread;

var c:    char;
    vp:   vvcptr;
    x, y: integer;
    r:    real; 
    sp:   bstptr; { string pointer }
    sgn:  integer; { sign }

begin

   getchr; { skip 'read' }
   repeat { process variables }

      skpspc; { skip spaces }
      if not (ktrans[chkchr] in [cintv, cstrv, crlv]) then
         prterr(evare); { variable expected }
      if datac = nil then prterr(eoutdat); { out of data }
      { check ANY data exists at data statement }
      if not (ktrans[datac^.str[datal]] in 
              [cintc, cstrc, crlc, csub, cadd]) then begin

         { this may not be pretty, but we switch executing lines to the
           offending data statement, so that we can show the bad data
           statement }
         curprg := datac; { set new line }
         linec := datal; { set new character }
         prterr(ecstexp) { constant expected }

      end;
      { if the data is positive or negative, we need to accept the sign
        and verify again }
      sgn := 1; { set positive }
      if ktrans[datac^.str[datal]] in [csub, cadd] then begin

         { check and change sign }
         if ktrans[datac^.str[datal]] = csub then sgn := -1;
         datal := datal+1; { advance over sign }
         { recheck valid constant }
         if not (ktrans[datac^.str[datal]] in [cintc, crlc]) then begin

            { this may not be pretty, but we switch executing lines to the
              offending data statement, so that we can show the bad data
              statement }
            curprg := datac; { set new line }
            linec := datal; { set new character }
            prterr(encstexp) { constant expected }

         end

      end;
      if chkchr = chr(ord(cintv)) then begin { read integer variable }

         getchr; { skip }
         c := chkchr; { get variable }
         getchr; { skip }
         skpspc; { skip spaces }
         vp := nil; { set not vector }
         if chkchr = chr(ord(clpar)) then { array reference }
            parvec(vartbl[ord(c)]^.intv, vp, x, vvint);
         { check next data item is integer }
         if datac^.str[datal] = chr(ord(cintc)) then begin { read integer }

            datal := datal+1; { index integer }
            dcdint(datac^, datal, y) { get integer }

         end else if datac^.str[datal] = chr(ord(crlc)) then begin

            { read real }
            datal := datal+1; { index real }
            dcdrl(datac^, datal, r); { get real }
            y := round(r) { convert to integer }

         end else prterr(erdtyp); { wrong type }
         if vp <> nil then { array assign }
            vp^.int[x] := y*sgn { place integer }
         else { ordinary reference }
            vartbl[ord(c)]^.int := y*sgn { place integer }
   
      end else if chkchr = chr(ord(crlv)) then begin { read real variable }

         getchr; { skip }
         c := chkchr; { get variable }
         getchr; { skip }
         skpspc; { skip spaces }
         vp := nil; { set not vector }
         if chkchr = chr(ord(clpar)) then { array reference }
            parvec(vartbl[ord(c)]^.rlv, vp, x, vvrl);
         { check next data item is integer }
         if datac^.str[datal] = chr(ord(cintc)) then begin

            datal := datal+1; { index integer }
            dcdint(datac^, datal, y); { get integer }
            r := y { convert to real }

         end else if datac^.str[datal] = chr(ord(crlc)) then begin

            datal := datal+1; { index real }
            dcdrl(datac^, datal, r) { get real }

         end else prterr(erdtyp); { wrong type }
         if vp <> nil then { array assign }
            vp^.rl[x] := r*sgn { place real }
         else { ordinary reference }
            vartbl[ord(c)]^.rl := r*sgn { place real }
   
      end else begin { read string variable }

         getchr; { skip }
         c := chkchr; { get variable }
         getchr; { skip }
         skpspc; { skip spaces }
         if chkchr = chr(ord(clpar)) then begin { array reference }

            parvec(vartbl[ord(c)]^.strv, vp, x, vvstr);
            makstr(vp^.str[x]); { make sure string exists }
            sp := vp^.str[x] { index string }

         end else begin { normal reference }

            makstr(vartbl[ord(c)]^.str); { make sure string exists }
            sp := vartbl[ord(c)]^.str { index string }

         end;
         { check next data item is string }
         if datac^.str[datal] <> chr(ord(cstrc)) then prterr(erdtyp);
         datal := datal+1; { index length }
         x := ord(datac^.str[datal]); { get length }
         sp^.len := x; { place string length }
         y := 1; { set 1st string position }
         datal := datal+1; { index string }
         while x > 0 do begin { copy string characters }
      
            { copy character }
            sp^.str[y] := datac^.str[datal];
            datal := datal+1; { next character }
            y := y+1;
            x := x-1 { count }

         end

      end;
      spcdat; { skip any spaces }
      { see if there is more data. if not, we advance to the next data
        statement, either for the next time around the loop, or the next
        read statement }
      if datac^.str[datal] = chr(ord(ccma)) then begin

         datal := datal+1; { ',', just skip for next constant }
         spcdat { skip any spaces }

      end else nxtdat; { find next data statement }
      skpspc; { skip spaces }
      c := chkchr; { save next }
      if c = chr(ord(ccma)) then getchr { skip ',' }

   { until no more variables to read }
   until c <> chr(ord(ccma))

end;

{******************************************************************************

Restore

Handles the 'restore' statement.

******************************************************************************}

procedure srestore;

begin

   getchr; { skip 'restore' }
   datac := prglst; { reset data index to start of program }
   datal := 1;
   skpspc; { skip spaces }
   if chkchr = chr(ord(crlv)) then begin { search for symbolic label }

      getchr; { skip }
      vi := ord(chkchr); { get variable index }
      getchr; { skip }
      if vartbl[vi]^.gto then datac := vartbl[vi]^.glin { set line }
      else prterr(elabnf) { not found }
      
   end else if chkchr = chr(ord(cintc)) then { line is specified }
      datac := schlab(getint); { find corresponding line }
   nxtdat { find first data statement }

end;

{******************************************************************************

Let/procedure

Handles the 'let' and procedure statements.

******************************************************************************}

procedure slet;

var c:         char;
    vp:        vvcptr;
    vap, vap1: varptr;
    x:         integer;

begin

   if chkchr = chr(ord(clet)) then getchr; { skip 'let' }
   skpspc; { skip spaces }
   { check next is variable }
   if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then prterr(evare);
   c := chkchr; { save head type }
   getchr; { skip }
   x := ord(chkchr); { get variable }
   getchr; { skip }
   { check is a procedure }
   if vartbl[x]^.prc then
      parfnc(vartbl[x], crlv) { execute procedure }
   else begin { variable assign }

      if vartbl[x]^.sys then prterr(esysvar); { system variable }
      vp := nil; { set ordinary access }
      { record handles (r.) are typeless, and so become default reals by
        appearance. so we only process them here. any number of record
        handles can appear, but arrays must follow records. this limitation
        will disappear in time }
      vap := vartbl[x]; { index target variable }
      while chkchr = chr(ord(cperiod)) do begin { record fields }

         getchr; { skip '.' }
         if c <> chr(ord(crlv)) then prterr(etypfld);
         if not (ktrans[chkchr] in [cintv, crlv, cstrv]) then prterr(einvfld);
         c := chkchr; { save head type again }
         getchr; { skip start tolken }
         x := ord(chkchr); { get field index }
         getchr; { skip }
         fndfld(vap^.rec, x, vap1); { lookup or make field }
         vap := vap1 { copy back }
         
      end;
      if chkchr = chr(ord(clpar)) then { array reference }
         { parse array reference by type }
         if c = chr(ord(cstrv)) then { string }
            parvec(vap^.strv, vp, x, vvstr)
         else if c = chr(ord(cintv)) then { integer } 
            parvec(vap^.intv, vp, x, vvint)
         else { real }
            parvec(vap^.rlv, vp, x, vvrl);
      skpspc; { skip spaces }
      { check next is '=' }
      if chkchr <> chr(ord(cequ)) then prterr(eeque);
      getchr; { skip '=' }
      expr; { get source expression }
      if c = chr(ord(cstrv)) then begin
      
         if temp[top].typ <> tstr then prterr(estyp);
         if vp <> nil then begin { array assign }

            makstr(vp^.str[x]); { make sure string exists }
            vp^.str[x]^ := temp[top].bstr; { place value }
            
         end else begin { ordinary reference }

            makstr(vartbl[x]^.str); { make sure string exists }
            vap^.str^ := temp[top].bstr; { place value }

         end;
         top := top-1
      
      end else if c = chr(ord(cintv)) then begin
      
         cvtint; { convert top to integer }
         if temp[top].typ <> tint then prterr(estyp);
         if vp <> nil then { array assign }
            vp^.int[x] := temp[top].int { place value }
         else { ordinary reference }
            vap^.int := temp[top].int;
         top := top-1
      
      end else begin
      
         cvtrl; { convert to real }
         if temp[top].typ <> trl then prterr(estyp);
         if vp <> nil then { array assign }
            vp^.rl[x] := temp[top].rl { place value }
         else { ordinary reference }
            vap^.rl := temp[top].rl;
         top := top-1
      
      end

   end

end;

{******************************************************************************

Gosub

Handles the 'gosub' statement.

******************************************************************************}

procedure sgosub;

var pp: bstptr; { program line pointer }
    vi: varinx; { index for variable }

begin

   getchr; { skip 'gosub' }
   skpspc; { skip spaces }
   { symbolic labels will look like real variables }
   if chkchr = chr(ord(crlv)) then begin { search for symbolic label }

      getchr; { skip }
      vi := ord(chkchr); { get variable index }
      getchr; { skip }
      if vartbl[vi]^.gto then pp := vartbl[vi]^.glin { set line }
      else prterr(elabnf) { not found }

   end else { numeric label }
      pp := schlab(getint); { find the target label }
   pshctl; { push a control block }
   ctlstk^.typ := ctgosub; { set 'gosub' type }
   ctlstk^.line := curprg; { save present position }
   ctlstk^.chrp := linec;
   curprg := pp; { set new line }
   goto 2 { go next line }

end;

{******************************************************************************

Return

Handles the 'return' statement.

******************************************************************************}

procedure sreturn;

begin

   fndctl(ctgosub); { find a 'gosub' block }
   if ctlstk = nil then prterr(enoret); { nothing to return to }
   curprg := ctlstk^.line; { restore execution position }
   linec := ctlstk^.chrp;
   popctl { remove control block }

end;

{******************************************************************************

For

Handles the 'for' statement.

******************************************************************************}

procedure sfor;

var c: char;
    f: boolean;

begin

   getchr; { skip 'for' }
   skpspc;
   { check next is variable }
   if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then prterr(evare);
   pshctl; { push a control block }
   ctlstk^.typ := ctfor; { set 'for' type }
   ctlstk^.sov := false; { set not sequence of values type }
   { set variable type }
   case ktrans[chkchr] of { variable type }

      cintv: ctlstk^.vtyp := vtint; { integer }
      crlv:  ctlstk^.vtyp := vtrl; { real }
      cstrv: ctlstk^.vtyp := vtstr { string }

   end;
   getchr; { skip }
   c := chkchr; { get index }
   getchr; { skip }
   if vartbl[ord(c)]^.sys then prterr(esysvar); { system variable }
   ctlstk^.vinx := c; { get variable index }
   skpspc; { skip spaces }
   { check next is '=' }
   if chkchr <> chr(ord(cequ)) then prterr(eeque);
   getchr; { skip '=' }
   expr; { get starting expression }
   { place starting value }
   case ctlstk^.vtyp of { variable type }
   
      vtint: begin { integer }

         cvtint; { convert to integer }
         if temp[top].typ <> tint then prterr(estyp);
         vartbl[ord(c)]^.int := temp[top].int;
         top := top-1

      end;
      vtrl: begin { real }

         cvtrl; { convert to real }
         if temp[top].typ <> trl then prterr(estyp);
         vartbl[ord(c)]^.rl := temp[top].rl;
         top := top-1

      end;
      vtstr: begin { string }
   
         if temp[top].typ <> tstr then prterr(estyp);
         makstr(vartbl[ord(c)]^.str); { make sure string exists }
         vartbl[ord(c)]^.str^ := temp[top].bstr;
         top := top-1

      end

   end;
   skpspc; { skip spaces }
   if chkchr = chr(ord(cto)) then begin { process 'to [step]' sequence }

      getchr; { skip 'to' }
      if ctlstk^.vtyp = vtstr then prterr(etostr);
      expr; { parse end expression }
      if ctlstk^.vtyp = vtint then begin { place integer end }

         cvtint; { convert to integer }
         if temp[top].typ <> tint then prterr(estyp);
         ctlstk^.endi := temp[top].int;
         top := top-1;
         ctlstk^.stepi := 1 { set default step }

      end else begin { place real end }

         cvtrl; { convert to real }
         if temp[top].typ <> trl then prterr(estyp);
         ctlstk^.endr := temp[top].rl;
         top := top-1;
         ctlstk^.stepr := 1.0 { set default step }

      end;
      skpspc; { skip spaces }
      if chkchr = chr(ord(cstep)) then begin { a step is specified }

         getchr; { skip 'step' }
         expr; { get step expression }
         if ctlstk^.vtyp = vtint then begin { place integer step }

            cvtint; { convert to integer }
            if temp[top].typ <> tint then prterr(estyp);
            ctlstk^.stepi := temp[top].int;
            top := top-1;

         end else begin { place real step }

            cvtrl; { convert to real }
            if temp[top].typ <> trl then prterr(estyp);
            ctlstk^.stepr := temp[top].rl;
            top := top-1;

         end

      end;
      { check loop is already complete }
      if ctlstk^.vtyp = vtint then begin { integer }

         { find if end condition is met }
         if ((ctlstk^.stepi >= 0) and 
             (vartbl[ord(c)]^.int > ctlstk^.endi)) or
            ((ctlstk^.stepi < 0) and 
             (vartbl[ord(c)]^.int < ctlstk^.endi)) then repeat 

            { end condition true, skip to 'next' }
            while (chkchr <> chr(ord(cnext))) and
                  (chkchr <> chr(ord(cpend))) do 
               skptlkl; { skip to 'next' }
            if chkchr = chr(ord(cpend)) then
               prterr(emsgnxt); { missing 'next' }
            getchr; { skip 'next' }
            skpspc; { skip spaces }
            f := false; { set no match }
            if chkchr = chr(ord(cintv)) then begin { integer variable }

               getchr; { skip }
               if chkchr = c then f := true; { found }
               getchr { skip }

            end else skptlk { skip variable }
 
         until f { matching 'next' is found }

      end else begin { real }

         { find if end condition is met }
         if ((ctlstk^.stepr >= 0) and (vartbl[ord(c)]^.rl > ctlstk^.endr)) or
            ((ctlstk^.stepr < 0) and (vartbl[ord(c)]^.rl < ctlstk^.endr)) then
            repeat { end condition true, skip to 'next' }

            while (chkchr <> chr(ord(cnext))) and
                  (chkchr <> chr(ord(cpend))) do 
               skptlkl; { skip to 'next' }
            if chkchr = chr(ord(cpend)) then
               prterr(emsgnxt); { missing 'next' }
            getchr; { skip 'next' }
            skpspc; { skip spaces }
            f := false; { set no match }
            if chkchr = chr(ord(crlv)) then begin { integer variable }

               getchr; { skip }
               if chkchr = c then f := true; { found }
               getchr { skip }

            end else skptlk { skip variable }
 
         until f { matching 'next' is found }

      end

   end else begin { process sequence of values style }
   
      { sequence must have ',' }
      if chkchr <> chr(ord(ccma)) then prterr(etoexp); { 'to' expected }
      ctlstk^.sov := true { set sequence of values type }

   end;
   ctlstk^.line := curprg; { save present position }
   ctlstk^.chrp := linec;
   if ctlstk^.sov then { sequence of values type }
      { skip forward to end of statement }
      while not chksend do skptlk

end;

{******************************************************************************

Next

Handles the 'next' statement.

******************************************************************************}

procedure snext;

var c:  char;
    cp: ctlptr;
    pp: bstptr; { program line pointer }
    y:  integer;
    vt: vartyp;

begin

   getchr; { skip 'next' }
   skpspc; { skip spaces }
   repeat { 'next' variables }

      if chksend then begin { "anonymous" next }

         fndctl(ctfor); { find any 'for' block }
         if ctlstk = nil then prterr(enoafor); { no matching 'for' }
         vt := ctlstk^.vtyp; { place variable type }
         c := ctlstk^.vinx { place variable number }

      end else begin { something exists }

         { check next is variable }
         if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then prterr(evare);
         case ktrans[chkchr] of { variable }

            cintv: vt := vtint; { set integer type }
            crlv:  vt := vtrl; { set real type }
            cstrv: vt := vtstr { set string type }

         end;
         getchr; { skip }
         c := chkchr; { get variable index }
         getchr; { skip }
         repeat { search 'for' control blocks }

            fndctl(ctfor); { find a 'for' block }
            cp := ctlstk; { index that }
            if cp <> nil then { check proper type }
               if (cp^.vtyp = vt) and (cp^.vinx = c) then 
                  cp := nil; { terminate search }
            if cp <> nil then popctl { remove that block }

         until cp = nil; { until terminated }
         if ctlstk = nil then prterr(enofor); { no matching 'for' }

      end;
      case vt of { process step by type }

         vtint: begin { integer }

            if ctlstk^.sov then begin { process sequence of values type }

               pp := curprg; { save line position }
               y := linec;
               curprg := ctlstk^.line; { restore old line position }
               linec := ctlstk^.chrp;
               skpspc; { skip spaces }
               if chkchr = chr(ord(ccma)) then begin { more step values }
               
                  getchr; { skip ',' }
                  expr; { get string expression }             
                  cvtint; { convert to integer }  
                  if temp[top].typ <> tint then prterr(estyp);
                  vartbl[ord(c)]^.int := temp[top].int; { place value }
                  top := top-1;
                  ctlstk^.line := curprg; { place new line position }
                  ctlstk^.chrp := linec;
                  { skip forward to end of statement }
                  while not chksend do skptlk
               
               end else begin { for loop terminates }
               
                  curprg := pp; { restore after 'next' position }
                  linec := y
               
               end

            end else begin { process to [step] sequence }

               { find next value }
               vartbl[ord(c)]^.int := vartbl[ord(c)]^.int+ctlstk^.stepi;
               { find if end condition is met }
               if ((ctlstk^.stepi >= 0) and 
                   (vartbl[ord(c)]^.int <= ctlstk^.endi)) or
                  ((ctlstk^.stepi < 0) and 
                   (vartbl[ord(c)]^.int >= ctlstk^.endi)) then begin
      
                  { we loop again }
                  curprg := ctlstk^.line; { reset position }
                  linec := ctlstk^.chrp
      
               end

            end

         end;

         vtrl: begin { real }

            if ctlstk^.sov then begin { process sequence of values type }

               pp := curprg; { save line position }
               y := linec;
               curprg := ctlstk^.line; { restore old line position }
               linec := ctlstk^.chrp;
               skpspc; { skip spaces }
               if chkchr = chr(ord(ccma)) then begin { more step values }
               
                  getchr; { skip ',' }
                  expr; { get string expression }             
                  cvtrl; { convert to real }  
                  if temp[top].typ <> trl then prterr(estyp);
                  vartbl[ord(c)]^.rl := temp[top].rl; { place value }
                  top := top-1;
                  ctlstk^.line := curprg; { place new line position }
                  ctlstk^.chrp := linec;
                  { skip forward to end of statement }
                  while not chksend do skptlk
               
               end else begin { for loop terminates }
               
                  curprg := pp; { restore after 'next' position }
                  linec := y
               
               end

            end else begin { process to [step] sequence }

               { find next value }
               vartbl[ord(c)]^.rl := vartbl[ord(c)]^.rl+ctlstk^.stepr;
               { find if end condition is met }
               if ((ctlstk^.stepr >= 0.0) and 
                   (vartbl[ord(c)]^.rl <= ctlstk^.endr)) or
                  ((ctlstk^.stepr < 0.0) and 
                   (vartbl[ord(c)]^.rl >= ctlstk^.endr)) then begin

                  { we loop again }
                  curprg := ctlstk^.line; { reset position }
                  linec := ctlstk^.chrp

               end

            end

         end;

         vtstr: begin { string }

            { note that strings are allways sequence of values }
            pp := curprg; { save line position }
            y := linec;
            curprg := ctlstk^.line; { restore old line position }
            linec := ctlstk^.chrp;
            skpspc; { skip spaces }
            if chkchr = chr(ord(ccma)) then begin { more step values }

               getchr; { skip ',' }
               expr; { get string expression }               
               if temp[top].typ <> tstr then prterr(estyp);
               makstr(vartbl[ord(c)]^.str); { make sure string exists }
               vartbl[ord(c)]^.str^ := temp[top].bstr; { place value }
               top := top-1;
               ctlstk^.line := curprg; { place new line position }
               ctlstk^.chrp := linec;
               { skip forward to end of statement }
               while not chksend do skptlk

            end else begin { for loop terminates }

               curprg := pp; { restore after 'next' position }
               linec := y

            end

         end

      end;
      c := chkchr; { check next character }
      if c = chr(ord(ccma)) then getchr; { skip ',' }
      skpspc { skip spaces }

   until c <> chr(ord(ccma)) { until not ',' }

end;

{******************************************************************************

On

Handles the 'on..goto' and 'on..gosub' statements.

******************************************************************************}

procedure son;

var x:     integer;
    c, c1: char;
    pp:    bstptr; { program line pointer }
    vi:    varinx; { index for variable }

begin

   getchr; { skip 'on' }
   expr; { get selector expression }
   cvtint; { convert to integer }
   if temp[top].typ <> tint then prterr(eexmi); { check integer }
   x := temp[top].int; { get the selector }
   top := top-1; { purge value }
   skpspc; { skip spaces }
   if (chkchr <> chr(ord(cgoto))) and (chkchr <> chr(ord(cgosub))) then
      prterr(egtgsexp); { 'goto'/'gosub' expected }
   c1 := chkchr; { save statement type for later }
   getchr; { skip 'goto'/'gosub' }
   repeat

      if chkchr = chr(ord(crlv)) then begin { search for symbolic label }
      
         getchr; { skip }
         vi := ord(chkchr); { get variable index }
         getchr; { skip }
         if vartbl[vi]^.gto then pp := vartbl[vi]^.glin { set line }
         else prterr(elabnf) { not found }
        
      end else { numeric label }
         pp := schlab(getint); { find the target label }
      x := x-1; { count labels }
      skpspc; { skip spaces }
      c := chkchr; { save next }
      if c = chr(ord(ccma)) then getchr; { skip ',' }
      { check proper ending }
      if (c <> chr(ord(ccma))) and not chksend then prterr(ecmaexp)

   until (x = 0) or (c <> chr(ord(ccma))); { label found or no next }
   while not chksend do skptlk; { skip to end of statement }
   if x = 0 then begin { found the label, goto next line }

      if c1 = chr(ord(cgosub)) then begin { 'gosub' }

         pshctl; { push a control block }
         ctlstk^.typ := ctgosub; { set 'gosub' type }
         ctlstk^.line := curprg; { save present position }
         ctlstk^.chrp := linec;

      end;
      curprg := pp; { set new line }
      goto 2 { go next line }

   end
      
end;

{******************************************************************************

While

Handles the 'while' statement.

******************************************************************************}

procedure swhile;

begin

   getchr; { skip 'while' }
   pshctl; { push a control block }
   ctlstk^.typ := ctwhile; { set 'while' type }
   ctlstk^.line := curprg; { save present position }
   ctlstk^.chrp := linec;
   expr; { parse expression }
   cvtint; { convert to integer }
   if temp[top].typ <> tint then prterr(eexmi); { check integer }
   if temp[top].int = 0 then begin { false }

      top := top-1; { purge value }
      { skip multi-line to next 'wend' }
      while (chkchr <> chr(ord(cwend))) and 
            (chkchr <> chr(ord(cpend))) do skptlkl;
      { check found 'wend' }
      if chkchr <> chr(ord(cwend)) then prterr(eedwhexp);
      getchr; { skip 'wend' }
      popctl { remove control block }
      
   end else begin { true }

      top := top-1; { purge value }
      skpspc; { check for optional ':' }
      if chkchr = chr(ord(ccln)) then getchr; { skip ':' }
      goto 1 { re-enter same line }

   end

end;

{******************************************************************************

Wend

Handles the 'wend' statement.

******************************************************************************}

procedure swend;

var y:  integer;
    pp: bstptr; { program line pointer }

begin

   getchr; { skip 'wend' }
   fndctl(ctwhile); { find a 'while' block }
   if ctlstk = nil then prterr(enowhil); { no matching 'while' }
   pp := curprg; { save execution position }
   y := linec;
   curprg := ctlstk^.line; { restore old execution position }
   linec := ctlstk^.chrp;
   expr; { parse expression }
   cvtint; { convert to integer }
   if temp[top].typ <> tint then prterr(eexmi); { check integer }
   if temp[top].int = 0 then begin { false }

      top := top-1; { purge value }
      curprg := pp; { restore new position }
      linec := y;
      popctl { remove control block }

   end else begin { true }

      top := top-1; { purge value }
      skpspc; { check for optional ':' }
      if chkchr = chr(ord(ccln)) then getchr; { skip ':' }
      goto 1 { re-enter same line }

   end

end;

{******************************************************************************

Repeat

Handles the 'repeat' statement.

******************************************************************************}

procedure srepeat;

begin

   getchr; { skip 'repeat' }
   pshctl; { push a control block }
   ctlstk^.typ := ctrepeat; { set 'repeat' type }
   ctlstk^.line := curprg; { save present position }
   ctlstk^.chrp := linec;
   skpspc; { check for optional ':' }
   if chkchr = chr(ord(ccln)) then getchr; { skip ':' }
   goto 1 { re-enter same line }

end;

{******************************************************************************

Until

Handles the 'until' statement.

******************************************************************************}

procedure suntil;

begin

   getchr; { skip 'until' }
   fndctl(ctrepeat); { find a 'repeat' block }
   if ctlstk = nil then prterr(enorpt); { no matching 'repeat' }
   expr; { parse ending expression }
   cvtint; { convert to integer }
   if temp[top].typ <> tint then prterr(eexmi); { check integer }
   if temp[top].int = 0 then begin { false }

      top := top-1; { purge value }
      curprg := ctlstk^.line; { restore old position }
      linec := ctlstk^.chrp;
      skpspc; { check for optional ':' }
      if chkchr = chr(ord(ccln)) then getchr; { skip ':' }
      popctl; { remove control block }
      goto 1 { re-enter same line }

   end else { true }
      top := top-1 { purge value }

end;

{******************************************************************************

Select

Handles the 'select' statement.

******************************************************************************}

procedure sselect;

var m: boolean;

begin

   getchr; { skip 'select' }
   pshctl; { put a control block }
   ctlstk^.typ := ctselect; { set 'select' type }
   expr; { parse expression }
   m := false; { set no matching case found }
   repeat { match cases }

      { find next interesting object }
      while not (ktrans[chkchr] in [ccase, cother, cendsel, cpend]) do skptlkl;
      if chkchr = chr(ord(ccase)) then begin { case }

         getchr; { skip 'case' }
         expr; { parse case expression }
         if temp[top].typ = tstr then begin { string }

            if temp[top-1].typ <> tstr then prterr(ecasmat); { not same type }
            if strequ(temp[top-1].bstr, temp[top].bstr) then
               m := true { set result }

         end else { integer/real }
            if chkequ then m := true;
         top := top-1 { remove operand }

      end else if chkchr = chr(ord(cother)) then begin { other }

         getchr; { skip 'other' }
         m := true { set match found }

      end

   until (chkchr = chr(ord(cendsel))) or m; { until 'endsel' or match found }
   top := top-1; { remove selector expression }
   { if we terminated on 'endsel', then skip that, otherwise leave endif as
     and error }
   if (chkchr = chr(ord(cendsel))) and not m then begin

      getchr; { skip 'endsel' }
      popctl { remove control level }

   end

end;

{******************************************************************************

Case/other

Handles the 'case' statement. If a case is met while executing, then it simply
terminates the select statement, since if we are executing, a case must already
be active. The same rule applies to 'other'.

******************************************************************************}

procedure scase;

begin

   getchr; { skip 'case' }
   fndctl(ctselect); { find a 'select' block }
   if ctlstk = nil then prterr(enosel); { no matching 'select' }
   popctl; { remove control block }
   { skip to end }
   while not (ktrans[chkchr] in [cendsel, cpend]) do skptlkl;
   if chkchr = chr(ord(cendsel)) then getchr { skip endsel }

end;

{******************************************************************************

Endsel

Handles the 'endsel' statement. If an endsel is encountered during execution,
then it is hit while executing the "winning" case. It is simply skipped.

******************************************************************************}

procedure sendsel;

begin

   getchr; { skip 'endsel' }
   fndctl(ctselect); { find a 'select' block }
   if ctlstk = nil then prterr(enosel); { no matching 'select' }
   popctl { remove control block }

end;

{******************************************************************************

Dim

Handles the 'dim' statement.

******************************************************************************}

procedure sdim;

var c:       char;
    x, y:    integer;
    vp, vp1: varptr; { variable pointer }

begin

   getchr; { skip 'dim' }
   repeat { process dim variables }

      skpspc; { skip spaces }
      { check next is variable }
      if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then prterr(evare);
      c := chkchr; { save head type }
      getchr; { skip }
      x := ord(chkchr); { get variable }
      getchr; { skip }
      { check if we are in a function/procedure }
      if fnsstk <> nil then begin { save off old level variable }

         vp := vartbl[x]; { index old variable }
         getvarp(vartbl[x]); { create a new one }
         vartbl[x]^.nam := vp^.nam; { place name }
         vartbl[x]^.inx := vp^.inx; { place name }
         vp^.next := fnsstk^.vs; { push old onto save list }
         fnsstk^.vs := vp

      end;
      vp := vartbl[x]; { index target variable }
      while chkchr = chr(ord(cperiod)) do begin { record fields }

         getchr; { skip '.' }
         if c <> chr(ord(crlv)) then prterr(etypfld);
         if not (ktrans[chkchr] in [cintv, crlv, cstrv]) then prterr(einvfld);
         c := chkchr; { save head type again }
         getchr; { skip start tolken }
         x := ord(chkchr); { get field index }
         getchr; { skip }
         fndfld(vp^.rec, x, vp1); { lookup or make field }
         vp := vp1 { copy back }
         
      end;
      { for now, since our vectors and strings are fixed, we will just count
        the indexes and validate they are within the fixed bounds. Later, we
        would actually size the variables here }
      if chkchr = chr(ord(clpar)) then begin { parameters exist }

         getchr; { skip '(' }
         expr; { parse index }
         cvtint; { convert to integer }
         if temp[top].typ <> tint then prterr(einte); { integer expected }
         if temp[top].int > maxvec then prterr(edimtl); { demension to large }
         top := top-1; { remove from stack }
         y := 1; { set 1st index }
         skpspc; { skip spaces }
         while chkchr = chr(ord(ccma)) do begin { parse indecies }
      
            getchr; { skip ',' }
            expr; { parse index }
            cvtint; { convert to integer }
            if temp[top].typ <> tint then prterr(einte); { integer expected }
            if temp[top].int > maxvec then prterr(edimtl); { demension too large }
            top := top-1; { remove from stack }
            y := y+1; { count indicies }
            skpspc; { skip spaces }
            
         end;
         if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
         getchr; { skip ')' }
         { check not already allocated, and create new entry }
         if c = chr(ord(cintv)) then begin { integer }
      
            { check already allocated }
            if vp^.intv <> nil then prterr(earddim);
            getvec(vp^.intv); { get the starting vector }
            vp^.intv^.inx := y; { place index count }
            { place type of first vector }
            if y > 1 then vp^.intv^.vt := vvvec
            else vp^.intv^.vt := vvint;
            clrvec(vp^.intv) { clear that }
      
         end else if c = chr(ord(crlv)) then begin { real }
      
            { check already allocated }
            if vp^.rlv <> nil then prterr(earddim);
            getvec(vp^.rlv); { get the starting vector }
            vp^.rlv^.inx := y; { place index count }
            { place type of first vector }
            if y > 1 then vp^.rlv^.vt := vvvec
            else vp^.rlv^.vt := vvrl;
            clrvec(vp^.rlv) { clear that }
      
         end else begin { string }
      
            if y > 1 then begin { more than one index }
      
               { check already allocated }
               if vp^.intv <> nil then prterr(earddim);
               getvec(vp^.strv); { get the starting vector }
               vp^.strv^.inx := y-1; { place index count }
               { place type of first vector }
               if y > 2 then vp^.strv^.vt := vvvec
               else vp^.strv^.vt := vvstr;
               clrvec(vp^.strv) { clear that }
      
            end
      
         end

      end;
      skpspc; { skip spaces }
      c := chkchr; { save ending }
      if c = chr(ord(ccma)) then getchr { skip ',' }

   until c <> chr(ord(ccma)) { until not ',' }
   
end;

{******************************************************************************

Def

Handles the 'def' statement.

******************************************************************************}

procedure sdef(ml:   boolean;  { true if multiline }
               proc: boolean); { true if is a procedure }

var c:          char;
    x, y:       integer;
    varp, varl: varptr;

begin

   if curprg = immprg then prterr(einvimm); { invalid from immediate }
   getchr; { skip 'def' }
   skpspc; { skip spaces }
   { check next is variable }
   if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then prterr(evare);
   c := chkchr; { save head type }
   getchr; { skip }
   x := ord(chkchr); { get variable }
   getchr; { skip }
   if vartbl[x]^.fnc or vartbl[x]^.prc then
      prterr(edupdef); { duplicate definition }
   { note that once the function flag is set, the name is overridden as
     an array }
   if proc then vartbl[x]^.prc := true { set is a procedure definition }
   else vartbl[x]^.fnc := true; { set is a function definition }
   vartbl[x]^.ml := ml; { set multiline type }
   { check untyped procedures }
   if proc and (c <> chr(ord(crlv))) then prterr(eprctyp); { typed }
   { place result type }
   case ktrans[c] of { type }

      cstrv: vartbl[x]^.typ := vtstr; { string }
      cintv: vartbl[x]^.typ := vtint; { integer }
      crlv:  vartbl[x]^.typ := vtrl   { real }

   end;
   skpspc; { skip spaces }
   if chkchr = chr(ord(clpar)) then begin { parameters exist }

      getchr; { skip '(' }
      skpspc; { skip spaces }
      if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then prterr(evare);
      c := chkchr; { save head type }
      getchr; { skip }
      y := ord(chkchr); { get variable }
      getchr; { skip }
      getvarp(varp); { get a parameter entry }
      vartbl[x]^.par := varp; { set as 1st parameter }
      varp^.inx := y; { place variable index }
      varp^.nam := vartbl[y]^.nam; { place name }
      { place parameter type }
      case ktrans[c] of { type }

         cstrv: varp^.typ := vtstr; { string }
         cintv: varp^.typ := vtint; { integer }
         crlv:  varp^.typ := vtrl   { real }

      end;
      varl := varp; { set last entry }
      skpspc; { skip spaces }
      while chkchr = chr(ord(ccma)) do begin { parse parameters }

         getchr; { skip ',' }
         skpspc; { skip spaces }
         if not (ktrans[chkchr] in [cstrv, cintv, crlv]) then 
            prterr(evare);
         c := chkchr; { save head type }
         getchr; { skip }
         y := ord(chkchr); { get variable }
         getchr; { skip }
         getvarp(varp); { get a parameter entry }
         varl^.par := varp; { set as next parameter }
         varp^.inx := y; { place variable index }
         varp^.nam := vartbl[y]^.nam; { place name }
         { place parameter type }
         case ktrans[c] of { type }

            cstrv: varp^.typ := vtstr; { string }
            cintv: varp^.typ := vtint; { integer }
            crlv:  varp^.typ := vtrl   { real }

         end;
         varl := varp; { set last entry }
         skpspc { skip spaces }

      end;
      if chkchr <> chr(ord(crpar)) then prterr(erpe); { ')' expected }
      getchr { skip ')' }
      
   end;
   skpspc; { skip spaces }
   if ml then begin { multiline }

      if chkchr = chr(ord(ccln)) then getchr { skip ':' if present }

   end else begin { single line }

      if chkchr <> chr(ord(cequ)) then prterr(eeque); { '=' expected }
      getchr; { skip '=' }

   end;
   skpspc; { skip spaces }
   { place position of function }
   vartbl[x]^.line := curprg;
   vartbl[x]^.chrp := linec;
   if ml then { skip multiline }
      while not (ktrans[chkchr] in [cendfunc, cendproc, cpend]) do skptlkl;
   { check matching termination }
   if proc and (chkchr <> chr(ord(cendproc))) then prterr(enoendp);
   if not proc and (chkchr <> chr(ord(cendfunc))) then prterr(enoendf);
   while not chksend do skptlk { skip to end of statement }

end;

{******************************************************************************

End function

Ends a multiline function

******************************************************************************}

procedure sendfunc;

begin

   getchr; { skip 'endfunc' }
   expr; { parse return function }
   if fnsstk = nil then prterr(enfact); { no function was active }
   fnsstk^.endf := true; { set function terminated }
   curprg := nil; { terminate }
   goto 2

end;

{******************************************************************************

End procedure

Ends a multiline function

******************************************************************************}

procedure sendproc;

begin

   getchr; { skip 'endproc' }
   if fnsstk = nil then prterr(enpact); { no procedure was active }
   fnsstk^.endf := true; { set function terminated }
   curprg := nil; { terminate }
   goto 2

end;

{******************************************************************************

Randomize

Handles the 'randomize' statement.

******************************************************************************}

procedure srand;

begin

   getchr; { skip 'randomize' }
   { the random number generator is restarted on each run. So to acheive
     a different result on each run, we just need to negate that reset.
     we do this by restoring the last found random seed }
   rndseq := rndsav

end;

{******************************************************************************

Dumpvar

Handles the 'dumpvar' statement.

******************************************************************************}

procedure sdumpv;

var vi: varinx;

procedure dmpvar(vp: varptr);

var vp1: varptr;

begin

   write(vi:1, ': ', vp^.nam, ' ', vp^.ref:3, ' ');
   if vp^.intv = nil then write('<nil> ')
   else write(vp^.intv^.inx:3, '   ');
   if vp^.rlv = nil then write('<nil> ')
   else write(vp^.rlv^.inx:3, '   ');
   if vp^.strv = nil then write('<nil> ')
   else write(vp^.strv^.inx:3, '   ');
   write(vp^.int:intdig, ' ', vp^.rl, ' ');
   if vp^.str = nil then write('<nil>')
   else begin

      write('"');
      prtbstr(filtab[2]^, vp^.str^);
      write('"')

   end;
   writeln;
   vp1 := vp^.rec; { get record root }
   while vp1 <> nil do begin

      write('.');
      dmpvar(vp1);
      vp1 := vp1^.next

   end

end;

begin

   getchr; { skip 'dumpvar' }
   writeln('Variables dump:');
   writeln;
   for vi := 1 to maxvar do { print variables }
      if vartbl[vi] <> nil then { variable active }
      dmpvar(vartbl[vi]); { dump the variable }
   writeln

end;

{******************************************************************************

Dumpprg

Handles the 'dumpprg' statement.

******************************************************************************}

procedure sdumpp;

var y, lc, z: integer;
    pp:       bstptr; { program line pointer }

begin

   getchr; { skip 'dumpprg' }
   writeln('Program encoded dump:');
   writeln;
   pp := prglst; { index top of program }
   while pp <> nil do begin { print program lines }

      prtlin(output, pp); { print line }
      lc := 0; { clear output count }
      for y := 1 to pp^.len do begin { print a tolken code }

         z := ord(pp^.str[y]); { get code }
         write(z div 100:1);
         z := z mod 100;
         write(z div 10:1);
         z := z mod 10;
         write(z:1);
         write(' ');
         lc := lc+1;
         if lc >= 20 then begin writeln; lc := 0 end;

      end;
      writeln;
      pp := pp^.next { next line }

   end

end;

{******************************************************************************

Trace

Handles the 'trace' statement.

******************************************************************************}

procedure strace;

begin

   getchr; { skip 'trace' }
   trace := true { enable tracing }

end;

{******************************************************************************

Notrace

Handles the 'notrace' statement.

******************************************************************************}

procedure snotrace;

begin

   getchr; { skip 'notrace' }
   trace := false { disable tracing }

end;

begin { stat }

   { if trace enabled, print the line before execution }
   if trace then prtlin(output, curprg);
   skpspc;
   cmd := ktrans[chkchr]; { get command tolken }
   { check next tolken is a statement head }
   if not (cmd in [cinput, cprint, cgoto, con, cif, celse, cendif, crem, crema,
                   cstop, crun, clist, cnew, clet, cload, csave, cdata,
                   cread, crestore, cgosub, creturn, cfor, cnext, cto, cwhile,
                   cwend, crepeat, cuntil, cselect, ccase, cother, cendsel,
                   copen, cclose, cend, cdumpv, cdumpp, cdim, cdef, cfunction,
                   cendfunc, cprocedure, cendproc, crand, ctrace,
                   cnotrace, cbye, cintv, cstrv, crlv]) then
      prterr(estate);
   case cmd of { statement }
 
      cinput:       sinput;         { input variable }
      cprint:       sprint;         { print }
      cgoto:        sgoto;          { goto line number }
      cif:          sif;            { if conditional }
      celse:        selse;          { else conditional }
      cendif:       sendif;         { endif conditional }
      crem, crema:  srem;           { remark }
      { stop/end program }
      cstop, 
      cend:         goto 88;
      crun:         srun;           { run program }
      clist, 
      csave:        slstsav(cmd);   { list or save program }
      cload:        sload;          { load program }
      cnew:         snew;           { clear current program }
      cdata:        sdata;          { define data }
      cread:        sread;          { read data }
      crestore:     srestore;       { restore data }
      clet, 
      cintv, 
      cstrv, 
      crlv:         slet;           { assign variable }
      cgosub:       sgosub;         { go subroutine }
      creturn:      sreturn;        { return to caller position }
      cfor:         sfor;           { for loop }
      cnext:        snext;          { next }
      con:          son;            { on..goto or on gosub }
      cwhile:       swhile;         { while }
      cwend:        swend;          { end of while }
      crepeat:      srepeat;        { repeat }
      cuntil:       suntil;         { until }
      cselect:      sselect;        { select }
      ccase:        scase;          { case }
      cother:       scase;          { other }
      cendsel:      sendsel;        { end select }
      cdim:         sdim;           { demension variables }
      cdef:         sdef(false, false); { define function }
      cfunction:    sdef(true, false); { define multiline function }
      cendfunc:     sendfunc;       { end function }
      cprocedure:   sdef(true, true); { define multiline procedure }
      cendproc:     sendproc;       { end procedure }
      crand:        srand;          { randomize random number generator }
      cdumpv:       sdumpv;         { dump variables diagnostic }
      cdumpp:       sdumpp;         { dump program encoded diagnostic }
      ctrace:       strace;         { trace enable }
      cnotrace:     snotrace;       { trace disable }
      copen:        sopen;          { open file }
      cclose:       sclose;         { close file }
      cbye:         goto 99         { exit basic }
 
   end

end; { stat }

begin { execl }

   if linec = 1 then begin { at start of line }

      if chkchr = chr(ord(cintc)) then skptlk; { skip line number }
      skpspc; { skip spaces }
      { as a label looking thing, symbolic goto labels will appear to be real
        variables in source }
      if chkchr = chr(ord(crlv)) then begin { check for line label }

         linecs := linec; { save starting position }
         getchr;
         vi := ord(chkchr); { get variable index }
         getchr;
         { the syntax of a label is identical to a parameterless procedure
           followed by a statement separator (':'). because labels get declared
           immediately on encounter, the procedure interpretation must take
           precidence }
         if vartbl[vi]^.prc and (vartbl[vi]^.par <> nil) then linec := linecs
         else begin { is a possible label }

            { now we find the ':' }
            skpspc; { skip spaces }
            if chkchr = chr(ord(ccln)) then begin { its a label }

               getchr; { get ':' }
               { check label on immediate }
               if curprg = immprg then prterr(elabimm)

            end else linec := linecs { restore }

         end

      end

   end;

   1: { restart line }

   repeat { loop until all partial exec lines cleared }

      skpspc; { skip spaces }
      { if not blank line }
      if chkchr <> chr(ord(clend)) then repeat { execute statements }

         stat; { execute a statement }
         skpspc; { skip spaces }
         c := chkchr; { save next }
         if c = chr(ord(ccln)) then getchr { ':', skip }

      { until not ':', or a "terminal free" verb encountered, else, endif,
        wend and until }
      until not (ktrans[c] in [ccln, celse, cendif, cwend, cuntil, cendsel,
                               cendfunc, cnext, crema]);
      cansif; { cancel single line if }
      skpspc; { skip spaces }
      if chkchr <> chr(ord(clend)) then prterr(eedlexp) { should be at line end }

   until chkchr = chr(ord(clend)); { until line end }
   curprg := curprg^.next; { index next program line }

   2: { next program line }

   cansif { cancel single line 'if's }

end; { execl }

begin { executive }

   writeln;
   writeln('Basic interpreter vs. 0.1 Copyright (C) 1994 S. A. Moore');
   writeln;
   { initalize keycode translation }
   for ki := cinput to clend do ktrans[chr(ord(ki))] := ki;
   { initalize keys }
   for ki := cinput to clend do keywd[ki] := '??????????  ';
   keywd[cinput]       := 'input       '; 
   keywd[cprint]       := 'print       ';
   keywd[cgoto]        := 'goto        '; 
   keywd[con]          := 'on          '; 
   keywd[cif]          := 'if          ';
   keywd[cthen]        := 'then        '; 
   keywd[celse]        := 'else        '; 
   keywd[cendif]       := 'endif       '; 
   keywd[crem]         := 'rem         '; 
   keywd[crema]        := '!           '; 
   keywd[cstop]        := 'stop        ';
   keywd[crun]         := 'run         '; 
   keywd[clist]        := 'list        ';
   keywd[cnew]         := 'new         '; 
   keywd[clet]         := 'let         ';
   keywd[cload]        := 'load        ';
   keywd[csave]        := 'save        ';
   keywd[cdata]        := 'data        ';
   keywd[cread]        := 'read        ';
   keywd[crestore]     := 'restore     ';
   keywd[cgosub]       := 'gosub       '; 
   keywd[creturn]      := 'return      '; 
   keywd[cfor]         := 'for         '; 
   keywd[cnext]        := 'next        '; 
   keywd[cstep]        := 'step        '; 
   keywd[cto]          := 'to          '; 
   keywd[cwhile]       := 'while       '; 
   keywd[cwend]        := 'wend        '; 
   keywd[crepeat]      := 'repeat      '; 
   keywd[cuntil]       := 'until       '; 
   keywd[cselect]      := 'select      ';
   keywd[ccase]        := 'case        ';
   keywd[cother]       := 'other       ';
   keywd[cendsel]      := 'endsel      ';
   keywd[copen]        := 'open        '; 
   keywd[cclose]       := 'close       '; 
   keywd[cend]         := 'end         '; 
   keywd[cdumpv]       := 'dumpvar     '; 
   keywd[cdumpp]       := 'dumpprg     '; 
   keywd[cdim]         := 'dim         '; 
   keywd[cdef]         := 'def         '; 
   keywd[cfunction]    := 'function    '; 
   keywd[cendfunc]     := 'endfunc     '; 
   keywd[cprocedure]   := 'procedure   '; 
   keywd[cendproc]     := 'endproc     '; 
   keywd[crand]        := 'randomize   '; 
   keywd[ctrace]       := 'trace       '; 
   keywd[cas]          := 'as          '; 
   keywd[coutput]      := 'output      '; 
   keywd[cnotrace]     := 'notrace     '; 
   keywd[cbye]         := 'bye         '; 
   keywd[cmod]         := 'mod         ';
   keywd[cidiv]        := 'div         ';
   keywd[cand]         := 'and         ';
   keywd[cor]          := 'or          ';
   keywd[cxor]         := 'xor         ';
   keywd[cnot]         := 'not         ';
   keywd[cleft]        := 'left$       '; 
   keywd[cright]       := 'right$      ';
   keywd[cmid]         := 'mid$        '; 
   keywd[cthen]        := 'then        ';
   keywd[cstr]         := 'str$        '; 
   keywd[cval]         := 'val         ';
   keywd[cchr]         := 'chr$        '; 
   keywd[casc]         := 'asc         '; 
   keywd[clen]         := 'len         '; 
   keywd[csqr]         := 'sqr         '; 
   keywd[cabs]         := 'abs         '; 
   keywd[csgn]         := 'sgn         '; 
   keywd[crnd]         := 'rnd         '; 
   keywd[cint]         := 'int         '; 
   keywd[csin]         := 'sin         '; 
   keywd[ccos]         := 'cos         '; 
   keywd[ctan]         := 'tan         '; 
   keywd[catn]         := 'atn         '; 
   keywd[clog]         := 'log         '; 
   keywd[cexp]         := 'exp         '; 
   keywd[ctab]         := 'tab         '; 
   keywd[cusing]       := 'using       '; 
   keywd[ceof]         := 'eof         '; 
   keywd[clcase]       := 'lcase$      '; 
   keywd[cucase]       := 'ucase$      '; 
   keywd[clequ]        := '<=          ';
   keywd[cgequ]        := '>=          '; 
   keywd[cequ]         := '=           ';
   keywd[cnequ]        := '<>          '; 
   keywd[cltn]         := '<           ';
   keywd[cgtn]         := '>           '; 
   keywd[cadd]         := '+           ';
   keywd[csub]         := '-           '; 
   keywd[cmult]        := '*           ';
   keywd[cdiv]         := '/           '; 
   keywd[cexpn]        := '^           ';
   keywd[cscn]         := ';           '; 
   keywd[ccln]         := ':           '; 
   keywd[clpar]        := '(           '; 
   keywd[crpar]        := ')           '; 
   keywd[ccma]         := ',           '; 
   keywd[cpnd]         := '#           '; 
   keywd[cperiod]      := '.           '; 
   keywd[cintc]        := 'int const   '; 
   keywd[cstrc]        := 'str const   '; 
   keywd[crlc]         := 'real const  '; 
   keywd[cintv]        := 'int var     '; 
   keywd[cstrv]        := 'string var  '; 
   keywd[crlv]         := 'real var    '; 
   keywd[cspc]         := 'space       '; 
   keywd[cspcs]        := 'spaces      '; 
   keywd[cpend]        := 'end of pgm  '; 
   keywd[clend]        := 'end of lin  '; 
   for i := 1 to maxvar do vartbl[i] := nil; { clear active variables table }
   for fi := 1 to maxfil do filtab[fi] := nil; { set files nil }
   for ki := cinput to clend do keycodc[ord(ki)] := ki; 
   varfre := nil; { free variables list }
   ctlstk := nil; { clear control stack }
   ctlfre := nil; { clear free control list }
   strfre := nil; { clear free strings list }
   vecfre := nil; { clear free vectors list }
   prglst := nil; { clear program source list }
   linbuf := nil; { clear line buffer pointer }
   curprg := nil; { clear current execute line }
   filfre := nil; { clear free files list }
   fnsstk := nil; { clear function context stack }
   fnsfre := nil; { clear free function context stack }
   immprg := nil; { clear }
   rndseq := 1; { start random number generator }
   rndsav := rndseq;
   newlin := true; { set printing on new line }
   trace := false; { set no execution trace }
   fsrcop := false; { set source file is not open }
   clear; { clear program }
   { open first two file entries for input and output, respectively }
   getfil(filtab[1]); { get an input file entry }
   filtab[1]^.ty := tyinp; { set up input file }
   filtab[1]^.cp := 1; { set 1st character position }
   filtab[1]^.st := stopenrd; { flag open for read }
   getfil(filtab[2]); { get an output file entry }
   filtab[2]^.ty := tyout; { set up output file }
   filtab[2]^.cp := 1; { set 1st character position }
   filtab[2]^.st := stopenwr; { flag open for write }

   88: { return to interactive line entry } 
   while true do begin

      { restore display parameters to normal }
      if not newlin then writeln; { if not at line start, force it }
      newlin := true;
      writeln('Ready');
      77: { reenter line interpret } 
      getstr(immprg); { get a new program line for entry }
      { get user lines until non-blank }
      repeat inpbstr(input, immprg^) until not null(immprg);
      keycom(immprg);
      if lint(immprg^) > 0 then begin

         clrvars; { clear variables to make room for program }
         enter(immprg); { enter program line }
         datac := prglst; { set data position to program start }
         datal := 1;
         nxtdat; { find first data statement }
         reglab; { find all symbolic labels }
         goto 77

      end else begin

         curprg := immprg; { set immediate as current execute }
         top := 0; { clear stack }
         repeat { execute lines } 

            linec := 1; { set 1st character }
            execl { execute single lines }

         until curprg = nil; { execute lines }
         putstr(immprg); { release immediate buffer }
         immprg := nil { clear }

      end

   end;
   99: { end program }

   { close and release all files }
   for fi := 1 to maxfil do if filtab[fi] <> nil then
      if filtab[fi]^.st <> stclose then 
         if filtab[fi]^.ty = tyoth then closefile(filtab[fi]^.f)

end.
