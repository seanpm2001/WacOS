(*$l-,u-*)
{*******************************************************************************
*                                                                              *
*                           Portable Pascal compiler                           *
*                           ************************                           *
*                                                                              *
*                                 Pascal P5                                    *
*                                                                              *
*                                 ETH May 76                                   *
*                                                                              *
* Authors:                                                                     *
*                                                                              *
*    Urs Ammann                                                                *
*    Kesav Nori                                                                *
*    Christian Jacobi                                                          *
*    K. Jensen                                                                 *
*    N. Wirth                                                                  *
*                                                                              *
* Address:                                                                     *
*                                                                              *
*    Institut Fuer Informatik                                                  *
*    Eidg. Technische Hochschule                                               *
*    CH-8096 Zuerich                                                           *
*                                                                              *
*  This code is fully documented in the book                                   *
*        "Pascal Implementation"                                               *
*   by Steven Pemberton and Martin Daniels                                     *
* published by Ellis Horwood, Chichester, UK                                   *
*         ISBN: 0-13-653-0311                                                  *
*       (also available in Japanese)                                           *
*                                                                              *
* Steven Pemberton, CWI/AA,                                                    *
* Kruislaan 413, 1098 SJ Amsterdam, NL                                         *
* Steven.Pemberton@cwi.nl                                                      *
*                                                                              *
* Adaption from P4 to P5 by:                                                   *
*                                                                              *
*    Scott A. Moore                                                            *
*    samiam@moorecad.com                                                       *
*                                                                              *
* Note for the implementation.                                                 *
* ===========================                                                  *
* This interpreter is written for the case where all the fundamental types     *
* take one storage unit.                                                       *
*                                                                              *
* In an actual implementation, the handling of the sp pointer has to take      *
* into account the fact that the types may have lengths different from one:    *
* in push and pop operations the sp has to be increased and decreased not      *
* by 1, but by a number depending on the type concerned.                       *
*                                                                              *
* However, where the number of units of storage has been computed by the       *
* compiler, the value must not be corrected, since the lengths of the types    *
* involved have already been taken into account.                               *
*                                                                              *
* P5 errors added:                                                             *
*                                                                              *
* 182 identifier too long                                                      *
* 183 For index variable must be local to this block                           *
* 184 Interprocedure goto does not reference outter block of destination       *
*                                                                              *
* P5 instructions modified:                                                    *
*                                                                              *
* lca'string'       '                                                          *
*                                                                              *
* was changed to                                                               *
*                                                                              *
* lca 'string'''                                                               *
*                                                                              *
* That is, lca has a space before the opening quote, no longer pads to the     *
* right, and represents single quotes with a quote image. pint converts quote  *
* images back to single quotes, and pads out strings to their full length.     *
*                                                                              *
* In addition, the way files work was extensively modified. Original P5 could  *
* not represent files as fully expressed variables, such as within an array    *
* or record, and were effectively treated as constants. To treat them as true  *
* variable accesses, the stacking order of the file in all file subroutines    *
* was changed so that the file is on the bottom. This matches the source       *
* order of the file in write(f, ...) or read(f, ...). Also, the file           *
* operations now leave the file on the stack for the duration of a write or    *
* read, then dump them using a specific new instruction "dmp". This allows     *
* multiparameter writes and reads to be effectively a chain of single          *
* operations using one file reference. Finally, files were tied to the type    *
* ending 'a', because files are now full variable references.                  *
*                                                                              *
* New layout of memory in store:                                               *
*                                                                              *
*    maxstr ->    ---------------------                                        *
*                 | Constants         |                                        *
*        cp ->    ---------------------                                        *
*                 | Stack             |                                        *
*        sp ->    ---------------------                                        *
*                 | Free space        |                                        *
*        np ->    ---------------------                                        *
*                 | Heap              |                                        *
*        gbtop -> ---------------------                                        *
*                 | Globals           |                                        *
*        pctop -> ---------------------                                        *
*                 | Code              |                                        *
*                 ---------------------                                        *
*                                                                              *
* The constants are loaded upside down from the top of memory. The heap grows  *
* down, the stack grows up, and when they cross, it is an overflow error.      *
*                                                                              *
*******************************************************************************}

{ Set default configuration flags. This gives proper behavior even if no
  preprocessor flags are passed in.

  The defaults are:
  WRDSIZ32       - 32 bit compiler.
}
#if !defined(WRDSIZ16) && !defined(WRDSIZ32) && !defined(WRDSIZ64)
#define WRDSIZ32 1
#endif

program pcode(input,output,prd,prr);

label 1;

const

      { ************************************************************************

      Program object sizes and characteristics, sync with pint. These define
      the machine specific characteristics of the target.

      The configurations are as follows:

      type                  #bits 32  #bits 64
      ===========================================================
      integer               32        64
      real                  64        64
      char                  8         8
      boolean               8         8
      set                   256       256
      pointers              32        64
      marks                 32        64
      File logical number   8         8

      Both endian types are supported. There is no alignment needed, but you
      may wish to use alignment to tune the runtime speed.

      The machine characteristics dependent on byte accessable machines. This
      table is all you should need to adapt to any byte addressable machine.

      }

#ifdef WRDSIZ16
#include "mpb16.inc"
#endif

#ifdef WRDSIZ32
#include "mpb32.inc"
#endif

#ifdef WRDSIZ64
#include "mpb64.inc"
#endif

      { ******************* end of pcom and pint common parameters *********** }

      { internal constants }

      { !!! Need to use the small size memory to self compile, otherwise, by
        definition, pint cannot fit into its own memory. }
#ifndef SELF_COMPILE
      maxstr      = 16777215;  { maximum size of addressing for program/var }
      maxtop      = 16777216;  { maximum size of addressing for program/var+1 }
      maxdef      = 2097152;   { maxstr / 8 for defined bits }
#else
      maxstr     =  2000000;   { maximum size of addressing for program/var }
      maxtop     =  2000001;   { maximum size of addressing for program/var+1 }
      maxdef      = 250000;    { maxstr /8 for defined bits }
#endif

      maxdigh     = 6;       { number of digits in hex representation of maxstr }
      maxdigd     = 8;       { number of digits in decimal representation of maxstr }

      codemax     = maxstr;  { set size of code store to maximum possible }

      maxlabel = 5000;       { total possible labels in intermediate }
      resspc   = 0;          { reserve space in heap (if you want) }

      { locations of header files after program block mark, each header
        file is two values, a file number and a single character buffer }
      filres    = 2;         { space reserved for file }
      inputoff  = 0;         { 'input' file address }
      outputoff = 2;         { 'output' file address }
      prdoff    = 4;         { 'prd' file address }
      prroff    = 6;         { 'prr' file address }

      { assigned logical channels for header files }
      inputfn    = 1;        { 'input' file no. }
      outputfn   = 2;        { 'output' file no. }
      prdfn      = 3;        { 'prd' file no. }
      prrfn      = 4;        { 'prr' file no. }

      stringlgth  = 1000;    { longest string length we can buffer }
      maxsp       = 46;      { number of predefined procedures/functions }
      maxins      = 255;     { maximum instruction code, 0-255 or byte }
      maxfil      = 100;     { maximum number of general (temp) files }
      maxalfa     = 10;      { maximum number of characters in alfa type }

      { version numbers }

      majorver   = 1; { major version number }
      minorver   = 4; { minor version number }
      experiment = false; { is version experimental? }

type
      { These equates define the instruction layout. I have choosen a 32 bit
        layout for the instructions defined by (4 bit) digit:

           byte 0:   Instruction code
           byte 1:   P parameter
           byte 2-5: Q parameter

        This means that there are 256 instructions, 256 procedure levels,
        and 2gb of total addressing. This could be 4gb if we get rid of the
        need for negatives. }
      lvltyp      = 0..255;     { procedure/function level }
      instyp      = 0..maxins;  { instruction }
      address     = -maxstr..maxtop; { address }

      beta        = packed array[1..25] of char; (*error message*)
      settype     = set of setlow..sethigh;
      alfainx     = 1..maxalfa; { index for alfa type }
      alfa        = packed array[alfainx] of char;
      byte        = 0..255; { 8-bit byte }
      bytfil      = packed file of byte; { untyped file of bytes }
      fileno      = 0..maxfil; { logical file number }
      { VAR reference block }
      varptr       = ^varblk;
      varblk       = record
                       next: varptr; { next entry }
                       s, e: address { start and end address of block }
                     end;
      { with reference block }
      wthptr       = ^wthblk;
      wthblk       = record
                       next: wthptr; { next entry }
                       b: address    { address of block }
                     end;

var   pc          : address;   (*program address register*)
      pctop,lsttop: address;   { top of code store }
      gbtop, gbsiz: address;   { top of globals, size of globals }
      gbset       : boolean;   { global size was set }
      op : instyp; p : lvltyp; q : integer;  (*instruction register*)
      q1, q2: integer; { extra parameters }
      store       : packed array [0..maxstr] of byte; { complete program storage }
      storedef    : packed array [0..maxdef] of byte; { defined bits }
      sdi         : 0..maxdef; { index for that }
      cp          : address;  (* pointer to next free constant position *)
      mp,sp,np,ep : address;  (* address registers *)
      (*mp  points to beginning of a data segment
        sp  points to top of the stack
        ep  points to the maximum extent of the stack
        np  points to top of the dynamically allocated area*)
      bitmsk      : packed array [0..7] of byte; { bits in byte }

      interpreting: boolean;

      { !!! remove this next statement for self compile }
#ifndef SELF_COMPILE
      prd,prr     : text; (*prd for read only, prr for write only *)
#endif

      instr       : array[instyp] of alfa; (* mnemonic instruction codes *)
      sptable     : array[0..maxsp] of alfa; (*standard functions and procedures*)
      insp        : array[instyp] of boolean; { instruction includes a p parameter }
      insq        : array[instyp] of 0..32; { length of q parameter }
      srclin      : integer; { current source line executing }
      option      : array ['a'..'z'] of boolean; { option array }

      { check flags: these turn on runtime checks }
      dochkovf    : boolean;    { check arithmetic overflow }

      { debug flags: turn these on for various dumps and traces }
      dodmpins    : boolean;    { dump instructions after assembly }
      dodmplab    : boolean;    { dump label definitions }
      dodmpsto    : boolean;    { dump storage area specs }
      dotrcrot    : boolean;    { trace routine executions }
      dotrcins    : boolean;    { trace instruction executions }
      dopmd       : boolean;    { perform post-mortem dump on error }
      dosrclin    : boolean;    { add source line sets to code }
      dotrcsrc    : boolean;    { trace source line executions (requires dosrclin) }
      dodmpspc    : boolean;    { dump heap space after execution }
      dorecycl    : boolean;    { obey heap space recycle requests }
      { invoke a special recycle mode that creates single word entries on
        recycle of any object, breaking off and recycling the rest. Once
        allocated, each entry exists forever, and accesses to it can be
        checked. }
      dochkrpt    : boolean;    { check reuse of freed entry (automatically
                                  invokes dorecycl = false }
      dochkdef    : boolean;    { check undefined accesses }

      filtable    : array [1..maxfil] of text; { general (temp) text file holders }
      { general (temp) binary file holders }
      bfiltable   : array [1..maxfil] of bytfil;
      { file state holding }
      filstate    : array [1..maxfil] of (fclosed, fread, fwrite);
      { file buffer full status }
      filbuff     : array [1..maxfil] of boolean;
      varlst      : varptr; { active var block pushdown stack }
      varfre      : varptr; { free var block entries }
      wthlst      : wthptr; { active with block pushdown stack }
      wthcnt      : integer; { number of outstanding with levels }
      wthfre      : wthptr; { free with block entries }
      errsinprg   : integer; { errors in source program }
      maxpow16    : integer; { maximum power of 16 }
      hexdig      : integer; { digits in unsigned hex }

      (*locally used for interpreting one instruction*)
      ad,ad1,ad2  : address;
      b           : boolean;
      i,j,k,i1,i2 : integer;
      c           : char;
      i3, i4      : integer;
      r1, r2      : real;
      b1, b2      : boolean;
      s1, s2      : settype;
      c1          : char;
      a1, a2, a3  : address;
      pcs         : address;
      bai         : integer;

(*--------------------------------------------------------------------*)

{ Low level error check and handling }

{ print in hex (carefull, it chops high digits freely!) }

procedure wrthex(v: integer; { value } f: integer { field });
var p,i,d,t,n: integer;
    digits: packed array [1..inthex] of char;
function digit(d: integer): char;
begin
  if d < 10 then c := chr(d+ord('0'))
  else c := chr(d-10+ord('A'));
  digit := c
end;
begin
   n := inthex; { number of digits }
   if v < 0 then begin { signed }
     v := v+1+maxint; { convert number to 31 bit unsigned }
     t := v div maxpow16+8; { extract high digit }
     digits[inthex] := digit(t); { place high digit }
     v := v mod maxpow16; { remove digit }
     n := inthex-1 { set number of digits-1 }
   end;
   p := 1;
   for i := 1 to n do begin
      d := v div p mod 16; { extract digit }
      digits[i] := digit(d); { place }
      if i < inthex then p := p*16
   end;
   for i := f downto 1 do write(digits[i]) { output }
end;

procedure dmpmem(s, e: address);
   var i, x: integer;
       bs: array [1..16] of byte;
       f, l: boolean;
       ba: address;
begin l := false; for i := 1 to 16 do bs[i] := 0;
   while s <= e do begin
     ba := s; i := 1; f := true;
     while (s <= e) and (i <= 16) do begin
       if bs[i] <> store[s] then f := false;
       bs[i] := store[s]; s := s+1; i := i+1
     end;
     if not f or (i < 16) then begin
       if l then begin
         writeln;
         for x := 1 to maxdigh do write('*'); write(': ');
         for x := 1 to 16 do write('** ');
       end;
       writeln; wrthex(ba, maxdigh); write(': ');
       for x := 1 to i-1 do begin wrthex(bs[x], 2); write(' ') end;
       l := false
     end else l := true
   end
end;

procedure pmd;
begin
   if dopmd then begin
      writeln;
      write('pc = ');  wrthex(pcs, maxdigh);
      write(' op = '); wrthex(op, 2);
      write(' sp = '); wrthex(sp, maxdigh);
      write(' mp = '); wrthex(mp, maxdigh);
      write(' np = '); wrthex(np, maxdigh);
      write(' cp = '); wrthex(cp, maxdigh);
      writeln;
      write('------------------------------------------------------------');
      writeln('-------------');

      writeln;
      writeln('Stack');
      writeln;
      dmpmem(pctop, sp-1);
      writeln;
      writeln;
      writeln('Constants');
      writeln;
      dmpmem(cp, maxstr);
      writeln;
      writeln;
      writeln('Heap');
      writeln;
      dmpmem(np, cp-1);
      writeln;
   end
end; (*pmd*)

procedure errori(string: beta);
begin writeln; write('*** Runtime error');
      if srclin > 0 then write(' [', srclin:1, ']');
      writeln(': ', string);
      pmd; goto 1
end;(*errori*)

{ get bit from defined array }

function getdef(a: address): boolean;

var b: byte;
    r: boolean;

begin

  if dochkdef then begin

    b := storedef[a div 8]; { get byte }
    r := odd(b div bitmsk[a mod 8])

  end else r := true; { always set defined }

  getdef := r

end;

{ put bit to defined array }

procedure putdef(a: address; b: boolean);

var sb: byte;
    r:  boolean;

begin

  if dochkdef then begin

    sb := storedef[a div 8]; { get byte }
    { test bit as is }
    r := odd(sb div bitmsk[a mod 8]);
    if r <> b then begin

      if b then sb := sb+bitmsk[a mod 8]
      else sb := sb-bitmsk[a mod 8];
      storedef[a div 8] := sb

    end

  end

end;

procedure chkdef(a: address);
begin
   if dochkdef then if not getdef(a) then errori('Undefined location access')
end;

(*--------------------------------------------------------------------*)

{ Accessor functions

  These translate store variables to internal, and convert to and from store RAM
  formats.

  The acessors are fairly machine independent, they rely here on the machine
  being byte addressable. The endian format is inherent to the machine.

  The exception are the get/put int8,16,32,64 and 128 bit routines, which are
  dependent on the endian mode of the machine.

}

function getint(a: address): integer;

var r: record case boolean of

          true:  (i: integer);
          false: (b: packed array [1..intsize] of byte);

       end;
    i: 1..intsize;

begin

   if dochkdef then chkdef(a);
   for i := 1 to intsize do r.b[i] := store[a+i-1];

   getint := r.i

end;

procedure putint(a: address; x: integer);

var r: record case boolean of

          true:  (i: integer);
          false: (b: packed array [1..intsize] of byte);

       end;
    i: 1..intsize;

begin

   r.i := x;
   for i := 1 to intsize do
     begin store[a+i-1] := r.b[i]; putdef(a+i-1, true) end

end;

function getrel(a: address): real;

var r: record case boolean of

          true:  (r: real);
          false: (b: packed array [1..realsize] of byte);

       end;
    i: 1..realsize;

begin

   if dochkdef then chkdef(a);
   for i := 1 to realsize do r.b[i] := store[a+i-1];
   getrel := r.r

end;

procedure putrel(a: address; f: real);

var r: record case boolean of

          true:  (r: real);
          false: (b: packed array [1..realsize] of byte);

       end;
    i: 1..realsize;

begin

   r.r := f;
   for i := 1 to realsize do
     begin store[a+i-1] := r.b[i]; putdef(a+i-1, true) end

end;

function getbol(a: address): boolean;

var b: boolean;

begin

   if dochkdef then chkdef(a);
   if store[a] = 0 then b := false else b := true;
   getbol := b

end;

procedure putbol(a: address; b: boolean);

begin

   store[a] := ord(b); putdef(a, true)

end;

procedure getset(a: address; var s: settype);

var r: record case boolean of

          true:  (s: settype);
          false: (b: packed array [1..setsize] of byte);

       end;
    i: 1..setsize;

begin

   if dochkdef then chkdef(a);
   for i := 1 to setsize do r.b[i] := store[a+i-1];
   s := r.s

end;

procedure putset(a: address; s: settype);

var r: record case boolean of

          true:  (s: settype);
          false: (b: packed array [1..setsize] of byte);

       end;
    i: 1..setsize;

begin

   r.s := s;
   for i := 1 to setsize do
     begin store[a+i-1] := r.b[i]; putdef(a+i-1, true) end

end;

function getchr(a: address): char;

begin

   if dochkdef then chkdef(a);
   getchr := chr(store[a])

end;

procedure putchr(a: address; c: char);

begin

   store[a] := ord(c); putdef(a, true)

end;

function getbyt(a: address): byte;

begin

   if dochkdef then chkdef(a);
   getbyt := store[a]

end;

procedure putbyt(a: address; b: byte);

begin

   store[a] := b; putdef(a, true)

end;

function getadr(a: address): address;

var r: record case boolean of

          true:  (a: address);
          false: (b: packed array [1..adrsize] of byte);

       end;
    i: 1..adrsize;

begin

   if dochkdef then chkdef(a);
   for i := 1 to adrsize do r.b[i] := store[a+i-1];
   getadr := r.a

end;

procedure putadr(a: address; ad: address);

var r: record case boolean of

          true:  (a: address);
          false: (b: packed array [1..adrsize] of byte);

       end;
    i: 1..adrsize;

begin

   r.a := ad;
   for i := 1 to adrsize do
     begin store[a+i-1] := r.b[i]; putdef(a+i-1, true) end

end;

{ Swap pointer on top with second on stack. The size of the second is given. }

procedure swpstk(l: address);

var sb: packed array [1..maxsize] of byte;
    p:  address;
    i:  1..maxsize;

begin

   { get the top pointer }
   p := getadr(sp);
   { load up the second on stack }
   for i := 1 to l do sb[i] := store[sp+adrsize+i-1];
   putadr(sp+l, p); { place pointer at bottom }
   for i := 1 to l do begin
     store[sp+i-1] := sb[i]; { place second as new top }
     putdef(sp+i-1, true)
   end

end;

{ end of accessor functions

(*--------------------------------------------------------------------*)

{ Push/pop

  These routines handle both the data type, and their lengths on the stack.

}

procedure popint(var i: integer); begin i := getint(sp); sp := sp+intsize end;
procedure pshint(i: integer); begin sp := sp-intsize; putint(sp, i) end;
procedure poprel(var r: real); begin r := getrel(sp); sp := sp+realsize end;
procedure pshrel(r: real); begin sp := sp-realsize; putrel(sp, r) end;
procedure popset(var s: settype); begin getset(sp, s); sp := sp+setsize end;
procedure pshset(s: settype); begin sp := sp-setsize; putset(sp, s) end;
procedure popadr(var a: address); begin a := getadr(sp); sp := sp+adrsize end;
procedure pshadr(a: address); begin sp := sp-adrsize; putadr(sp, a) end;

{ list single instruction at address }

procedure lstins(var ad: address);

var op: instyp; p : lvltyp; q, q1,q2 : integer;  (*instruction register*)

begin

   { fetch instruction from byte store }
   op := store[ad]; ad := ad+1;
   if insp[op] then begin p := store[ad]; ad := ad+1 end;
   if insq[op] > 0 then begin

      if insq[op] = 1 then q := store[ad]
      else begin
        q := getint(ad);
        if insq[op] > intsize then q1 := getint(ad+intsize);
        if insq[op] > intsize*2 then q2 := getint(ad+intsize*2);
      end;
      ad := ad+insq[op]

   end;
   write(': ');
   wrthex(op, 2);
   write(' ', instr[op]:10, '  ');
   if insp[op] then begin

      wrthex(p, 2);
      if insq[op] > 0 then begin write(','); wrthex(q, inthex) end;
      if insq[op] > intsize then  begin write(','); wrthex(q1, inthex) end;
      if insq[op] > intsize*2 then begin write(','); wrthex(q2, inthex) end

   end else if insq[op] > 0 then begin

      write('   '); wrthex(q, inthex);
      if insq[op] > intsize then begin write(','); wrthex(q1, inthex) end;
      if insq[op] > intsize*2 then begin write(','); wrthex(q2, inthex) end

   end

end;

{ dump contents of instruction memory }

procedure dmpins;

var i:  address;

begin

   writeln;
   writeln('Contents of instruction memory');
   writeln;
   writeln('Addr    Op Ins         P  Q');
   writeln('----------------------------------');
   i := 0;
   while i < lsttop do begin

      wrthex(i, maxdigh);
      lstins(i);
      writeln

   end;
   writeln

end;

{ align address, upwards }

procedure alignu(algn: address; var flc: address);
  var l: integer;
begin
  l := flc-1;
  flc := l + algn  -  (algn+l) mod algn
end (*align*);

{ align address, downwards }

procedure alignd(algn: address; var flc: address);
  var l: integer;
begin
  l := flc+1;
  flc := l - algn  +  (algn-l) mod algn
end (*align*);

(*--------------------------------------------------------------------*)

{ load intermediate file }

procedure load;
   type  labelst  = (entered,defined); (*label situation*)
         labelrg  = 0..maxlabel;       (*label range*)
         labelrec = record
                          val: address;
                           st: labelst
                    end;
   var  word : array[alfainx] of char; ch  : char;
        labeltab: array[labelrg] of labelrec;
        labelvalue: address;
        iline: integer; { line number of intermediate file }

   procedure init;
      var i: integer;
   begin for i := 0 to maxins do instr[i] := '          ';
         {

           Notes:

           1. Instructions marked with "*" are for internal use only.
              The "*" mark both shows in the listing, and also prevents
              their use in the intermediate file, since only alpha
              characters are allowed as opcode labels.

           2. "---" entries are unused.

         }
         instr[  0]:='lodi      '; insp[  0] := true;  insq[  0] := intsize;
         instr[  1]:='ldoi      '; insp[  1] := false; insq[  1] := intsize;
         instr[  2]:='stri      '; insp[  2] := true;  insq[  2] := intsize;
         instr[  3]:='sroi      '; insp[  3] := false; insq[  3] := intsize;
         instr[  4]:='lda       '; insp[  4] := true;  insq[  4] := intsize;
         instr[  5]:='lao       '; insp[  5] := false; insq[  5] := intsize;
         instr[  6]:='stoi      '; insp[  6] := false; insq[  6] := 0;
         instr[  7]:='ldcs      '; insp[  7] := false; insq[  7] := intsize;
         instr[  8]:='cvbi      '; insp[  8] := false; insq[  8] := intsize*3;
         instr[  9]:='indi      '; insp[  9] := false; insq[  9] := intsize;
         instr[ 10]:='inci      '; insp[ 10] := false; insq[ 10] := intsize;
         instr[ 11]:='mst       '; insp[ 11] := true;  insq[ 11] := intsize;
         instr[ 12]:='cup       '; insp[ 12] := true;  insq[ 12] := intsize;
         instr[ 13]:='ents      '; insp[ 13] := false; insq[ 13] := intsize;
         instr[ 14]:='retp      '; insp[ 14] := false; insq[ 14] := 0;
         instr[ 15]:='csp       '; insp[ 15] := false; insq[ 15] := 1;
         instr[ 16]:='ixa       '; insp[ 16] := false; insq[ 16] := intsize;
         instr[ 17]:='equa      '; insp[ 17] := false; insq[ 17] := 0;
         instr[ 18]:='neqa      '; insp[ 18] := false; insq[ 18] := 0;
         instr[ 19]:='vbs       '; insp[ 19] := false; insq[ 19] := intsize;
         instr[ 20]:='vbe       '; insp[ 20] := false; insq[ 20] := 0;
         instr[ 21]:='cvbx      '; insp[ 21] := false; insq[ 21] := intsize*3;
         instr[ 22]:='cvbb      '; insp[ 22] := false; insq[ 22] := intsize*3;
         instr[ 23]:='ujp       '; insp[ 23] := false; insq[ 23] := intsize;
         instr[ 24]:='fjp       '; insp[ 24] := false; insq[ 24] := intsize;
         instr[ 25]:='xjp       '; insp[ 25] := false; insq[ 25] := intsize;
         instr[ 26]:='chki      '; insp[ 26] := false; insq[ 26] := intsize;
         instr[ 27]:='cvbc      '; insp[ 27] := false; insq[ 27] := intsize*3;
         instr[ 28]:='adi       '; insp[ 28] := false; insq[ 28] := 0;
         instr[ 29]:='adr       '; insp[ 29] := false; insq[ 29] := 0;
         instr[ 30]:='sbi       '; insp[ 30] := false; insq[ 30] := 0;
         instr[ 31]:='sbr       '; insp[ 31] := false; insq[ 31] := 0;
         instr[ 32]:='sgs       '; insp[ 32] := false; insq[ 32] := 0;
         instr[ 33]:='flt       '; insp[ 33] := false; insq[ 33] := 0;
         instr[ 34]:='flo       '; insp[ 34] := false; insq[ 34] := 0;
         instr[ 35]:='trc       '; insp[ 35] := false; insq[ 35] := 0;
         instr[ 36]:='ngi       '; insp[ 36] := false; insq[ 36] := 0;
         instr[ 37]:='ngr       '; insp[ 37] := false; insq[ 37] := 0;
         instr[ 38]:='sqi       '; insp[ 38] := false; insq[ 38] := 0;
         instr[ 39]:='sqr       '; insp[ 39] := false; insq[ 39] := 0;
         instr[ 40]:='abi       '; insp[ 40] := false; insq[ 40] := 0;
         instr[ 41]:='abr       '; insp[ 41] := false; insq[ 41] := 0;
         instr[ 42]:='not       '; insp[ 42] := false; insq[ 42] := 0;
         instr[ 43]:='and       '; insp[ 43] := false; insq[ 43] := 0;
         instr[ 44]:='ior       '; insp[ 44] := false; insq[ 44] := 0;
         instr[ 45]:='dif       '; insp[ 45] := false; insq[ 45] := 0;
         instr[ 46]:='int       '; insp[ 46] := false; insq[ 46] := 0;
         instr[ 47]:='uni       '; insp[ 47] := false; insq[ 47] := 0;
         instr[ 48]:='inn       '; insp[ 48] := false; insq[ 48] := 0;
         instr[ 49]:='mod       '; insp[ 49] := false; insq[ 49] := 0;
         instr[ 50]:='odd       '; insp[ 50] := false; insq[ 50] := 0;
         instr[ 51]:='mpi       '; insp[ 51] := false; insq[ 51] := 0;
         instr[ 52]:='mpr       '; insp[ 52] := false; insq[ 52] := 0;
         instr[ 53]:='dvi       '; insp[ 53] := false; insq[ 53] := 0;
         instr[ 54]:='dvr       '; insp[ 54] := false; insq[ 54] := 0;
         instr[ 55]:='mov       '; insp[ 55] := false; insq[ 55] := intsize;
         instr[ 56]:='lca       '; insp[ 56] := false; insq[ 56] := intsize;
         instr[ 57]:='deci      '; insp[ 57] := false; insq[ 57] := intsize;
         instr[ 58]:='stp       '; insp[ 58] := false; insq[ 58] := 0;
         instr[ 59]:='ordi      '; insp[ 59] := false; insq[ 59] := 0;
         instr[ 60]:='chr       '; insp[ 60] := false; insq[ 60] := 0;
         instr[ 61]:='ujc       '; insp[ 61] := false; insq[ 61] := intsize;
         instr[ 62]:='rnd       '; insp[ 62] := false; insq[ 62] := 0;
         instr[ 63]:='pck       '; insp[ 63] := false; insq[ 63] := intsize*2;
         instr[ 64]:='upk       '; insp[ 64] := false; insq[ 64] := intsize*2;
         instr[ 65]:='ldoa      '; insp[ 65] := false; insq[ 65] := intsize;
         instr[ 66]:='ldor      '; insp[ 66] := false; insq[ 66] := intsize;
         instr[ 67]:='ldos      '; insp[ 67] := false; insq[ 67] := intsize;
         instr[ 68]:='ldob      '; insp[ 68] := false; insq[ 68] := intsize;
         instr[ 69]:='ldoc      '; insp[ 69] := false; insq[ 69] := intsize;
         instr[ 70]:='stra      '; insp[ 70] := true;  insq[ 70] := intsize;
         instr[ 71]:='strr      '; insp[ 71] := true;  insq[ 71] := intsize;
         instr[ 72]:='strs      '; insp[ 72] := true;  insq[ 72] := intsize;
         instr[ 73]:='strb      '; insp[ 73] := true;  insq[ 73] := intsize;
         instr[ 74]:='strc      '; insp[ 74] := true;  insq[ 74] := intsize;
         instr[ 75]:='sroa      '; insp[ 75] := false; insq[ 75] := intsize;
         instr[ 76]:='sror      '; insp[ 76] := false; insq[ 76] := intsize;
         instr[ 77]:='sros      '; insp[ 77] := false; insq[ 77] := intsize;
         instr[ 78]:='srob      '; insp[ 78] := false; insq[ 78] := intsize;
         instr[ 79]:='sroc      '; insp[ 79] := false; insq[ 79] := intsize;
         instr[ 80]:='stoa      '; insp[ 80] := false; insq[ 80] := 0;
         instr[ 81]:='stor      '; insp[ 81] := false; insq[ 81] := 0;
         instr[ 82]:='stos      '; insp[ 82] := false; insq[ 82] := 0;
         instr[ 83]:='stob      '; insp[ 83] := false; insq[ 83] := 0;
         instr[ 84]:='stoc      '; insp[ 84] := false; insq[ 84] := 0;
         instr[ 85]:='inda      '; insp[ 85] := false; insq[ 85] := intsize;
         instr[ 86]:='indr      '; insp[ 86] := false; insq[ 86] := intsize;
         instr[ 87]:='inds      '; insp[ 87] := false; insq[ 87] := intsize;
         instr[ 88]:='indb      '; insp[ 88] := false; insq[ 88] := intsize;
         instr[ 89]:='indc      '; insp[ 89] := false; insq[ 89] := intsize;
         instr[ 90]:='inca      '; insp[ 90] := false; insq[ 90] := intsize;
         instr[ 91]:='ivtx      '; insp[ 91] := false; insq[ 91] := intsize*3;
         instr[ 92]:='ivtb      '; insp[ 92] := false; insq[ 92] := intsize*3;
         instr[ 93]:='incb      '; insp[ 93] := false; insq[ 93] := intsize;
         instr[ 94]:='incc      '; insp[ 94] := false; insq[ 94] := intsize;
         instr[ 95]:='chka      '; insp[ 95] := false; insq[ 95] := intsize;
         instr[ 96]:='ivtc      '; insp[ 96] := false; insq[ 96] := intsize*3;
         instr[ 97]:='chks      '; insp[ 97] := false; insq[ 97] := intsize;
         instr[ 98]:='chkb      '; insp[ 98] := false; insq[ 98] := intsize;
         instr[ 99]:='chkc      '; insp[ 99] := false; insq[ 99] := intsize;
         instr[100]:='ctp       '; insp[100] := false; insq[100] := intsize;
         instr[101]:='wbs       '; insp[101] := false; insq[101] := intsize;
         instr[102]:='wbe       '; insp[102] := false; insq[102] := intsize;
         instr[103]:='decb      '; insp[103] := false; insq[103] := intsize;
         instr[104]:='decc      '; insp[104] := false; insq[104] := intsize;
         instr[105]:='loda      '; insp[105] := true;  insq[105] := intsize;
         instr[106]:='lodr      '; insp[106] := true;  insq[106] := intsize;
         instr[107]:='lods      '; insp[107] := true;  insq[107] := intsize;
         instr[108]:='lodb      '; insp[108] := true;  insq[108] := intsize;
         instr[109]:='lodc      '; insp[109] := true;  insq[109] := intsize;
         instr[110]:='rgs       '; insp[110] := false; insq[110] := 0;
         instr[111]:='---       '; insp[111] := false; insq[111] := 0;
         instr[112]:='ipj       '; insp[112] := true;  insq[112] := intsize;
         instr[113]:='cip       '; insp[113] := true;  insq[113] := 0;
         instr[114]:='lpa       '; insp[114] := true;  insq[114] := intsize;
         instr[115]:='---       '; insp[115] := false; insq[115] := 0;
         instr[116]:='---       '; insp[116] := false; insq[116] := 0;
         instr[117]:='dmp       '; insp[117] := false; insq[117] := intsize;
         instr[118]:='swp       '; insp[118] := false; insq[118] := intsize;
         instr[119]:='tjp       '; insp[119] := false; insq[119] := intsize;
         instr[120]:='lip       '; insp[120] := true;  insq[120] := intsize;
         instr[121]:='---       '; insp[121] := false; insq[121] := 0;
         instr[122]:='---       '; insp[122] := false; insq[122] := 0;
         instr[123]:='ldci      '; insp[123] := false; insq[123] := intsize;
         instr[124]:='ldcr      '; insp[124] := false; insq[124] := intsize;
         instr[125]:='ldcn      '; insp[125] := false; insq[125] := 0;
         instr[126]:='ldcb      '; insp[126] := false; insq[126] := boolsize;
         instr[127]:='ldcc      '; insp[127] := false; insq[127] := charsize;
         instr[128]:='reti      '; insp[128] := false; insq[128] := 0;
         instr[129]:='retr      '; insp[129] := false; insq[129] := 0;
         instr[130]:='retc      '; insp[130] := false; insq[130] := 0;
         instr[131]:='retb      '; insp[131] := false; insq[131] := 0;
         instr[132]:='reta      '; insp[132] := false; insq[132] := 0;
         instr[133]:='---       '; insp[133] := false; insq[133] := 0;
         instr[134]:='ordb      '; insp[134] := false; insq[134] := 0;
         instr[135]:='---       '; insp[135] := false; insq[135] := 0;
         instr[136]:='ordc      '; insp[136] := false; insq[136] := 0;
         instr[137]:='equi      '; insp[137] := false; insq[137] := 0;
         instr[138]:='equr      '; insp[138] := false; insq[138] := 0;
         instr[139]:='equb      '; insp[139] := false; insq[139] := 0;
         instr[140]:='equs      '; insp[140] := false; insq[140] := 0;
         instr[141]:='equc      '; insp[141] := false; insq[141] := 0;
         instr[142]:='equm      '; insp[142] := false; insq[142] := intsize;
         instr[143]:='neqi      '; insp[143] := false; insq[143] := 0;
         instr[144]:='neqr      '; insp[144] := false; insq[144] := 0;
         instr[145]:='neqb      '; insp[145] := false; insq[145] := 0;
         instr[146]:='neqs      '; insp[146] := false; insq[146] := 0;
         instr[147]:='neqc      '; insp[147] := false; insq[147] := 0;
         instr[148]:='neqm      '; insp[148] := false; insq[148] := intsize;
         instr[149]:='geqi      '; insp[149] := false; insq[149] := 0;
         instr[150]:='geqr      '; insp[150] := false; insq[150] := 0;
         instr[151]:='geqb      '; insp[151] := false; insq[151] := 0;
         instr[152]:='geqs      '; insp[152] := false; insq[152] := 0;
         instr[153]:='geqc      '; insp[153] := false; insq[153] := 0;
         instr[154]:='geqm      '; insp[154] := false; insq[154] := intsize;
         instr[155]:='grti      '; insp[155] := false; insq[155] := 0;
         instr[156]:='grtr      '; insp[156] := false; insq[156] := 0;
         instr[157]:='grtb      '; insp[157] := false; insq[157] := 0;
         instr[158]:='grts      '; insp[158] := false; insq[158] := 0;
         instr[159]:='grtc      '; insp[159] := false; insq[159] := 0;
         instr[160]:='grtm      '; insp[160] := false; insq[160] := intsize;
         instr[161]:='leqi      '; insp[161] := false; insq[161] := 0;
         instr[162]:='leqr      '; insp[162] := false; insq[162] := 0;
         instr[163]:='leqb      '; insp[163] := false; insq[163] := 0;
         instr[164]:='leqs      '; insp[164] := false; insq[164] := 0;
         instr[165]:='leqc      '; insp[165] := false; insq[165] := 0;
         instr[166]:='leqm      '; insp[166] := false; insq[166] := intsize;
         instr[167]:='lesi      '; insp[167] := false; insq[167] := 0;
         instr[168]:='lesr      '; insp[168] := false; insq[168] := 0;
         instr[169]:='lesb      '; insp[169] := false; insq[169] := 0;
         instr[170]:='less      '; insp[170] := false; insq[170] := 0;
         instr[171]:='lesc      '; insp[171] := false; insq[171] := 0;
         instr[172]:='lesm      '; insp[172] := false; insq[172] := intsize;
         instr[173]:='ente      '; insp[173] := false; insq[173] := intsize;
         instr[174]:='mrkl*     '; insp[174] := false; insq[174] := intsize;
         instr[175]:='ckvi      '; insp[175] := false; insq[175] := intsize;
         instr[176]:='---       '; insp[176] := false; insq[176] := intsize;
         instr[177]:='---       '; insp[177] := false; insq[177] := intsize;
         instr[178]:='---       '; insp[178] := false; insq[178] := intsize;
         instr[179]:='ckvb      '; insp[179] := false; insq[179] := intsize;
         instr[180]:='ckvc      '; insp[180] := false; insq[180] := intsize;
         instr[181]:='dupi      '; insp[181] := false; insq[181] := 0;
         instr[182]:='dupa      '; insp[182] := false; insq[182] := 0;
         instr[183]:='dupr      '; insp[183] := false; insq[183] := 0;
         instr[184]:='dups      '; insp[184] := false; insq[184] := 0;
         instr[185]:='dupb      '; insp[185] := false; insq[185] := 0;
         instr[186]:='dupc      '; insp[186] := false; insq[186] := 0;
         instr[187]:='cks       '; insp[187] := false; insq[187] := 0;
         instr[188]:='cke       '; insp[188] := false; insq[188] := 0;
         instr[189]:='inv       '; insp[189] := false; insq[189] := 0;
         instr[190]:='ckla      '; insp[190] := false; insq[190] := intsize;
         instr[191]:='cta       '; insp[191] := false; insq[191] := intsize*3;
         instr[192]:='ivti      '; insp[192] := false; insq[192] := intsize*3;
         instr[193]:='lodx      '; insp[193] := true;  insq[193] := intsize;
         instr[194]:='ldox      '; insp[194] := false; insq[194] := intsize;
         instr[195]:='strx      '; insp[195] := true;  insq[195] := intsize;
         instr[196]:='srox      '; insp[196] := false; insq[196] := intsize;
         instr[197]:='stox      '; insp[197] := false; insq[197] := 0;
         instr[198]:='indx      '; insp[198] := false; insq[198] := intsize;
         instr[199]:='chkx      '; insp[199] := false; insq[199] := intsize;
         instr[200]:='ordx      '; insp[200] := false; insq[200] := 0;
         instr[201]:='incx      '; insp[201] := false; insq[201] := intsize;
         instr[202]:='decx      '; insp[202] := false; insq[202] := intsize;
         instr[203]:='ckvx      '; insp[203] := false; insq[203] := intsize;
         instr[204]:='retx      '; insp[204] := false; insq[204] := 0;

         { sav (mark) and rst (release) were removed }
         sptable[ 0]:='get       ';     sptable[ 1]:='put       ';
         sptable[ 2]:='---       ';     sptable[ 3]:='rln       ';
         sptable[ 4]:='new       ';     sptable[ 5]:='wln       ';
         sptable[ 6]:='wrs       ';     sptable[ 7]:='eln       ';
         sptable[ 8]:='wri       ';     sptable[ 9]:='wrr       ';
         sptable[10]:='wrc       ';     sptable[11]:='rdi       ';
         sptable[12]:='rdr       ';     sptable[13]:='rdc       ';
         sptable[14]:='sin       ';     sptable[15]:='cos       ';
         sptable[16]:='exp       ';     sptable[17]:='log       ';
         sptable[18]:='sqt       ';     sptable[19]:='atn       ';
         sptable[20]:='---       ';     sptable[21]:='pag       ';
         sptable[22]:='rsf       ';     sptable[23]:='rwf       ';
         sptable[24]:='wrb       ';     sptable[25]:='wrf       ';
         sptable[26]:='dsp       ';     sptable[27]:='wbf       ';
         sptable[28]:='wbi       ';     sptable[29]:='wbr       ';
         sptable[30]:='wbc       ';     sptable[31]:='wbb       ';
         sptable[32]:='rbf       ';     sptable[33]:='rsb       ';
         sptable[34]:='rwb       ';     sptable[35]:='gbf       ';
         sptable[36]:='pbf       ';     sptable[37]:='rib       ';
         sptable[38]:='rcb       ';     sptable[39]:='nwl       ';
         sptable[40]:='dsl       ';     sptable[41]:='eof       ';
         sptable[42]:='efb       ';     sptable[43]:='fbv       ';
         sptable[44]:='fvb       ';     sptable[45]:='wbx       ';
         sptable[46]:='rbr       ';


         pc := begincode;
         cp := maxstr; { set constants pointer to top of storage }
         for i:= 1 to 10 do word[i]:= ' ';
         for i:= 0 to maxlabel do
             with labeltab[i] do begin val:=-1; st:= entered end;
         { initalize file state }
         for i := 1 to maxfil do begin filstate[i] := fclosed; filbuff[i] := false end;

         { !!! remove this next statement for self compile }
#ifndef SELF_COMPILE
  reset(prd);
#endif

         iline := 1; { set 1st line of intermediate }

         gbset := false { global size not set }

   end;(*init*)

   procedure errorl(string: beta); (*error in loading*)
   begin writeln;
      writeln('*** Program load error: [', iline:1, '] ', string);
      goto 1
   end; (*errorl*)

   procedure dmplabs; { dump label table }

   var i: labelrg;

   begin

      writeln;
      writeln('Label table');
      writeln;
      for i := 1 to maxlabel do if labeltab[i].val <> -1 then begin

         write('Label: ', i:5, ' value: ', labeltab[i].val, ' ');
         if labeltab[i].st = entered then writeln('Entered')
         else writeln('Defined')

      end;
      writeln

   end;

   procedure update(x: labelrg); (*when a label definition lx is found*)
      var curr,succ,ad: address; (*resp. current element and successor element
                               of a list of future references*)
          endlist: boolean;
          op: instyp; q : address;  (*instruction register*)
   begin
      if labeltab[x].st=defined then errorl('duplicated label         ')
      else begin
             if labeltab[x].val<>-1 then (*forward reference(s)*)
             begin curr:= labeltab[x].val; endlist:= false;
                while not endlist do begin
                      ad := curr;
                      op := store[ad]; { get instruction }
                      q := getadr(ad);
                      succ:= q; { get target address from that }
                      q:= labelvalue; { place new target address }
                      ad := curr;
                      putadr(ad, q);
                      if succ=-1 then endlist:= true
                                 else curr:= succ
                 end
             end;
             labeltab[x].st := defined;
             labeltab[x].val:= labelvalue;
      end
   end;(*update*)

   procedure getnxt; { get next character }
   begin
      ch := ' ';
      if not eoln(prd) then read(prd,ch)
   end;

   procedure skpspc; { skip spaces }
   begin
     while (ch = ' ') and not eoln(prd) do getnxt
   end;

   procedure getlin; { get next line }
   begin
     readln(prd);
     iline := iline+1 { next intermediate line }
   end;

   procedure assemble; forward;

   procedure generate;(*generate segment of code*)
      var x: integer; (* label number *)
          l: integer;
          again: boolean;
          ch1: char;
          ad: address;
   begin
      again := true;
      while again do
            begin if eof(prd) then errorl('unexpected eof on input  ');
                  getnxt;(* first character of line*)
                  if not (ch in ['!', 'l', 'q', ' ', ':', 'o', 'g','v', 'f']) then
                    errorl('unexpected line start    ');
                  case ch of
                       '!': getlin; { comment }
                       'l': begin read(prd,x);
                                  getnxt;
                                  if ch='=' then read(prd,labelvalue)
                                            else labelvalue:= pc;
                                  update(x); getlin
                            end;
                       'q': begin again := false; getlin end;
                       ' ': begin getnxt;
                                  while not eoln(prd) and (ch = ' ') do getnxt;
                                  if not eoln(prd) and (ch <> ' ') then assemble
                                  else getlin
                            end;
                       ':': begin { source line }
                               read(prd,x); { get source line number }
                               if dosrclin then begin
                                  { pass source line register instruction }
                                  store[pc] := 174; putdef(pc, true); pc := pc+1;
                                  putint(pc, x); pc := pc+intsize
                               end;
                               { skip the rest of the line, which would be the
                                 contents of the source line if included }
                               while not eoln(prd) do
                                  read(prd, c); { get next character }
                               getlin { source line }
                            end;
                       'o': begin { option }
                              getnxt;
                              while not eoln(prd) and (ch = ' ') do getnxt;
                              repeat
                                if not (ch in ['a'..'z']) then
                                  errorl('No valid option found    ');
                                ch1 := ch; getnxt;
                                option[ch1] := ch = '+'; getnxt;
                                case ch1 of
                                  'o': dochkovf := option[ch1];
                                  'e': dodmpins := option[ch1];
                                  'g': dodmplab := option[ch1];
                                  'a': dodmpsto := option[ch1];
                                  'f': dotrcrot := option[ch1];
                                  'm': dotrcins := option[ch1];
                                  'j': dopmd    := option[ch1];
                                  'h': dosrclin := option[ch1];
                                  'k': dotrcsrc := option[ch1];
                                  'w': dodmpspc := option[ch1];
                                  'n': dorecycl := option[ch1];
                                  'p': dochkrpt := option[ch1];
                                  'q': dochkdef := option[ch1];
                                  { these options are free }

                                  { these options are used in pcom.pas }
                                  'b':; 'd':; 'c':; 'l':; 'v':; 'r':; 't':;
                                  'u':; 'x':; 'y':; 'z':; 'i':;
                                  { reserved options }
                                  's':; { ISO 7185 standard }
                                end
                              until not (ch in ['a'..'z']);
                              getlin
                            end;
                       'g': begin read(prd,gbsiz); gbset := true; getlin end;
                       'v': begin { variant logical table }
                              getnxt; skpspc;
                              if ch <> 'l' then
                                errorl('Label format error       ');
                              getnxt; read(prd, x);
                              getnxt;
                              read(prd,l); cp := cp-(l*intsize+intsize+intsize);
                              ad := cp; putint(ad, l); ad := ad+intsize;
                              while not eoln(prd) do begin
                                read(prd,i); putint(ad, i); ad := ad+intsize;
                              end;
                              labelvalue:=cp;
                              update(x);
                              getlin
                            end;
                       'f': begin { faults (errors) }
                              read(prd,i); errsinprg := errsinprg+i; getlin
                            end;
                  end;
            end
   end; (*generate*)

   procedure assemble; (*translate symbolic code into machine code and store*)
      var name :alfa; r :real; s :settype;
          i,x,s1,lb,ub,l:integer; c: char;
          str: packed array [1..stringlgth] of char; { buffer for string constants }
          t: integer; { [sam] temp for compiler bug }

      procedure lookup(x: labelrg); (* search in label table*)
      begin case labeltab[x].st of
                entered: begin q := labeltab[x].val;
                           labeltab[x].val := pc
                         end;
                defined: q:= labeltab[x].val
            end(*case label..*)
      end;(*lookup*)

      procedure labelsearch;
         var x: labelrg;
      begin while (ch<>'l') and not eoln(prd) do read(prd,ch);
            read(prd,x); lookup(x)
      end;(*labelsearch*)

      procedure getname;
      var i: alfainx;
      begin
        if eof(prd) then errorl('Unexpected eof on input  ');
        for i := 1 to maxalfa do word[i] := ' ';
        i := 1; { set 1st character of word }
        if not (ch in ['a'..'z']) then errorl('No operation label       ');
        while ch in ['a'..'z'] do begin
          if i = maxalfa then errorl('Opcode label is too long ');
          word[i] := ch;
          i := i+1; ch := ' ';
          if not eoln(prd) then read(prd,ch); { next character }
        end;
        pack(word,1,name)
      end; (*getname*)

      procedure storeop;
      begin
        if pc+1 > cp then errorl('Program code overflow    ');
        store[pc] := op; putdef(pc, true); pc := pc+1
      end;

      procedure storep;
      begin
        if pc+1 > cp then errorl('Program code overflow    ');
        store[pc] := p; putdef(pc, true); pc := pc+1
      end;

      procedure storeq;
      begin
        if pc+adrsize > cp then errorl('Program code overflow    ');
         putint(pc, q); pc := pc+intsize
      end;

      procedure storeq1;
      begin
        if pc+adrsize > cp then errorl('Program code overflow    ');
         putint(pc, q1); pc := pc+intsize
      end;

   begin  p := 0;  q := 0;  op := 0;
      getname;
      { note this search removes the top instruction from use }
      while (instr[op]<>name) and (op < maxins) do op := op+1;
      if op = maxins then errorl('illegal instruction      ');

      case op of  (* get parameters p,q *)

          (*lod,str,lda,lip*)
          0, 193, 105, 106, 107, 108, 109, 195,
          2, 70, 71, 72, 73, 74,4,120: begin read(prd,p,q); storeop; storep;
                                             storeq
                                       end;

          12,11(*cup,mst*): begin read(prd,p); storeop; storep; labelsearch;
                                  storeq end;

          113(*cip*): begin read(prd,p); storeop; storep end;

          { equm,neqm,geqm,grtm,leqm,lesm take a parameter }
          142, 148, 154, 160, 166, 172,

          (*lao,ixa,mov,dmp,swp*)
          5,16,55,117,118,

          (*ldo,sro,ind,inc,dec,ckv,vbs*)
          1, 194, 196, 198, 65, 66, 67, 68, 69, 3, 75, 76, 77, 78, 79, 9, 85,
          86, 87, 88, 89, 10, 90, 93, 94, 57, 103, 104, 175, 176, 177, 178,
          179, 180, 201, 202, 203, 19: begin read(prd,q); storeop; storeq end;

          (*pck,upk*)
          63, 64: begin read(prd,q); read(prd,q1); storeop; storeq; storeq1 end;

          (*cta,ivt,cvb*)
          191, 91, 92, 96, 192, 8, 21, 22, 27: begin
            read(prd,q); read(prd,q1); storeop; storeq; storeq1; labelsearch;
            storeq
          end;

          (*ujp,fjp,xjp,tjp*)
          23,24,25,119,

          (*ents,ente*)
          13, 173: begin storeop; labelsearch; storeq end;

          (*ipj,lpa*)
          112,114: begin read(prd,p); storeop; storep; labelsearch; storeq end;

          15 (*csp*): begin skpspc; getname;
                           while name<>sptable[q] do
                           begin q := q+1; if q > maxsp then
                                 errorl('std proc/func not found  ')
                           end; storeop;
                           if pc+1 > cp then
                             errorl('Program code overflow    ');
                           store[pc] := q; putdef(pc, true); pc := pc+1
                      end;

          7, 123, 124, 125, 126, 127 (*ldc*): begin case op of  (*get q*)
                           123: begin read(prd,i); storeop;
                                      if pc+intsize > cp then
                                         errorl('Program code overflow    ');
                                      putint(pc, i); pc := pc+intsize
                                end;

                           124: begin read(prd,r);
                                      cp := cp-realsize;
                                      alignd(realal, cp);
                                      if cp <= 0 then
                                         errorl('constant table overflow  ');
                                      putrel(cp, r); q := cp;
                                      storeop; storeq
                                end;

                           125: storeop; (*p,q = 0*)

                           126: begin read(prd,q); storeop;
                                      if pc+1 > cp then
                                        errorl('Program code overflow    ');
                                      putbol(pc, q <> 0); pc := pc+1 end;

                           127: begin
                                  skpspc;
                                  if ch in ['0'..'9'] then begin i := 0;
                                    while ch in ['0'..'9'] do
                                      begin i := i*10+ord(ch)-ord('0'); getnxt end;
                                    c := chr(i);
                                  end else begin
                                    if ch <> '''' then
                                      errorl('illegal character        ');
                                    getnxt;  c := ch;
                                    getnxt;
                                    if ch <> '''' then
                                      errorl('illegal character        ');
                                  end;
                                  storeop;
                                  if pc+1 > cp then
                                    errorl('Program code overflow    ');
                                  putchr(pc, c); pc := pc+1
                                end;
                           7: begin skpspc;
                                   if ch <> '(' then
                                     errorl('ldcs() expected          ');
                                   s := [ ];  getnxt;
                                   while ch<>')' do
                                   begin read(prd,s1); getnxt; s := s + [s1]
                                   end;
                                   cp := cp-setsize;
                                   alignd(setal, cp);
                                   if cp <= 0 then
                                      errorl('constant table overflow  ');
                                   putset(cp, s);
                                   q := cp;
                                   storeop; storeq
                                end
                           end (*case*)
                     end;

           26, 95, 97, 98, 99, 190, 199 (*chk*): begin
                         read(prd,lb,ub);
                         if (op = 95) or (op = 190) then q := lb
                         else
                         begin
                           cp := cp-intsize;
                           alignd(intal, cp);
                           if cp <= 0 then errorl('constant table overflow  ');
                           putint(cp, ub);
                           cp := cp-intsize;
                           alignd(intal, cp);
                           if cp <= 0 then errorl('constant table overflow  ');
                           putint(cp, lb); q := cp
                         end;
                         storeop; storeq
                       end;

           56 (*lca*): begin read(prd,l); skpspc;
                         for i := 1 to stringlgth do str[i] := ' ';
                         if ch <> '''' then errorl('bad string format        ');
                         i := 0;
                         repeat
                           if eoln(prd) then errorl('unterminated string      ');
                           getnxt;
                           c := ch; if (ch = '''') and (prd^ = '''') then begin
                             getnxt; c := ' '
                           end;
                           if c <> '''' then begin
                             if i >= stringlgth then errorl('string overflow          ');
                             str[i+1] := ch; { accumulate string }
                             i := i+1
                           end
                         until c = '''';
                         { place in storage }
                         cp := cp-l;
                         if cp <= 0 then errorl('constant table overflow  ');
                         q := cp;
                         for x := 1 to l do putchr(q+x-1, str[x]);
                         { this should have worked, the for loop is faulty
                           because the calculation for end is done after the i
                           set
                         for i := 0 to i-1 do putchr(q+i, str[i+1]);
                         }
                         storeop; storeq
                       end;

          14, 128, 129, 130, 131, 132, 204, (*ret*)

          { equ,neq,geq,grt,leq,les with no parameter }
          17, 137, 138, 139, 140, 141,
          18, 143, 144, 145, 146, 147,
          149, 150, 151, 152, 153,
          155, 156, 157, 158, 159,
          161, 162, 163, 164, 165,
          167, 168, 169, 170, 171,

          59, 133, 134, 135, 136, 200, (*ord*)

          6, 80, 81, 82, 83, 84, 197, (*sto*)

          { eof,adi,adr,sbi,sbr,sgs,flt,flo,trc,ngi,ngr,sqi,sqr,abi,abr,not,and,
            ior,dif,int,uni,inn,mod,odd,mpi,mpr,dvi,dvr,stp,chr,rnd,rgs,fbv,
            fvb,ctp }
          28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
          48,49,50,51,52,53,54,58,60,62,110,111,
          115,116,100,101,102,

          { dupi, dupa, dupr, dups, dupb, dupc, cks, cke, inv, vbe }
          181,182,183,184,185,186,187,188,189,20: storeop;

          (*ujc must have same length as ujp, so we output a dummy q argument*)
          61 (*ujc*): begin storeop; q := 0; storeq end

      end; (*case*)

      getlin { next intermediate line }

   end; (*assemble*)

   procedure prtrng(a, b: address);
   var i: 1..maxdigh;
   begin
     wrthex(a, maxdigh); write('-');
     if b+1 > a then wrthex(b, maxdigh) else for i := 1 to maxdigh do write('*');
     writeln(' (',b+1-a:maxdigd,')')
   end;

begin (*load*)

   init;
   generate;
   pctop := pc; { save top of code store }
   gbtop := pctop+gbsiz; { set top of globals }
   lsttop := pctop; { save as top of listing }
   pc := 0;
   generate;
   if not gbset then errorl('global space not set     ');
   alignu(stackal, pctop); { align the end of code for globals }
   alignd(heapal, cp); { align the start of cp for stack top }
   gbtop := pctop+gbsiz; { set top of globals }
   alignu(gbsal, gbtop); { align the globals top }

   if dodmpsto then begin { dump storage overview }

      writeln;
      writeln('Storage areas occupied');
      writeln;
      write('Program     '); prtrng(0, pctop-1);
      write('Globals     '); prtrng(pctop, gbtop-1);
      write('Stack/Heap  '); prtrng(gbtop, cp-1);
      write('Constants   '); prtrng(cp, maxstr);
      writeln

   end;
   if dodmpins then dmpins; { Debug: dump instructions from store }
   if dodmplab then dmplabs { Debug: dump label definitions }

end; (*load*)

(*------------------------------------------------------------------------*)

{ runtime handlers }

procedure varenter(s, e: address);
var vp: varptr;
begin
  if varfre <> nil then begin vp := varfre; varfre := vp^.next end
  else new(vp);
  vp^.s := s; vp^.e := e; vp^.next := varlst; varlst := vp
end;

procedure varexit;
var vp: varptr;
begin
  if varlst = nil then errori('VAR block list empty     ');
  vp := varlst; varlst := vp^.next; vp^.next := varfre; varfre := vp
end;

function varlap(s, e: address): boolean;
var vp: varptr; f: boolean;
begin
  vp := varlst; f := false;
  while (vp <> nil) and not f do begin
    f := (vp^.e >= s) and (vp^.s <= e);
    vp := vp^.next
  end;
  varlap := f
end;

procedure withenter(b: address);
var wp: wthptr;
begin
  if wthfre <> nil then begin wp := wthfre; wthfre := wp^.next end
  else new(wp);
  wp^.b := b; wp^.next := wthlst; wthlst := wp;
  wthcnt := wthcnt+1
end;

procedure withexit;
var wp: wthptr;
begin
  if wthlst = nil then errori('With base list empty     ');
  wp := wthlst; wthlst := wp^.next; wp^.next := wthfre; wthfre := wp;
  wthcnt := wthcnt-1
end;

function withsch(b: address): boolean;
var wp: wthptr; f: boolean;
begin
  wp := wthlst; f := false;
  while (wp <> nil) and not f do begin
    f := wp^.b = b;
    wp := wp^.next
  end;
  withsch := f
end;

function base(ld :integer):address;
   var ad :address;
begin ad := mp;
   while ld>0 do begin ad := getadr(ad+marksl); ld := ld-1 end;
   base := ad
end; (*base*)

procedure compare;
(*comparing is only correct if result by comparing integers will be*)
begin
  popadr(a2);
  popadr(a1);
  i := 0; b := true;
  while b and (i<q) do begin
    chkdef(a1+i); chkdef(a2+i);
    if store[a1+i] = store[a2+i] then i := i+1
    else b := false
  end;
  if i = q then i := i-1 { point at last location }
end; (*compare*)

procedure valfil(fa: address); { attach file to file entry }
var i,ff: integer;
begin
   if store[fa] = 0 then begin { no file }
     if fa = pctop+inputoff then ff := inputfn
     else if fa = pctop+outputoff then ff := outputfn
     else if fa = pctop+prdoff then ff := prdfn
     else if fa = pctop+prroff then ff := prrfn
     else begin
       i := 5; { start search after the header files }
       ff := 0;
       while i <= maxfil do begin
         if filstate[i] = fclosed then begin ff := i; i := maxfil end;
         i := i+1
       end;
       if ff = 0 then errori('To many files            ');
     end;
     store[fa] := ff; putdef(fa, true)
   end
end;

procedure valfilwm(fa: address); { validate file write mode }
begin
   valfil(fa); { validate file address }
   if filstate[store[fa]] <> fwrite then errori('File not in write mode   ')
end;

procedure valfilrm(fa: address); { validate file read mode }
begin
   valfil(fa); { validate file address }
   if filstate[store[fa]] <> fread then errori('File not in read mode    ')
end;

procedure getop; { get opcode }

begin

   op := store[pc]; pc := pc+1

end;

procedure getp; { get p parameter }

begin

   p := store[pc]; pc := pc+1

end;

procedure getq; { get q parameter }

begin

   q := getint(pc); pc := pc+intsize

end;

procedure getq1; { get q1 parameter }

begin

   q1 := getint(pc); pc := pc+intsize

end;

procedure getq2; { get q2 parameter }

begin

   q2 := getint(pc); pc := pc+intsize

end;

{

   Blocks in the heap are dead simple. The block begins with a length, including
   the length itself. If the length is positive, the block is free. If negative,
   the block is allocated. This means that AddressOfBLock+abs(lengthOfBlock) is
   address of the next block, and RequestedSize <= LengthOfBLock+adrsize is a
   reasonable test for if a free block fits the requested size, since it will
   never be true of occupied blocks.

}

{ report all space in heap }

procedure repspc;
var l, ad: address;
begin
   writeln;
   writeln('Heap space breakdown');
   writeln;
   ad := gbtop; { index the bottom of heap }
   while ad < np do begin
      l := getadr(ad); { get next block length }
      write('addr: '); wrthex(ad, maxdigh); write(': ', abs(l):6, ': ');
      if l >= 0 then writeln('free') else writeln('alloc');
      ad := ad+abs(l)
   end
end;

{ find free block using length }

procedure fndfre(len: address; var blk: address);
var l, b: address;
begin
  b := 0; { set no block found }
  blk := gbtop; { set to bottom of heap }
  while blk < np do begin { search blocks in heap }
     l := getadr(blk); { get length }
     if (abs(l) < heapal) or (abs(l) > np) then errori('Heap format invalid      ');
     if l >= len+adrsize then begin b := blk; blk := np end { found }
     else blk := blk+abs(l) { go next block }
  end;
  if b > 0 then begin { block was found }
     putadr(b, -l); { allocate block }
     blk := b+adrsize; { set base address }
     if l > len+adrsize+adrsize+resspc then begin
        { If there is enough room for the block, header, and another header,
          then a reserve factor if desired. }
        putadr(b, -(len+adrsize)); { allocate block }
        b := b+len+adrsize; { go to top of allocated block }
        putadr(b, l-(len+adrsize)) { set length of stub space }
     end
  end else blk := 0 { set no block found }
end;

{ coalesce space in heap }

procedure cscspc;
var ad, ad1, l, l1: address;
begin
   { first, colapse all free blocks at the heap top }
   l := 0;
   while (l >= 0) and (np > gbtop) do begin
     { find last entry }
     ad := gbtop;
     while ad < np do begin ad1 := ad; ad := ad+abs(getadr(ad)) end;
     l := getadr(ad1); { get header length }
     if l >= 0 then np := ad1; { release to free space }
   end;
   { now, walk up and collapse adjacent free blocks }
   ad := gbtop; { index bottom }
   while ad < np do begin
      l := getadr(ad); { get header length }
      if l >= 0 then begin { free }
         ad1 := ad+l; { index next block }
         if ad1 < np then begin { not against end }
            l1 := getadr(ad1); { get length next }
            if l1 >=0 then
               putadr(ad, l+l1) { both blocks are free, combine the blocks }
            else ad := ad+l+abs(l1) { skip both blocks }
         end else ad := ad1         { skip to end, done }
      end else ad := ad+abs(l) { this block is not free, skip it }
   end
end;

{ allocate space in heap }

procedure newspc(len: address; var blk: address);
var ad,ad1: address;
begin
  alignu(adrsize, len); { align to units of address }
  fndfre(len, blk); { try finding an existing free block }
  if blk = 0 then begin { allocate from heap top }
     ad := np; { save base of new block }
     np := np+(len+adrsize); { find new heap top }
     ad1 := np; { save address }
     alignu(heapal, np); { align to arena }
     len := len+(np-ad1); { adjust length upwards for alignment }
     if np > sp then errori('Store overflow: dynamic  ');
     putadr(ad, -(len+adrsize)); { allocate block }
     blk := ad+adrsize { index start of block }
  end;
  { clear block and set undefined }
  for ad := blk to blk+len-1 do begin store[ad] := 0; putdef(ad, false) end
end;

{ dispose of space in heap }

procedure dspspc(len, blk: address);
var ad: address;
begin
   len := len; { shut up compiler check }
   if blk = 0 then errori('Dispose uninit pointer   ')
   else if blk = nilval then errori('Dispose nil pointer      ')
   else if (blk < gbtop) or (blk >= np) then errori('Bad pointer value        ');
   ad := blk-adrsize; { index header }
   if getadr(ad) >= 0 then errori('Block already freed      ');
   if dorecycl and not dochkrpt then begin { obey recycling requests }
      putadr(ad, abs(getadr(ad))); { set block free }
      cscspc { coalesce free space }
   end else if dochkrpt then begin { perform special recycle }
      { check can break off top block }
      len := abs(getadr(ad)); { get length }
      if len >= adrsize*2 then putadr(ad+adrsize, abs(getadr(ad))-adrsize);
      { the "marker" is a block with a single address. Since it can't
        hold more than that, it will never be reused }
      putadr(ad, adrsize) { indicate freed but fixed block }
   end
end;

{ check pointer indexes free entry }

function isfree(blk: address): boolean;
begin
   isfree := getadr(blk-adrsize) = adrsize
end;

{ system routine call}

procedure callsp;
   var line: boolean;
       i, j, w, l, f: integer;
       c: char;
       b: boolean;
       ad,ad1: address;
       r: real;
       fn: fileno;
       mn,mx: integer;

   procedure readi(var f: text; var i: integer);

   var s: integer;
       d: integer;

   begin

      s := +1; { set sign }
      { skip leading spaces }
      while (f^ = ' ') and not eof(f) do get(f);
      if eof(f) then errori('End of file              ');
      if not (f^ in ['+', '-', '0'..'9']) then
        errori('Invalid integer format   ');
      if f^ = '+' then get(f)
      else if f^ = '-' then begin get(f); s := -1 end;
      if not (f^ in ['0'..'9']) then errori('Invalid integer format   ');
      i := 0; { clear initial value }
      while (f^ in ['0'..'9']) do begin { parse digit }

         d := ord(f^)-ord('0');
         if (i > maxint div 10) or
            ((i = maxint div 10) and (d > maxint mod 10)) then
           errori('Input value overflows    ');
         i := i*10+d; { add in new digit }
         get(f)

      end;
      i := i*s { place sign }

   end;

   procedure readr(var f: text; var r: real);

   var i: integer; { integer holding }
       e: integer; { exponent }
       d: integer; { digit }
       s: boolean; { sign }

   { find power of ten }

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

      e := 0; { clear exponent }
      s := false; { set sign }
      r := 0.0; { clear result }
      { skip leading spaces }
      while (f^ = ' ') and not eof(f) do get(f);
      if eof(f) then errori('End of file              ');
      { get any sign from number }
      if f^ = '-' then begin get(f); s := true end
      else if f^ = '+' then get(f);
      if not (f^ in ['0'..'9']) then errori('Invalid real format      ');
      while (f^ in ['0'..'9']) do begin { parse digit }

         d := ord(f^)-ord('0');
         r := r*10+d; { add in new digit }
         get(f)

      end;
      if f^ in ['.', 'e', 'E'] then begin { it's a real }

         if f^ = '.' then begin { decimal point }

            get(f); { skip '.' }
            if not (f^ in ['0'..'9']) then errori('Invalid real format      ');
            while (f^ in ['0'..'9']) do begin { parse digit }

               d := ord(f^)-ord('0');
               r := r*10+d; { add in new digit }
               get(f);
               e := e-1 { count off right of decimal }

            end;

         end;
         if f^ in ['e', 'E'] then begin { exponent }

            get(f); { skip 'e' }
            if not (f^ in ['0'..'9', '+', '-']) then errori('Invalid real format      ');
            readi(f, i); { get exponent }
            { find with exponent }
            e := e+i

         end;
         if e < 0 then r := r/pwrten(e) else r := r*pwrten(e)

      end;
      if s then r := -r

   end;

   procedure readc(var f: text; var c: char);
   begin if eof(f) then errori('End of file              ');
         read(f,c);
   end;(*readc*)

   procedure writestr(var f: text; ad: address; w: integer; l: integer);
      var i: integer;
   begin (* l and w are numbers of characters *)
         if w>l then for i:=1 to w-l do write(f,' ')
                else l := w;
         for i := 0 to l-1 do write(f, getchr(ad+i))
   end;(*writestr*)

   procedure getfile(var f: text);
   begin if eof(f) then errori('End of file              ');
         get(f);
   end;(*getfile*)

   procedure putfile(var f: text; var ad: address; fn: fileno);
   begin if not filbuff[fn] then errori('File buffer undefined    ');
         f^:= getchr(ad+fileidsize); put(f);
         filbuff[fn] := false
   end;(*putfile*)

begin (*callsp*)
      if q > maxsp then errori('Invalid std proc/func    ');

      { trace routine executions }
      if dotrcrot then writeln(pc:6, '/', sp:6, '-> ', q:2);

      case q of
           0 (*get*): begin popadr(ad); valfil(ad); fn := store[ad];
                           if varlap(ad+fileidsize, ad+fileidsize) then
                             errori('VAR ref file buf modified');
                           if fn <= prrfn then case fn of
                              inputfn: getfile(input);
                              outputfn: errori('Get on output file       ');
                              prdfn: getfile(prd);
                              prrfn: errori('Get on prr file          ')
                           end else begin
                                if filstate[fn] <> fread then
                                   errori('File not in read mode    ');
                                getfile(filtable[fn])
                           end
                      end;
           1 (*put*): begin popadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                              inputfn: errori('Put on read file         ');
                              outputfn: putfile(output, ad, fn);
                              prdfn: errori('Put on prd file          ');
                              prrfn: putfile(prr, ad, fn)
                           end else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                putfile(filtable[fn], ad, fn)
                           end
                      end;
           { unused placeholder for "release" }
           2 (*rst*): errori('Invalid std proc/func    ');
           3 (*rln*): begin popadr(ad); pshadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                              inputfn: begin
                                 if eof(input) then errori('End of file              ');
                                 readln(input)
                              end;
                              outputfn: errori('Readln on output file    ');
                              prdfn: begin
                                 if eof(prd) then errori('End of file              ');
                                 readln(prd)
                              end;
                              prrfn: errori('Readln on prr file       ')
                           end else begin
                                if filstate[fn] <> fread then
                                   errori('File not in read mode    ');
                                if eof(filtable[fn]) then errori('End of file              ');
                                readln(filtable[fn])
                           end
                      end;
           4 (*new*): begin popadr(ad1); newspc(ad1, ad);
                      (*top of stack gives the length in units of storage *)
                            popadr(ad1); putadr(ad1, ad)
                      end;
           39 (*nwl*): begin popadr(ad1); popint(i); { size of record, size of f const list }
                            newspc(ad1+(i+1)*intsize, ad); { alloc record, size of list, number in list }
                            ad1 := ad+i*intsize; putint(ad1, i+adrsize+1);
                            k := i; { save n tags for later }
                            while k > 0 do
                              begin ad1 := ad1-intsize; popint(j);
                                    putint(ad1, j); k := k-1
                              end;
                            { get pointer to dest var, place base above taglist and
                              list of fixed consts }
                            popadr(ad1); putadr(ad1, ad+(i+1)*intsize)
                      end;
           5 (*wln*): begin popadr(ad); pshadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                              inputfn: errori('Writeln on input file    ');
                              outputfn: writeln(output);
                              prdfn: errori('Writeln on prd file      ');
                              prrfn: writeln(prr)
                           end else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                writeln(filtable[fn])
                           end
                      end;
           6 (*wrs*): begin popint(l); popint(w); popadr(ad1);
                           popadr(ad); pshadr(ad); valfil(ad); fn := store[ad];
                           if w < 1 then errori('Width cannot be < 1      ');
                           if fn <= prrfn then case fn of
                              inputfn: errori('Write on input file      ');
                              outputfn: writestr(output, ad1, w, l);
                              prdfn: errori('Write on prd file        ');
                              prrfn: writestr(prr, ad1, w, l)
                           end else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                writestr(filtable[fn], ad1, w, l)
                           end;
                      end;
          41 (*eof*): begin popadr(ad); valfil(ad); fn := store[ad];
                        if fn <= prrfn then case fn of
                          inputfn: pshint(ord(eof(input)));
                          prdfn: pshint(ord(eof(prd)));
                          outputfn,
                          prrfn: errori('Eof test on output file  ')
                        end else begin
                          if filstate[fn] = fwrite then pshint(ord(true))
                          else if filstate[fn] = fread then
                            pshint(ord(eof(filtable[fn]) and not filbuff[fn]))
                          else errori('File is not open         ')
                        end
                      end;
          42 (*efb*): begin
                        popadr(ad); valfilrm(ad); fn := store[ad];
                        { eof is file eof, and buffer not full }
                        pshint(ord(eof(bfiltable[fn]) and not filbuff[fn]))
                      end;
           7 (*eln*): begin popadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                                 inputfn: begin
                                   if eof(input) then
                                     errori('Eof on file              ');
                                   line:= eoln(input)
                                 end;
                                 outputfn: errori('Eoln output file         ');
                                 prdfn: line:=eoln(prd);
                                 prrfn: errori('Eoln on prr file         ')
                              end
                           else begin
                                if filstate[fn] <> fread then
                                   errori('File not in read mode    ');
                                if eof(filtable[fn]) then
                                     errori('Eof on file              ');
                                line:=eoln(filtable[fn])
                           end;
                           pshint(ord(line))
                      end;
           8 (*wri*): begin popint(w); popint(i); popadr(ad); pshadr(ad);
                            valfil(ad); fn := store[ad];
                            if w < 1 then errori('Width cannot be < 1      ');
                            if fn <= prrfn then case fn of
                                 inputfn: errori('Write on input file      ');
                                 outputfn: write(output, i:w);
                                 prdfn: errori('Write on prd file        ');
                                 prrfn: write(prr, i:w)
                              end
                            else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                write(filtable[fn], i:w)
                            end
                      end;
           9 (*wrr*): begin popint(w); poprel(r); popadr(ad); pshadr(ad);
                            valfil(ad); fn := store[ad];
                            if w < 1 then errori('Width cannot be < 1      ');
                            if fn <= prrfn then case fn of
                                 inputfn: errori('Write on input file      ');
                                 outputfn: write(output, r: w);
                                 prdfn: errori('Write on prd file        ');
                                 prrfn: write(prr, r:w)
                              end
                            else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                write(filtable[fn], r:w)
                            end;
                      end;
           10(*wrc*): begin popint(w); popint(i); c := chr(i); popadr(ad);
                            pshadr(ad); valfil(ad); fn := store[ad];
                            if w < 1 then errori('Width cannot be < 1      ');
                            if fn <= prrfn then case fn of
                                 inputfn: errori('Write on input file      ');
                                 outputfn: write(output, c:w);
                                 prdfn: errori('Write on prd file        ');
                                 prrfn: write(prr, c:w)
                              end
                            else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                write(filtable[fn], c:w)
                            end
                      end;
           11(*rdi*): begin popadr(ad1); popadr(ad); pshadr(ad); valfil(ad);
                            fn := store[ad];
                           if fn <= prrfn then case fn of
                                 inputfn: begin readi(input, i); putint(ad1, i);
                                          end;
                                 outputfn: errori('Read on output file      ');
                                 prdfn: begin readi(prd, i); putint(ad1, i) end;
                                 prrfn: errori('Read on prr file         ')
                              end
                           else begin
                                if filstate[fn] <> fread then
                                   errori('File not in read mode    ');
                                readi(filtable[fn], i);
                                putint(ad1, i)
                           end
                      end;
           37(*rib*): begin popint(mx); popint(mn); popadr(ad1); popadr(ad);
                            pshadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                                 inputfn: begin readi(input, i);
                                   if (i < mn) or (i > mx) then
                                     errori('Value read out of range  ');
                                   putint(ad1, i);
                                 end;
                                 outputfn: errori('Read on output file      ');
                                 prdfn: begin readi(prd, i);
                                   if (i < mn) or (i > mx) then
                                     errori('Value read out of range  ');
                                   putint(ad1, i)
                                 end;
                                 prrfn: errori('Read on prr file         ')
                              end
                           else begin
                                if filstate[fn] <> fread then
                                  errori('File not in read mode    ');
                                readi(filtable[fn], i);
                                if (i < mn) or (i > mx) then
                                  errori('Value read out of range  ');
                                putint(ad1, i)
                           end
                      end;
           12(*rdr*): begin popadr(ad1); popadr(ad); pshadr(ad); valfil(ad);
                            fn := store[ad];
                           if fn <= prrfn then case fn of
                                 inputfn: begin readr(input, r); putrel(ad1, r);
                                          end;
                                 outputfn: errori('Read on output file      ');
                                 prdfn: begin readr(prd, r); putrel(ad1, r) end;
                                 prrfn: errori('Read on prr file         ')
                              end
                           else begin
                                if filstate[fn] <> fread then
                                   errori('File not in read mode    ');
                                readr(filtable[fn], r);
                                putrel(ad1, r)
                           end
                      end;
           13(*rdc*): begin popadr(ad1); popadr(ad); pshadr(ad); valfil(ad);
                            fn := store[ad];
                           if fn <= prrfn then case fn of
                                 inputfn: begin readc(input, c); putchr(ad1, c);
                                          end;
                                 outputfn: errori('Read on output file      ');
                                 prdfn: begin readc(prd, c); putchr(ad1, c);
                                        end;
                                 prrfn: errori('Read on prr file         ')
                              end
                           else begin
                                if filstate[fn] <> fread then
                                   errori('File not in read mode    ');
                                readc(filtable[fn], c);
                                putchr(ad1, c)
                           end
                      end;
           38(*rcb*): begin popint(mx); popint(mn); popadr(ad1); popadr(ad);
                            pshadr(ad); valfil(ad);
                            fn := store[ad];
                           if fn <= prrfn then case fn of
                                 inputfn: begin readc(input, c);
                                   if (ord(c) < mn) or (ord(c) > mx) then
                                     errori('Value read out of range  ');
                                   putchr(ad1, c)
                                 end;
                                 outputfn: errori('Read on output file      ');
                                 prdfn: begin readc(prd, c);
                                   if (ord(c) < mn) or (ord(c) > mx) then
                                     errori('Value read out of range  ');
                                   putchr(ad1, c)
                                 end;
                                 prrfn: errori('Read on prr file         ')
                              end
                           else begin
                                if filstate[fn] <> fread then
                                   errori('File not in read mode    ');
                                readc(filtable[fn], c);
                                if (ord(c) < mn) or (ord(c) > mx) then
                                  errori('Value read out of range  ');
                                putchr(ad1, c)
                           end
                      end;
           14(*sin*): begin poprel(r1); pshrel(sin(r1)) end;
           15(*cos*): begin poprel(r1); pshrel(cos(r1)) end;
           16(*exp*): begin poprel(r1); pshrel(exp(r1)) end;
           17(*log*): begin poprel(r1);
                            if r1 <= 0 then errori('Cannot find ln <= 0      ');
                            pshrel(ln(r1)) end;
           18(*sqt*): begin poprel(r1);
                            if r1 < 0 then errori('Cannot find sqrt < 0     ');
                            pshrel(sqrt(r1)) end;
           19(*atn*): begin poprel(r1); pshrel(arctan(r1)) end;
           { placeholder for "mark" }
           20(*sav*): errori('Invalid std proc/func    ');
           21(*pag*): begin popadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                                inputfn: errori('Page on read file        ');
                                outputfn: page(output);
                                prdfn: errori('Page on prd file         ');
                                prrfn: page(prr)
                              end
                           else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                page(filtable[fn])
                           end
                      end;
           22(*rsf*): begin popadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                                inputfn: errori('Reset on input file      ');
                                outputfn: errori('Reset on output file     ');
                                prdfn: reset(prd);
                                prrfn: errori('Reset on prr file        ')
                              end
                           else begin
                                filstate[fn] := fread;
                                reset(filtable[fn]);
                                filbuff[fn] := false
                           end
                      end;
           23(*rwf*): begin popadr(ad); valfil(ad); fn := store[ad];
                           if fn <= prrfn then case fn of
                                inputfn: errori('Rewrite on input file    ');
                                outputfn: errori('Rewrite on output file   ');
                                prdfn: errori('Rewrite on prd file      ');
                                prrfn: rewrite(prr)
                              end
                           else begin
                                filstate[fn] := fwrite;
                                rewrite(filtable[fn])
                           end
                      end;
           24(*wrb*): begin popint(w); popint(i); b := i <> 0; popadr(ad);
                            pshadr(ad); valfil(ad); fn := store[ad];
                            if w < 1 then errori('Width cannot be < 1      ');
                            if fn <= prrfn then case fn of
                                 inputfn: errori('Write on input file      ');
                                 outputfn: write(output, b:w);
                                 prdfn: errori('Write on prd file        ');
                                 prrfn: write(prr, b:w)
                              end
                            else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                write(filtable[fn], b:w)
                            end
                      end;
           25(*wrf*): begin popint(f); popint(w); poprel(r); popadr(ad); pshadr(ad);
                            valfil(ad); fn := store[ad];
                            if w < 1 then errori('Width cannot be < 1      ');
                            if f < 1 then errori('Fraction cannot be < 1   ');
                            if fn <= prrfn then case fn of
                                 inputfn: errori('Write on input file      ');
                                 outputfn: write(output, r:w:f);
                                 prdfn: errori('Write on prd file        ');
                                 prrfn: write(prr, r:w:f)
                              end
                            else begin
                                if filstate[fn] <> fwrite then
                                   errori('File not in write mode   ');
                                write(filtable[fn], r:w:f)
                            end
                      end;
           26(*dsp*): begin
                           popadr(ad1); popadr(ad);
                           if varlap(ad, ad+ad1-1) then
                             errori('Dispose of VAR ref block ');
                           if withsch(ad) then
                             errori('Dispose of with ref block');
                           dspspc(ad1, ad)
                      end;
           40(*dsl*): begin
                           popadr(ad1); popint(i); { get size of record and n tags }
                           ad := getadr(sp+i*intsize); { get rec addr }
                           { under us is either the number of tags or the length of the block, if it
                             was freed. Either way, it is >= adrsize if not free }
                           if getint(ad-intsize) <= adrsize then
                             errori('Block already freed      ');
                           if i <> getint(ad-intsize)-adrsize-1 then
                             errori('New/dispose tags mismatch');
                           ad := ad-intsize*2; ad2 := sp;
                           { ad = top of tags in dynamic, ad2 = top of tags in
                             stack }
                           k := i;
                           while k > 0 do
                             begin
                               if getint(ad) <> getint(ad2) then
                                 errori('New/dispose tags mismatch');
                               ad := ad-intsize; ad2 := ad2+intsize; k := k-1
                             end;
                           ad := ad+intsize; ad1 := ad1+(i+1)*intsize;
                           if varlap(ad, ad+ad1-1) then
                             errori('Dispose of VAR ref block ');
                           if withsch(ad) then
                             errori('Dispose of with ref block');
                           dspspc(ad1, ad);
                           while i > 0 do begin popint(j); i := i-1 end;
                           popadr(ad)
                      end;
           27(*wbf*): begin popint(l); popadr(ad1); popadr(ad); pshadr(ad);
                           valfilwm(ad); fn := store[ad];
                           for i := 1 to l do begin
                              chkdef(ad1); write(bfiltable[fn], store[ad1]);
                              ad1 := ad1+1
                           end
                      end;
           28(*wbi*): begin popint(i); popadr(ad); pshadr(ad); pshint(i);
                            valfilwm(ad); fn := store[ad];
                            for i := 1 to intsize do
                               write(bfiltable[fn], store[sp+i-1]);
                            popint(i)
                      end;
           45(*wbx*): begin popint(i); popadr(ad); pshadr(ad); pshint(i);
                            valfilwm(ad); fn := store[ad];
                            write(bfiltable[fn], store[sp]);
                            popint(i)
                      end;
           29(*wbr*): begin poprel(r); popadr(ad); pshadr(ad); pshrel(r);
                            valfilwm(ad); fn := store[ad];
                            for i := 1 to realsize do
                               write(bfiltable[fn], store[sp+i-1]);
                            poprel(r)
                      end;
           30(*wbc*): begin popint(i); c := chr(i); popadr(ad); pshadr(ad); pshint(i);
                            valfilwm(ad); fn := store[ad];
                            for i := 1 to charsize do
                               write(bfiltable[fn], store[sp+i-1]);
                            popint(i)
                      end;
           31(*wbb*): begin popint(i); popadr(ad); pshadr(ad); pshint(i);
                            valfilwm(ad); fn := store[ad];
                            for i := 1 to boolsize do
                               write(bfiltable[fn], store[sp+i-1]);
                            popint(i)
                      end;
           32(*rbf*): begin popint(l); popadr(ad1); popadr(ad); pshadr(ad);
                            valfilrm(ad); fn := store[ad];
                            if filbuff[fn] then { buffer data exists }
                            for i := 1 to l do begin
                              store[ad1+i-1] := store[ad+fileidsize+i-1];
                              putdef(ad1+i-1, true)
                            end else begin
                              if eof(bfiltable[fn]) then
                                errori('End of file              ');
                              for i := 1 to l do begin
                                read(bfiltable[fn], store[ad1]);
                                putdef(ad1, true);
                                ad1 := ad1+1
                              end
                            end
                      end;
           33(*rsb*): begin popadr(ad); valfil(ad); fn := store[ad];
                           if filstate[fn] = fclosed then
                             errori('Cannot reset closed file ');
                           filstate[fn] := fread;
                           reset(bfiltable[fn]);
                           filbuff[fn] := false
                      end;
           34(*rwb*): begin popadr(ad); valfil(ad); fn := store[ad];
                           filstate[fn] := fwrite;
                           rewrite(bfiltable[fn]);
                           filbuff[fn] := false
                      end;
           35(*gbf*): begin popint(i); popadr(ad); valfilrm(ad);
                           fn := store[ad];
                           if varlap(ad+fileidsize, ad+fileidsize+i-1) then
                             errori('VAR ref file buf modified');
                           if filbuff[fn] then filbuff[fn] := false
                           else
                             for j := 1 to i do
                                read(bfiltable[fn], store[ad+fileidsize+j-1])
                      end;
           36(*pbf*): begin popint(i); popadr(ad); valfilwm(ad);
                        fn := store[ad];
                        if not filbuff[fn] then
                          errori('File buffer undefined    ');
                        for j := 1 to i do
                          write(bfiltable[fn], store[ad+fileidsize+j-1]);
                        filbuff[fn] := false;
                      end;
           43 (*fbv*): begin popadr(ad); pshadr(ad); valfil(ad);
                          fn := store[ad];
                          if fn = inputfn then putchr(ad+fileidsize, input^)
                          else if fn = prdfn then putchr(ad+fileidsize, prd^)
                          else begin
                            if filstate[fn] = fread then
                            putchr(ad+fileidsize, filtable[fn]^)
                          end
                        end;
           44 (*fvb*): begin popint(i); popadr(ad); pshadr(ad); valfil(ad);
                          fn := store[ad];
                          { load buffer only if in read mode, and buffer is
                            empty }
                          if (filstate[fn] = fread) and not filbuff[fn] then
                            begin
                              for j := 1 to i do begin
                                read(bfiltable[fn], store[ad+fileidsize+j-1]);
                                putdef(ad+fileidsize+j-1, true)
                              end
                            end;
                          filbuff[fn] := true
                        end;
           46(*rbr*): begin popint(mx); popint(mn); popint(l); popadr(ad1);
                            popadr(ad); pshadr(ad);
                            valfilrm(ad); fn := store[ad];
                            if filbuff[fn] then { buffer data exists }
                            for i := 1 to l do begin
                              store[ad1+i-1] := store[ad+fileidsize+i-1];
                              putdef(ad1+i-1, true)
                            end else begin
                              if eof(bfiltable[fn]) then
                                errori('End of file              ');
                              ad := ad1;
                              for i := 1 to l do begin
                                read(bfiltable[fn], store[ad1]);
                                putdef(ad1, true);
                                ad1 := ad1+1
                              end;
                              ad1 := ad
                            end;
                            { only two cases, char and byte, or integer }
                            if l = 1 then i := getbyt(ad1) else i := getint(ad1);
                            if (i < mn) or (i > mx) then
                              errori('Value read out of range  ')
                      end;
      end;(*case q*)
end;(*callsp*)

procedure dmpdsp(mp: address);

begin
  writeln;
  write('Mark @'); wrthex(mp, 8); writeln;
  write('sl: '); wrthex(mp+marksl, 8); write(': '); if getdef(mp+marksl) then wrthex(getadr(mp+marksl), 8) else write('********'); writeln;
  write('dl: '); wrthex(mp+markdl, 8); write(': '); if getdef(mp+markdl) then wrthex(getadr(mp+markdl), 8) else write('********'); writeln;
  write('ep: '); wrthex(mp+markep, 8); write(': '); if getdef(mp+markep) then wrthex(getadr(mp+markep), 8) else write('********'); writeln;
  write('sb: '); wrthex(mp+marksb, 8); write(': '); if getdef(mp+marksb) then wrthex(getadr(mp+marksb), 8) else write('********'); writeln;
  write('et: '); wrthex(mp+market, 8); write(': '); if getdef(mp+market) then wrthex(getadr(mp+market), 8) else write('********'); writeln;
  write('ra: '); wrthex(mp+markra, 8); write(': '); if getdef(mp+markra) then wrthex(getadr(mp+markra), 8) else write('********'); writeln
end;

procedure fndpow(var m: integer; p: integer; var d: integer);
begin
  m := 1; d := 1;
  while m < maxint div p do begin m := m*p; d := d+1 end
end;

begin (* main *)

  { Suppress unreferenced errors. }
  if adral = 0 then;
  if adral = 0 then;
  if boolal = 0 then;
  if charmax = 0 then;
  if charal = 0 then;
  if codemax = 0 then;
  if filesize = 0 then;
  if fileal = 0 then;
  if intdig = 0 then;
  if maxresult = 0 then;
  if ordminchar = 0 then;
  if ordmaxchar = 0 then;
  if maxexp = 0 then;
  if stackelsize = 0 then;
  if filres = 0 then;
  if ujplen = 0 then;
  if false then dmpdsp(0);

  write('P5 Pascal interpreter vs. ', majorver:1, '.', minorver:1);
  if experiment then write('.x');
  writeln;
  writeln;

  { !!! remove this next statement for self compile }
#ifndef SELF_COMPILE
  rewrite(prr);
#endif

  { construct bit equivalence table }
  i := 1;
  for bai := 0 to 7 do begin bitmsk[bai] := i; i := i*2 end;

  for sdi := 0 to maxdef do storedef[sdi] := 0; { clear storage defined flags }

  { debug/check flags }
  dochkovf    := true;  { check arithmetic overflow }
  dodmpins    := false; { dump instructions after assembly }
  dodmplab    := false; { dump label definitions }
  dodmpsto    := false; { dump storage area specs }
  dotrcrot    := false; { trace routine executions }
  dotrcins    := false; { trace instruction executions }
  dopmd       := false; { perform post-mortem dump on error }
  dosrclin    := true;  { add source line sets to code }
  dotrcsrc    := false; { trace source line executions }
  dodmpspc    := false; { dump heap space after execution }
  dorecycl    := true;  { obey heap space recycle requests }
  dochkrpt    := false; { check reuse of freed entry }
  dochkdef    := true;  { check undefined accesses }

  errsinprg := 0; { set no source errors }
  varlst := nil; { set no VAR block entries }
  varfre := nil;
  wthlst := nil; { set no with block entries }
  wthcnt := 0;
  wthfre := nil;
  fndpow(maxpow16, 16, hexdig);

  writeln('Assembling/loading program');
  load; (* assembles and stores code *)

  { check and abort if source errors: this indicates a bad intermediate }
  if errsinprg > 0 then begin
    writeln;
    writeln('*** Source program contains errors: ', errsinprg:1);
    goto 1
  end;

  pc := 0; sp := cp; np := gbtop; mp := cp; ep := 5; srclin := 0;

  { clear globals }
  for ad := pctop to gbtop-1 do begin store[ad] := 0; putdef(ad, false) end;

  interpreting := true;

  writeln('Running program');
  writeln;
  while interpreting do
  begin

    if pc >= pctop then errori('pc out of range          ');
    { fetch instruction from byte store }
    pcs := pc; { save starting pc }
    getop;

    (*execute*)

    { trace executed instructions }
    if dotrcins then begin

       wrthex(pcs, maxdigh);
       write('/');
       wrthex(sp, maxdigh);
       lstins(pcs);
       writeln

    end;
    case op of

          0   (*lodi*): begin getp; getq; pshint(getint(base(p) + q)) end;
          193 (*lodx*): begin getp; getq; pshint(getbyt(base(p) + q)) end;
          105 (*loda*): begin getp; getq; pshadr(getadr(base(p) + q)) end;
          106 (*lodr*): begin getp; getq; pshrel(getrel(base(p) + q)) end;
          107 (*lods*): begin getp; getq; getset(base(p) + q, s1); pshset(s1) end;
          108 (*lodb*): begin getp; getq; pshint(ord(getbol(base(p) + q))) end;
          109 (*lodc*): begin getp; getq; pshint(ord(getchr(base(p) + q))) end;

          1   (*ldoi*): begin getq; pshint(getint(pctop+q)) end;
          194 (*ldox*): begin getq; pshint(getbyt(pctop+q)) end;
          65  (*ldoa*): begin getq; pshadr(getadr(pctop+q)) end;
          66  (*ldor*): begin getq; pshrel(getrel(pctop+q)) end;
          67  (*ldos*): begin getq; getset(pctop+q, s1); pshset(s1) end;
          68  (*ldob*): begin getq; pshint(ord(getbol(pctop+q))) end;
          69  (*ldoc*): begin getq; pshint(ord(getchr(pctop+q))) end;

          2   (*stri*): begin getp; getq; popint(i); putint(base(p)+q, i) end;
          195 (*strx*): begin getp; getq; popint(i); putbyt(base(p)+q, i) end;
          70  (*stra*): begin getp; getq; popadr(ad); putadr(base(p)+q, ad) end;
          71  (*strr*): begin getp; getq; poprel(r1); putrel(base(p)+q, r1) end;
          72  (*strs*): begin getp; getq; popset(s1); putset(base(p)+q, s1) end;
          73  (*strb*): begin getp; getq; popint(i1); b1 := i1 <> 0;
                              putbol(base(p)+q, b1) end;
          74  (*strc*): begin getp; getq; popint(i1); c1 := chr(i1);
                              putchr(base(p)+q, c1) end;

          3   (*sroi*): begin getq; popint(i); putint(pctop+q, i) end;
          196 (*srox*): begin getq; popint(i); putbyt(pctop+q, i) end;
          75  (*sroa*): begin getq; popadr(ad); putadr(pctop+q, ad) end;
          76  (*sror*): begin getq; poprel(r1); putrel(pctop+q, r1) end;
          77  (*sros*): begin getq; popset(s1); putset(pctop+q, s1) end;
          78  (*srob*): begin getq; popint(i1); b1 := i1 <> 0;
                              putbol(pctop+q, b1) end;
          79  (*sroc*): begin getq; popint(i1); c1 := chr(i1);
                              putchr(pctop+q, c1) end;

          4 (*lda*): begin getp; getq; pshadr(base(p)+q) end;
          5 (*lao*): begin getq; pshadr(pctop+q) end;

          6   (*stoi*): begin popint(i); popadr(ad); putint(ad, i) end;
          197 (*stox*): begin popint(i); popadr(ad); putbyt(ad, i) end;
          80  (*stoa*): begin popadr(ad1); popadr(ad); putadr(ad, ad1) end;
          81  (*stor*): begin poprel(r1); popadr(ad); putrel(ad, r1) end;
          82  (*stos*): begin popset(s1); popadr(ad); putset(ad, s1) end;
          83  (*stob*): begin popint(i1); b1 := i1 <> 0; popadr(ad);
                              putbol(ad, b1) end;
          84  (*stoc*): begin popint(i1); c1 := chr(i1); popadr(ad);
                              putchr(ad, c1) end;

          127 (*ldcc*): begin pshint(ord(getchr(pc))); pc := pc+1 end;
          126 (*ldcb*): begin pshint(ord(getbol(pc))); pc := pc+1 end;
          123 (*ldci*): begin i := getint(pc); pc := pc+intsize; pshint(i) end;
          125 (*ldcn*): pshadr(nilval) (* load nil *) ;
          124 (*ldcr*): begin getq; pshrel(getrel(q)) end;
          7   (*ldcs*): begin getq; getset(q, s1); pshset(s1) end;

          9   (*indi*): begin getq; popadr(ad); pshint(getint(ad+q)) end;
          198 (*indx*): begin getq; popadr(ad); pshint(getbyt(ad+q)) end;
          85  (*inda*): begin getq; popadr(ad); ad1 := getadr(ad+q);
                              pshadr(ad1) end;
          86  (*indr*): begin getq; popadr(ad); pshrel(getrel(ad+q)) end;
          87  (*inds*): begin getq; popadr(ad); getset(ad+q, s1); pshset(s1) end;
          88  (*indb*): begin getq; popadr(ad); pshint(ord(getbol(ad+q))) end;
          89  (*indc*): begin getq; popadr(ad); pshint(ord(getchr(ad+q))) end;

          93  (*incb*),
          94  (*incc*),
          201 (*incx*),
          10  (*inci*): begin getq; popint(i1);
                         if dochkovf then if (i1<0) = (q<0) then
                            if maxint-abs(i1) < abs(q) then
                              errori('Arithmetic overflow      ');
                         pshint(i1+q)
                         end;
          90 (*inca*): begin getq; popadr(a1); pshadr(a1+q) end;

          11 (*mst*): begin (*p=level of calling procedure minus level of called
                              procedure + 1;  set dl and sl, decrement sp*)
                       (* then length of this element is
                          max(intsize,realsize,boolsize,charsize,ptrsize *)
                       getp; getq;
                       { allocate function result as zeros }
                       for j := 1 to q div intsize do pshint(0);
                       { invalidate }
                       for j := 1 to q do putdef(sp+j-1, false);
                       ad := sp; { save mark base }
                       { allocate mark as zeros }
                       for j := 1 to marksize div intsize do pshint(0);

                       { mark function result invalid }
                       { for j := 0 to maxresult-1 do putdef(ad+markfv+j, false); }

                       putadr(ad+marksl, base(p)); { sl }
                       (* the length of this element is ptrsize *)
                       putadr(ad+markdl, mp); { dl }
                       (* idem *)
                       putadr(ad+markep, ep) { ep }
                      end;

          12 (*cup*): begin (*p=no of locations for parameters, q=entry point*)
                       getp; getq;
                       mp := sp+(p+marksize); { mp to base of mark }

                       putadr(mp+markra, pc); { place ra }
                       pc := q
                      end;

          13 (*ents*): begin getq; ad := mp+q; (*q = length of dataseg*)
                          if ad <= np then begin
                            errori('Store overflow: ents     ');
                          end;
                          { clear allocated memory and set undefined }
                          while sp > ad do
                            begin sp := sp-1; store[sp] := 0; putdef(sp, false);
                            end;
                          putadr(mp+marksb, sp) { set bottom of stack }
                       end;

          173 (*ente*): begin getq; ep := sp+q;
                          if ep <= np then errori('Store overflow: ente     ');
                          putadr(mp+market, ep); { place current ep }
                          putint(ad+markwb, wthcnt) { with base count }
                        end;
                        (*q = max space required on stack*)

          { For characters and booleans, need to clean 8 bit results because
            only the lower 8 bits were stored to. }
          130 (*retc*): begin
                          { set stack below function result }
                          sp := mp;
                          putint(sp, ord(getchr(sp)));
                          pc := getadr(mp+markra);
                          ep := getadr(mp+markep);
                          mp := getadr(mp+markdl)
                        end;
          131 (*retb*): begin
                          { set stack below function result }
                          sp := mp;
                          putint(sp, ord(getbol(sp)));
                          pc := getadr(mp+markra);
                          ep := getadr(mp+markep);
                          mp := getadr(mp+markdl)
                        end;
          14  (*retp*),
          128 (*reti*),
          204 (*retx*),
          129 (*retr*),
          132 (*reta*): begin
                          { set stack below function result, if any }
                          sp := mp;
                          pc := getadr(mp+markra);
                          ep := getadr(mp+markep);
                          mp := getadr(mp+markdl)
                        end;

          15 (*csp*): begin q := store[pc]; pc := pc+1; callsp end;

          16 (*ixa*): begin getq; popint(i); popadr(a1); pshadr(q*i+a1) end;

          17  { equa }: begin popadr(a2); popadr(a1); pshint(ord(a1=a2)) end;
          139 { equb },
          141 { equc },
          137 { equi }: begin popint(i2); popint(i1); pshint(ord(i1=i2)) end;
          138 { equr }: begin poprel(r2); poprel(r1); pshint(ord(r1=r2)) end;
          140 { equs }: begin popset(s2); popset(s1); pshint(ord(s1=s2)) end;
          142 { equm }: begin getq; compare; pshint(ord(b)) end;

          18  { neqa }: begin popadr(a2); popadr(a1); pshint(ord(a1<>a2)) end;
          145 { neqb },
          147 { neqc },
          143 { neqi }: begin popint(i2); popint(i1); pshint(ord(i1<>i2)) end;
          144 { neqr }: begin poprel(r2); poprel(r1); pshint(ord(r1<>r2)) end;
          146 { neqs }: begin popset(s2); popset(s1); pshint(ord(s1<>s2)) end;
          148 { neqm }: begin getq; compare; pshint(ord(not b)) end;

          151 { geqb },
          153 { geqc },
          149 { geqi }: begin popint(i2); popint(i1); pshint(ord(i1>=i2)) end;
          150 { geqr }: begin poprel(r2); poprel(r1); pshint(ord(r1>=r2)) end;
          152 { geqs }: begin popset(s2); popset(s1); pshint(ord(s1>=s2)) end;
          154 { geqm }: begin getq; compare;
                              pshint(ord(b or (store[a1+i] >= store[a2+i])))
                        end;

          157 { grtb },
          159 { grtc },
          155 { grti }: begin popint(i2); popint(i1); pshint(ord(i1>i2)) end;
          156 { grtr }: begin poprel(r2); poprel(r1); pshint(ord(r1>r2)) end;
          158 { grts }: errori('set inclusion            ');
          160 { grtm }: begin getq; compare;
                              pshint(ord(not b and (store[a1+i] > store[a2+i])))
                        end;

          163 { leqb },
          165 { leqc },
          161 { leqi }: begin popint(i2); popint(i1); pshint(ord(i1<=i2)) end;
          162 { leqr }: begin poprel(r2); poprel(r1); pshint(ord(r1<=r2)) end;
          164 { leqs }: begin popset(s2); popset(s1); pshint(ord(s1<=s2)) end;
          166 { leqm }: begin getq; compare;
                              pshint(ord(b or (store[a1+i] <= store[a2+i])))
                        end;

          169 { lesb },
          171 { lesc },
          167 { lesi }: begin popint(i2); popint(i1); pshint(ord(i1<i2)) end;
          168 { lesr }: begin poprel(r2); poprel(r1); pshint(ord(r1<r2)) end;
          170 { less }: errori('Set inclusion            ');
          172 { lesm }: begin getq; compare;
                              pshint(ord(not b and (store[a1+i] < store[a2+i])))
                        end;

          23 (*ujp*): begin getq; pc := q end;
          24 (*fjp*): begin getq; popint(i); if i = 0 then pc := q end;
          25 (*xjp*): begin getq; popint(i1); pc := i1*ujplen+q end;

          95 (*chka*),
          190 (*ckla*): begin getq; popadr(a1); pshadr(a1);
                             {     0 = assign pointer including nil
                               Not 0 = assign pointer from heap address }
                             if a1 = 0 then
                                { if zero, but not nil, it's never been assigned }
                                errori('Uninitialized pointer    ')
                             else if (q <> 0) and (a1 = nilval) then
                                { q <> 0 means deref, and it was nil
                                  (which is not zero) }
                                errori('Dereference of nil ptr   ')
                             else if ((a1 < gbtop) or (a1 >= np)) and
                                     (a1 <> nilval) then
                                { outside heap space (which could have
                                  contracted!) }
                                errori('Bad pointer value        ')
                             else if dochkrpt and (a1 <> nilval) then begin
                               { perform use of freed space check }
                               if isfree(a1) then
                                 { attempt to dereference or assign a freed
                                   block }
                                 errori('Ptr used after dispose op')
                             end
                       end;
          97 (*chks*): begin getq; popset(s1); pshset(s1);
                         for j := setlow to getint(q)-1 do
                           if j in s1 then errori('Set element out of range ');
                         for j := getint(q+intsize)+1 to sethigh do
                           if j in s1 then errori('Set element out of range ');
                       end;
          98 (*chkb*),
          99 (*chkc*),
          199 { chkx },
          26 (*chki*): begin getq; popint(i1); pshint(i1);
                        if (i1 < getint(q)) or (i1 > getint(q+intsize)) then
                        errori('Value out of range       ')
                      end;

          187 { cks }: pshint(0);
          175 { ckvi },
          203 { ckvx },
          179 { ckvb },
          180 { ckvc }: begin getq; popint(i2); popint(i1);
                          pshint(i1); pshint(ord((i1 = q) or (i2 <> 0)));
                        end;
          188 { cke }: begin popint(i2); popint(i1);
                          if i2 = 0 then errori('Variant not active       ')
                        end;

          { all the dups are defined, but not all used }
          185 { dupb },
          186 { dupc },
          181 { dupi }: begin popint(i1); pshint(i1); pshint(i1) end;
          182 { dupa }: begin popadr(a1); pshadr(a1); pshadr(a1) end;
          183 { dupr }: begin poprel(r1); pshrel(r1); pshrel(r1) end;
          184 { dups }: begin popset(s1); pshset(s1); pshset(s1) end;

          189 { inv }: begin popadr(ad); putdef(ad, false) end;

          28 (*adi*): begin popint(i2); popint(i1);
                        if dochkovf then if (i1<0) = (i2<0) then
                          if maxint-abs(i1) < abs(i2) then
                            errori('Arithmetic overflow      ');
                      pshint(i1+i2) end;
          29 (*adr*): begin poprel(r2); poprel(r1); pshrel(r1+r2) end;
          30 (*sbi*): begin popint(i2); popint(i1);
                        if dochkovf then if (i1<0) <> (i2<0) then
                          if maxint-abs(i1) < abs(i2) then
                            errori('Arithmetic overflow      ');
                      pshint(i1-i2) end;
          31 (*sbr*): begin poprel(r2); poprel(r1); pshrel(r1-r2) end;
          32 (*sgs*): begin popint(i1);
                        if (i1 < setlow) or (i1 > sethigh) then
                          errori('Set element out of range ');
                        pshset([i1])
                      end;
          33 (*flt*): begin popint(i1); pshrel(i1) end;

          { note that flo implies the tos is float as well }
          34 (*flo*): begin poprel(r1); popint(i1); pshrel(i1); pshrel(r1) end;

          35 (*trc*): begin poprel(r1);
                        if dochkovf then if (r1 < -maxint) or (r1 > maxint) then
                          errori('Real argument to large   ');
                        pshint(trunc(r1)) end;
          36 (*ngi*): begin popint(i1); pshint(-i1) end;
          37 (*ngr*): begin poprel(r1); pshrel(-r1) end;
          38 (*sqi*): begin popint(i1);
                      if dochkovf then if i1 <> 0 then
                        if abs(i1) > maxint div abs(i1) then
                          errori('Arithmetic overflow      ');
                      pshint(sqr(i1)) end;
          39 (*sqr*): begin poprel(r1); pshrel(sqr(r1)) end;
          40 (*abi*): begin popint(i1); pshint(abs(i1)) end;
          41 (*abr*): begin poprel(r1); pshrel(abs(r1)) end;
          42 (*not*): begin popint(i1); b1 := i1 <> 0; pshint(ord(not b1)) end;
          43 (*and*): begin popint(i2); b2 := i2 <> 0;
                            popint(i1); b1 := i1 <> 0;
                            pshint(ord(b1 and b2)) end;
          44 (*ior*): begin popint(i2); b2 := i2 <> 0;
                            popint(i1); b1 := i1 <> 0;
                            pshint(ord(b1 or b2)) end;
          45 (*dif*): begin popset(s2); popset(s1); pshset(s1-s2) end;
          46 (*int*): begin popset(s2); popset(s1); pshset(s1*s2) end;
          47 (*uni*): begin popset(s2); popset(s1); pshset(s1+s2) end;
          48 (*inn*): begin popset(s1); popint(i1); pshint(ord(i1 in s1)) end;
          49 (*mod*): begin popint(i2); popint(i1);
                        if dochkovf then if i2 <= 0 then errori('Invalid divisor in mod   ');
                        pshint(i1 mod i2) end;
          50 (*odd*): begin popint(i1); pshint(ord(odd(i1))) end;
          51 (*mpi*): begin popint(i2); popint(i1);
                        if dochkovf then if (i1 <> 0) and (i2 <> 0) then
                          if abs(i1) > maxint div abs(i2) then
                            errori('Arithmetic overflow      ');
                        pshint(i1*i2) end;
          52 (*mpr*): begin poprel(r2); poprel(r1); pshrel(r1*r2) end;
          53 (*dvi*): begin popint(i2); popint(i1);
                            if dochkovf then if i2 = 0 then errori('Zero divide              ');
                            pshint(i1 div i2) end;
          54 (*dvr*): begin poprel(r2); poprel(r1);
                            if dochkovf then if r2 = 0.0 then errori('Zero divide              ');
                            pshrel(r1/r2) end;
          55 (*mov*): begin getq; popint(i2); popint(i1);
                       for i3 := 0 to q-1 do
                         begin store[i1+i3] := store[i2+i3];
                               putdef(i1+i3, getdef(i2+i3)) end;
                       (* q is a number of storage units *)
                      end;
          56 (*lca*): begin getq; pshadr(q) end;

          103 (*decb*),
          104 (*decc*),
          202 (*decx*),
          57  (*deci*): begin getq; popint(i1);
                          if dochkovf then if (i1<0) <> (q<0) then
                            if maxint-abs(i1) < abs(q) then
                              errori('Arithmetic overflow      ');
                          pshint(i1-q) end;

          58 (*stp*): interpreting := false;

          134 (*ordb*),
          136 (*ordc*),
          200 (*ordx*),
          59  (*ordi*): ; { ord is a no-op }

          60 (*chr*): ; { chr is a no-op }

          61 (*ujc*): errori('Case - error             ');
          62 (*rnd*): begin poprel(r1);
                        if dochkovf then if (r1 < -(maxint+0.5)) or (r1 > maxint+0.5) then
                          errori('Real argument to large   ');
                        pshint(round(r1)) end;
          63 (*pck*): begin getq; getq1; popadr(a3); popadr(a2); popadr(a1);
                       if a2+q > q1 then errori('Pack elements out of bnds');
                       for i4 := 0 to q-1 do begin chkdef(a1+a2);
                          store[a3+i4] := store[a1+a2];
                          putdef(a3+i4, getdef(a1+a2));
                          a2 := a2+1
                       end
                     end;
          64 (*upk*): begin getq; getq1; popadr(a3); popadr(a2); popadr(a1);
                       if a3+q > q1 then errori('Unpack elem out of bnds  ');
                       for i4 := 0 to q-1 do begin chkdef(a1+i4);
                          store[a2+a3] := store[a1+i4];
                          putdef(a2+a3, getdef(a1+i4));
                          a3 := a3+1
                       end
                     end;

          110 (*rgs*): begin popint(i2); popint(i1);
                         if (i1 < setlow) or (i1 > sethigh) or
                            (i2 < setlow) or (i2 > sethigh) then
                           errori('Set element out of range ');
                         pshset([i1..i2])
                       end;
          112 (*ipj*): begin getp; getq; pc := q;
                       mp := base(p); { index the mark to restore }
                       { restore marks until we reach the destination level }
                       sp := getadr(mp+marksb); { get the stack bottom }
                       ep := getadr(mp+market); { get the mark ep }
                       i := getint(mp+markwb); { get the with base count }
                       while wthcnt <> i do withexit { clean the with stack }
                     end;
          113 (*cip*): begin getp; popadr(ad);
                      mp := sp+(p+marksize);
                      { replace next link mp with the one for the target }
                      putadr(mp+marksl, getadr(ad+1*ptrsize));
                      putadr(mp+markra, pc);
                      pc := getadr(ad)
                    end;
          114 (*lpa*): begin getp; getq; { place procedure address on stack }
                      pshadr(base(p));
                      pshadr(q)
                    end;
          117 (*dmp*): begin getq; sp := sp+q end; { remove top of stack }

          118 (*swp*): begin getq; swpstk(q) end;

          119 (*tjp*): begin getq; popint(i); if i <> 0 then pc := q end;

          120 (*lip*): begin getp; getq; ad := base(p) + q;
                         ad1 := getadr(ad); ad2 := getadr(ad+1*ptrsize);
                         pshadr(ad2); pshadr(ad1);
                       end;

          191 (*cta*): begin getq; getq1; getq2; popint(i); popadr(ad); pshadr(ad);
                             pshint(i); ad := ad-q-intsize; ad1 := getadr(ad);
                             if ad1 < intsize then
                               errori('System error             ');
                             ad1 := ad1-adrsize-1;
                             ad := ad-ad1*intsize;
                             i := i-getint(q2+intsize);
                             if ad1 >= q1 then begin
                               if (i < 0) or (i >= getint(q2)) then
                                 errori('Value out of range       ');
                               if getadr(ad+(q1-1)*intsize) <>
                                  getint(q2+(i+2)*intsize) then
                                 errori('Change to alloc tagfield ');
                             end
                       end;

          192 (*ivti*),
          91  (*ivtx*),
          92  (*ivtb*),
          96  (*ivtc*): begin getq; getq1; getq2; popint(i); popadr(ad);
                          pshadr(ad); pshint(i);
                          i := i-getint(q2+intsize);
                          if (i < 0) or (i >= getint(q2)) then
                            errori('Value out of range       ');
                          if dochkdef then begin
                              b := getdef(ad);
                            if b then begin
                              if op = 192 then j := getint(ad) else j := getbyt(ad);
                              j := j-getint(q2+intsize);
                              b := getint(q2+(i+2)*intsize) <>
                                   getint(q2+(j+2)*intsize);
                            end;
                              if b then begin
                                ad := ad+q;
                                for j := 1 to q1 do
                                  begin putdef(ad, false); ad := ad+1 end
                              end
                            end
                      end;

          174 (*mrkl*): begin getq; srclin := q;
                              if dotrcsrc then
                                writeln('Source line executed: ', q:1)
                        end;
          19 (*vbs*): begin getq; popadr(ad); varenter(ad, ad+q-1) end;
          20 (*vbe*): varexit;

          8  (*cvbi*),
          21 (*cvbx*),
          22 (*cvbb*),
          27 (*cvbc*): begin getq; getq1; getq2; popint(i); popadr(ad);
                          pshadr(ad); pshint(i);
                          i := i-getint(q2+intsize);
                          if (i < 0) or (i >= getint(q2)) then
                            errori('Value out of range       ');
                          b := getdef(ad);
                          if b then begin
                            if op = 8 then j := getint(ad) else j := getbyt(ad);
                            j := j-getint(q2+intsize);
                            b := getint(q2+(i+2)*intsize) <>
                                 getint(q2+(j+2)*intsize)
                          end;
                          if b then begin
                            ad := ad+q;
                            if varlap(ad, ad+q1-1) then
                              errori('Change to VAR ref variant');
                          end
                        end;
          100 (*ctp*): begin popadr(ad); pshadr(ad); ad := ad-intsize; ad1 := getadr(ad);
                         if ad1 < intsize then
                           errori('System error             ');
                         ad1 := ad1-adrsize-1;
                         if ad1 <> 0 then errori('Access tag allocated rec ')
                       end;
          101 (*wbs*): begin popadr(ad); pshadr(ad); withenter(ad) end;
          102 (*wbe*): withexit;

          { illegal instructions }
          111, 115, 116, 121, 122, 133, 135, 176, 177, 178, 205, 206, 207, 208,
          209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222,
          223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236,
          237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250,
          251, 252, 253, 254, 255: errori('Illegal instruction      ');

    end
  end; (*while interpreting*)

  if varlst <> nil then errori('VAR block imbalance      ');
  if wthlst <> nil then errori('With block imbalance     ');

  { perform heap dump if requested }
  if dodmpspc then repspc;

  1 : { abort run }

  writeln;
  writeln('program complete');

end.
