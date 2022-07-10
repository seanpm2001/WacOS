(*$c+,t-,d-,l-*)
(******************************************************************************
*                                                                             *
*                           TEST SUITE FOR P4                                 *
*                                                                             *
*            Copyright (C) 1994 S. A. Moore - All rights reserved             *
*                                                                             *
* This program attempts to use and display the results of each feature of     *
* P4 pascal. It is a "positive" test in that it should compile and run  error *
* free, and thus does not check error conditions/detection.                   *
*                                                                             *
* Each test is labeled and numbered, and the expected result also output, so  *
* that the output can be self evidently hand checked.                         *
*                                                                             *
* The output can be redirected to a printer or a file to facillitate such     *
* checking.                                                                   *
*                                                                             *
* The output can also be automatically checked by comparing a known good file *
* to the generated file. To this end, we have regularized the output,         *
* specifying all output field widths that are normally compiler dependent.    *
*                                                                             *
* Only the following factors exist that are allowed to be compiler dependent, *
* but could cause a miscompare of the output:                                 *
*                                                                             *
*    1. The case of output booleans. We have choosen a standard format of     *
*       LOWER case for such output. Note that some compilers can choose the   *
*       case of such output by option.                                        *
*                                                                             *
* Because of this, it may be required to return to hand checking when         *
* encountering a differing compiler system.                                   *
*                                                                             *
* Notes:                                                                      *
*                                                                             *
* The following sections need to be completed:                                *
*                                                                             *
* 1. Arrays, records and pointers containing files.                           *
*                                                                             *
* 3. Pointer variables, array variables, and other complex accesses need to   *
* subjected to the same extentive tests that base variables are.              *
*                                                                             *
* 4. Need a test for access to locals of a surrounding procedure. This tests  *
* access to a procedure that is local, but not in the same scope.             *
*                                                                             *
******************************************************************************)
        
program suite(output);

label 1, 3;

const tcnst = 768;
      scst = 'this is a string';
      ccst = 'v';
      tsncst = -52;
      rcnst = 43.33;
      rscst = -84.22;
      testfile = true;

type string10 = packed array [1..10] of char;
     enum  = (one, two, three, four, five, six, seven, eight, nine, ten);
     esub  = three..six;
     subr  = 10..20;
     arri  = array [1..10] of integer;
     arrim = array [1..2, 1..2] of array [1..2, 1..2, 1..2, 1..2] of integer;
     cset  = set of char;
     iptr  = ^integer;
     recs  = record

               a: integer;
               b: char

            end;
     rec = record

              i:   integer;
              b:   boolean;
              c:   char;
              e:   enum;
              es:  esub;
              s:   subr;
              r:   real;
              st:  string10;
              a:   arri;
              rc:  recs;
              stc: cset;
              p:   iptr

           end;
     prec = packed record

              i:   integer;
              b:   boolean;
              c:   char;
              e:   enum;
              es:  esub;
              s:   subr;
              r:   real;
              st:  string10;
              a:   arri;
              rc:  recs;
              stc: cset;
              p:   iptr

           end;
     recv = record

               a: integer;
               b: char;
               case c: boolean of

                  false: (d: string10);
                  true:  (e: enum)

               (* end *)

            end; 
     arrr = array [1..10] of recs;
     vart = (vti, vtb, vtc, vte, vtes, vts, vtr, vtst, vta, vtrc, vtstc, vtp);

var i, x, y, z, q, n, t : integer;
    srx, sry, srz: 0..100;
    sras, srbs, srcs, srds, sres: -100..100;
    a : array [1..10] of integer;
    r : record

           rx: integer;
           rc: char;
           ry: integer;
           rb: boolean;
           rs: packed array [1..10] of char;

        end;
    da:    array [1..10, 1..10] of integer;
    sa, sb, sc : packed array [1..10] of char;
    ca, cb, cc : char;
    car :  array ['a'..'z'] of integer;
    sar:   array [1..10] of packed array [1..10] of char;
    ba, bb, bc : boolean;
    sva, svb, svc : (mon, tue, wed, thur, fri, sat, sun);
    s:     string10;
    as, bs, cs, ds, es : integer;
    ra, rb, rc, rd, re: real;
    sta,   stb, stc, std: set of 1..100;
    ste:   set of 1..10;
    stf:   packed set of 1..10;
    stg:   packed set of 1..20;
    csta,  cstb, cstc, cstd: set of char;
    cste:  set of 'a'..'z';
    cstf:  packed set of 'a'..'f';
    cstg:  packed set of char;
    ci:    char;
    sena,  senb, senc, send: set of enum;
    sene:  set of one..five;
    senf:  packed set of enum;
    seng:  packed set of one..seven;
    ei, ea: enum;
    sba,   sbb, sbc, sbd: set of boolean;
    sbe:   set of false..true;
    sbf:   packed set of boolean;
    sbg:   packed set of false..true;
    ai:    arri;
    arec:  rec;
    parec: prec;
    vrec:  recv;
    ip:    iptr;
    avi:   arri;
    avi2:  arri;
    pavi:  packed array [1..10] of integer;
    avis:  array [1..10] of 10..20;
    pavis: packed array [1..10] of 10..20;
    avb:   array [1..10] of boolean;
    pavb:  packed array [1..10] of boolean;
    avr:   array [1..10] of real;
    pavr:  packed array [1..10] of real;
    avc:   array [1..10] of char;
    pavc:  packed array [1..10] of char;
    avcs:  array [1..10] of 'g'..'p';
    pavcs: packed array [1..10] of 'g'..'p';
    ave:   array [1..10] of enum;
    pave:  packed array [1..10] of enum;
    aves:  array [1..10] of esub;
    paves: packed array [1..10] of esub;
    avs:   array [1..10] of cset;
    pavs:  packed array [1..10] of cset;
    avrc:  array [1..10] of recs;
    pavrc: packed array [1..10] of recs;
    avp:   array [1..10] of iptr;
    pavp:  packed array [1..10] of iptr;
    bia:   array [boolean] of integer;
    pbia:  packed array [boolean] of integer;
    cia:   array [char] of integer;
    pcia:  packed array [char] of integer;
    csia:  array ['a'..'z'] of integer;
    pcsia: packed array ['a'..'z'] of integer;
    eia:   array [enum] of integer;
    peia:  packed array [enum] of integer;
    esia:  array [two..six] of integer;
    pesia: packed array [two..six] of integer;
    mdar:  arrim;
    mdar2: arrim;
    vra:   record

              i: integer;
              case vt: vart of

                 vti:   (vdi:   integer;  a: integer);
                 vtb:   (vdb:   boolean;  b: integer);
                 vtc:   (vdc:   char;     c: integer);
                 vte:   (vde:   enum;     d: integer);
                 vtes:  (vdes:  esub;     e: integer);
                 vts:   (vds:   subr;     f: integer);
                 vtr:   (vdr:   real;     g: integer);
                 vtst:  (vdst:  string10; h: integer);
                 vta:   (vda:   arri;     j: integer);
                 vtrc:  (vdrc:  recs;     k: integer);
                 vtstc: (vdstc: cset;     l: integer);
                 vtp:   (vdp:   iptr;     m: integer)

              (* end *)

           end;
    vvrs:  record

              case vt: subr of

                 10, 11, 12, 13, 14, 15: (vi: integer);
                 16, 17, 18, 19, 20: (vb: boolean)

              (* end *)

           end; 
    vvrb:  record

              case vt:boolean of

                 true: (vi: integer);
                 false: (vb: boolean)

              (* end *)

           end; 
    vvre:  record

              case vt: enum of

                 one, two, three, four, five: (vi: integer);
                 six, seven, eight, nine, ten: (vb: boolean)

              (* end *)

           end; 
    vvres: record

              case vt: esub of

                 three, four: (vi: integer);
                 five, six: (vb: boolean)

              (* end *)

           end; 
    nvr:   record

              i: integer;
              r: record

                 i: integer;
                 r: record

                    i: integer;
                    r: record

                       i: integer;
                       r: record

                          i: integer;
                          r: record

                             i: integer;
                             r: record

                                i: integer;
                                r: record

                                   i: integer;
                                   r: record

                                      i: integer;
                                      r: record

                                         i: integer

                                      end

                                   end

                                end

                             end

                          end

                       end

                    end

                 end

              end

           end;
    rpa:   ^rec;
    ara:   arrr;
    pti, pti1: ^integer;
    ptb:   ^boolean;
    ptc:   ^char;
    pte:   ^enum;
    ptes:  ^esub;
    pts:   ^subr;
    ptr:   ^real;
    ptst:  ^string10;
    pta:   ^arri;
    ptrc:  ^recs;
    ptstc: ^cset;
    ptp:   ^iptr;

procedure junk1(z, q : integer);
 
begin

   write(z:1, ' ', q:1);

end;
 
procedure junk2(var z : integer);
 
begin

   z := z + 1

end;
 
procedure junk3(var p : string10);
 
begin

   write(p)

end;
 
procedure junk4(p : string10);
 
begin

   p[5] := '?';
   write(p)

end;
 
function junk5(x : integer) : integer;
 
begin

   junk5 := x + 1

end;

function junk7(a, b, c: integer): integer; forward;

function junk7;

var x, y, z: integer;

begin

   x := 1;
   y := 2;
   z := 3;
   write(a:1, ' ', b:1, ' ', c:1, ' ');
   a := 4;
   b := 5;
   c := 6;
   write(c:1, ' ', b:1, ' ', a:1, ' ', z:1, ' ', y:1, ' ', x:1);
   junk7 := 78

end;

procedure junk8(a: integer; b: boolean; c: char; e: enum; es: esub; s: subr;
                r: real; st: string10; ar: arri; rc: rec; rv: recv; stc: cset;
                p: iptr);

var i:  integer;
    ci: char;

begin

   writeln(a:1, ' ', ord(b):5, ' ', c:1, ' ', ord(e):1, ' ', ord(es):1, ' ', s:1, ' ', 
           r:15, ' ', st);
   for i := 1 to 10 do write(ar[i]:1, ' '); writeln;
   writeln(rc.i:1, ' ', ord(rc.b):5, ' ', rc.c:1, ' ', ord(rc.e):1, ' ', ord(rc.es):1, 
           ' ', rc.s:1, ' ', rc.r:15, ' ', rc.st);
   for i := 1 to 10 do write(rc.a[i]:1, ' '); writeln;
   writeln(rc.rc.a:1, ' ', rc.rc.b:1);
   for ci := 'a' to 'j' do if ci in rc.stc then write(ci) else write('_');
   writeln;
   writeln(rc.p^:1);
   writeln(rv.a:1, ' ', rv.b:1, ' ', ord(rv.c):5);
   if rv.c then writeln(ord(rv.e):1) else writeln(rv.d);
   for ci := 'a' to 'j' do if ci in stc then write(ci) else write('_');
   writeln;
   writeln(p^:1)

end;

procedure junk10(x, y: integer; junk10: char);

begin

   write(x:1, ' ', y:1, ' ', junk10:1)

end;   

function junk11(x: integer): integer;

begin

   junk11 := succ(x)

end;

procedure junk14;

var i, x: integer;

procedure junk15;

begin

   write(i:1, ' ', x:1)

end;

begin

   i := 62;
   x := 76;
   junk15

end;

begin

   write('****************************************************************');
   writeln('***************');
   writeln;
   writeln('                 TEST SUITE FOR P4 PASCAL');
   writeln;
   write('                 Copyright (C) 1995 S. A. Moore - All rights ');
   writeln('reserved');
   writeln;
   write('****************************************************************');
   writeln('***************');
   writeln;

(*******************************************************************************

                                 Metering

*******************************************************************************)

   writeln;
   writeln('The following are implementation defined characteristics');
   writeln;
   writeln('Integer default output field');
   writeln('         1111111111222222222233333333334');
   writeln('1234567890123456789012345678901234567890');
   writeln(1);
   writeln('Real default output field');
   writeln('         1111111111222222222233333333334');
   writeln('1234567890123456789012345678901234567890');
   writeln(1.2);
   writeln('Char default output field');
   writeln('         1111111111222222222233333333334');
   writeln('1234567890123456789012345678901234567890');
   writeln('a');
   if (ord('a') = 97) and (ord('(') = 40) and (ord('^') = 94) then
      writeln('Appears to be ASCII')
   else
      writeln('Appears to not be ASCII');

(*******************************************************************************

                           Control structures

*******************************************************************************)

   writeln;
   writeln('******************* Control structures tests *******************');
   writeln;
   write('Control1: ');
   for i := 1 to 10 do write(i:1, ' ');
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control2: ');
   for i := 10 downto 1 do write(i:1, ' ');
   writeln('s/b 10 9 8 7 6 5 4 3 2 1');
   write('Control3: ');
   i := 1;
   while i <=10 do begin write(i:1, ' '); i := i + 1 end;
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control4: ');
   i := 1; repeat write(i:1, ' '); i := i + 1 until i > 10;
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control5: ');
   i := 1;
   1: write(i:1, ' '); i := i + 1; if i <= 10 then goto 1;
   writeln('s/b 1 2 3 4 5 6 7 8 9 10');
   write('Control6: ');
   if true then write('yes') else write('no');
   writeln(' s/b yes');
   write('Control7: ');
   if false then write('no') else write('yes');
   writeln(' s/b yes');
   write('Control8: ');
   if true then write('yes '); write('stop');
   writeln(' s/b yes stop');
   write('Control9: ');
   if false then write('no '); write('stop');
   writeln(' s/b stop');
   write('Control10: ');
   for i := 1 to 10 do
      case i of
         1:     write('one ');
         2:     write('two ');
         3:     write('three ');
         4:     write('four ');
         5:     write('five ');
         6:     write('six ');
         7:     write('seven ');
         8:     write('eight ');
         9, 10: write('nine-ten ')

      end;
   writeln;
   write('Control10: s/b ');
   write('one two three four five ');
   writeln('six seven eight nine-ten nine-ten');
   write('Control12: start ');
   goto 003;
   write('!! BAD !!');
   3: writeln('stop s/b start stop');

(*******************************************************************************

                            Integers

*******************************************************************************)

   writeln;
   writeln('******************* Integers *******************');
   writeln;

   (* integer variables *)
   x := 43; y := 78; z := y;
   writeln('Integer1:   ', x + y:1, ' s/b 121');
   writeln('Integer2:   ', y - x:1, ' s/b 35');
   writeln('Integer3:   ', x * y:1, ' s/b 3354');
   writeln('Integer4:   ', y div x:1, ' s/b 1');
   writeln('Integer5:   ', y mod x:1, ' s/b 35');
   writeln('Integer6:   ', succ(x):1, ' s/b 44');
   writeln('Integer7:   ', pred(x):1, ' s/b 42');
   writeln('Integer8:   ', sqr(x):1, ' s/b 1849');
   writeln('Integer9:   ', chr(y), ' s/b N');
   writeln('Integer10:  ', ord(chr(x)):1, ' s/b 43');
   writeln('Integer11:  ', ord(odd(x)):5, ' s/b 1');
   writeln('Integer12:  ', ord(odd(y)):5, ' s/b 0');
   writeln('Integer13:  ', ord(z = y):5, ' s/b 1');
   writeln('Integer14:  ', ord(x = y):5, ' s/b 0');
   writeln('Integer15:  ', ord(x < y):5, ' s/b 1');
   writeln('Integer16:  ', ord(y < x):5, ' s/b 0');
   writeln('Integer17:  ', ord(y > x):5, ' s/b 1');
   writeln('Integer18:  ', ord(x > y):5, ' s/b 0');
   writeln('Integer19:  ', ord(x <> y):5, ' s/b 1');
   writeln('Integer20:  ', ord(y <> z):5, ' s/b 0');
   writeln('Integer21:  ', ord(x <= y):5, ' s/b 1');
   writeln('Integer22:  ', ord(z <= y):5, ' s/b 1');
   writeln('Integer23:  ', ord(y <= x):5, ' s/b 0');
   writeln('Integer24:  ', ord(y >= x):5, ' s/b 1');
   writeln('Integer25:  ', ord(y >= z):5, ' s/b 1');
   writeln('Integer26:  ', ord(x >= y):5, ' s/b 0');
 
   (* unsigned integer constants *)
   write('Integer27:  '); i := 546; writeln(i:1, ' s/b 546');
   writeln('Integer28:  ', 56 + 34:1, ' s/b 90');
   writeln('Integer29:  ', 56 - 34:1, ' s/b 22');
   writeln('Integer30:  ', 56 * 34:1, ' s/b 1904');
   writeln('Integer31:  ', 56 div 34:1, ' s/b 1');
   writeln('Integer32:  ', 56 mod 34:1, ' s/b 22');
   writeln('Integer33:  ', succ(5):1, ' s/b 6');
   writeln('Integer34:  ', pred(5):1, ' s/b 4');
   writeln('Integer35:  ', sqr(7):1, ' s/b 49');
   writeln('Integer36:  ', chr(65), ' s/b A');
   writeln('Integer37:  ', ord(chr(65)):1, ' s/b 65');
   writeln('Integer38:  ', tcnst:1, ' s/b 768');
   writeln('Integer39:  ', ord(odd(5)):5, ' s/b 1');
   writeln('Integer40:  ', ord(odd(8)):5, ' s/b 0');
   writeln('Integer41:  ', ord(56 = 56):5, ' s/b 1');
   writeln('Integer42:  ', ord(56 = 57):5, ' s/b 0');
   writeln('Integer43:  ', ord(56 < 57):5, ' s/b 1');
   writeln('Integer44:  ', ord(57 < 56):5, ' s/b 0');
   writeln('Integer45:  ', ord(57 > 56):5, ' s/b 1');
   writeln('Integer46:  ', ord(56 > 57):5, ' s/b 0');
   writeln('Integer47:  ', ord(56 <> 57):5, ' s/b 1');
   writeln('Integer48:  ', ord(56 <> 56):5, ' s/b 0');
   writeln('Integer49:  ', ord(55 <= 500):5, ' s/b 1');
   writeln('Integer50:  ', ord(67 <= 67):5, ' s/b 1');
   writeln('Integer51:  ', ord(56 <= 33):5, ' s/b 0');
   writeln('Integer52:  ', ord(645 >= 4):5, ' s/b 1');
   writeln('Integer53:  ', ord(23 >= 23):5, ' s/b 1');
   writeln('Integer54:  ', ord(45 >= 123):5, ' s/b 0');

   (* signed integer variables *)
   as := -14;
   bs := -32;
   cs := -14;
   ds := 20;
   es := -15;
   writeln('Integer55:  ', as + ds:1, ' s/b 6');
   writeln('Integer56:  ', ds + as:1, ' s/b 6');
   writeln('Integer57:  ', bs + ds:1, ' s/b -12');
   writeln('Integer58:  ', as + bs:1, ' s/b -46');
   writeln('Integer59:  ', ds - as:1, ' s/b 34');
   writeln('Integer60:  ', bs - ds:1, ' s/b -52');
   writeln('Integer61:  ', bs - as:1, ' s/b -18');
   writeln('Integer62:  ', ds * as:1, ' s/b -280');
   writeln('Integer63:  ', as * ds:1, ' s/b -280');
   writeln('Integer64:  ', as * bs:1, ' s/b 448');
   writeln('Integer65:  ', ds div as:1, ' s/b -1');
   writeln('Integer66:  ', bs div ds:1, ' s/b -1');
   writeln('Integer67:  ', bs div as:1, ' s/b 2');
   writeln('Integer68:  ', succ(as):1, ' s/b -13');
   writeln('Integer69:  ', pred(bs):1, ' s/b -33');
   writeln('Integer70: ', sqr(as):1, ' s/b 196');
   writeln('Integer71:  ', ord(odd(as)):5, ' s/b 0');
   writeln('Integer72:  ', ord(odd(es)):5, ' s/b 1');
   writeln('Integer73:  ', ord(as = cs):5, ' s/b 1');
   writeln('Integer74:  ', ord(as = bs):5, ' s/b 0');
   writeln('Integer75:  ', ord(as <> bs):5, ' s/b 1');
   writeln('Integer76:  ', ord(as <> cs):5, ' s/b 0');
   writeln('Integer77:  ', ord(as < ds):5, ' s/b 1');
   writeln('Integer78:  ', ord(bs < as):5, ' s/b 1');
   writeln('Integer79:  ', ord(ds < as):5, ' s/b 0');
   writeln('Integer80:  ', ord(as < bs):5, ' s/b 0');
   writeln('Integer81:  ', ord(ds > as):5, ' s/b 1');
   writeln('Integer82:  ', ord(as > bs):5, ' s/b 1');
   writeln('Integer83:  ', ord(as > ds):5, ' s/b 0');
   writeln('Integer84:  ', ord(bs > as):5, ' s/b 0');
   writeln('Integer85:  ', ord(as <= ds):5, ' s/b 1');
   writeln('Integer86:  ', ord(bs <= as):5, ' s/b 1');
   writeln('Integer87:  ', ord(as <= cs):5, ' s/b 1');
   writeln('Integer88:  ', ord(ds <= as):5, ' s/b 0');
   writeln('Integer89:  ', ord(as <= bs):5, ' s/b 0');
   writeln('Integer90:  ', ord(ds >= as):5, ' s/b 1');
   writeln('Integer91:  ', ord(as >= bs):5, ' s/b 1');
   writeln('Integer92:  ', ord(as >= cs):5, ' s/b 1');
   writeln('Integer93:  ', ord(as >= ds):5, ' s/b 0');
   writeln('Integer94:  ', ord(bs >= as):5, ' s/b 0');
   writeln('Integer95:  ', abs(as):1, ' s/b 14');

   (* signed integer constants *)
   writeln('Integer96:  ', 45 + (-30):1, ' s/b 15');
   writeln('Integer97:  ', -25 + 70:1, ' s/b 45');
   writeln('Integer98: ', -62 + 23:1, ' s/b -39');
   writeln('Integer99: ', -20 + (-15):1, ' s/b -35');
   writeln('Integer100: ', 20 - (-14):1, ' s/b 34');
   writeln('Integer101: ', -34 - 14:1, ' s/b -48');
   writeln('Integer102: ', -56 - (-12):1, ' s/b -44');
   writeln('Integer103: ', 5 * (-4):1, ' s/b -20');
   writeln('Integer104: ', (-18) * 7:1, ' s/b -126');
   writeln('Integer105: ', (-40) * (-13):1, ' s/b 520');
   writeln('Integer106: ', 30 div (-5):1, ' s/b -6');
   writeln('Integer107: ', (-50) div 2:1, ' s/b -25');
   writeln('Integer108: ', (-20) div (-4):1, ' s/b 5');
   writeln('Integer109: ', succ(-10):1, ' s/b -9');
   writeln('Integer110: ', succ(-1):1, ' s/b 0');
   writeln('Integer111: ', pred(-1):1, ' s/b -2');
   writeln('Integer112: ', sqr(-8):1, ' s/b 64');
   writeln('Integer113: ', pred(-54):1, ' s/b -55');
   writeln('Integer114: ', ord(odd(-20)):5, ' s/b 0');
   writeln('Integer115: ', ord(odd(-15)):5, ' s/b 1');
   writeln('Integer116: ', ord(-5 = -5):5, ' s/b 1');
   writeln('Integer117: ', ord(-5 = 5):5, ' s/b 0');
   writeln('Integer118: ', ord(-21 <> -40):5, ' s/b 1');
   writeln('Integer119: ', ord(-21 <> -21):5, ' s/b 0');
   writeln('Integer120: ', ord(-3 < 5):5, ' s/b 1');
   writeln('Integer121: ', ord(-32 < -20):5, ' s/b 1');
   writeln('Integer122: ', ord(20 < -20):5, ' s/b 0');
   writeln('Integer123: ', ord(-15 < -40):5, ' s/b 0');
   writeln('Integer124: ', ord(70 > -4):5, ' s/b 1');
   writeln('Integer125: ', ord(-23 > -34):5, ' s/b 1');
   writeln('Integer126: ', ord(-5 > 5):5, ' s/b 0');
   writeln('Integer127: ', ord(-60 > -59):5, ' s/b 0');
   writeln('Integer128: ', ord(-12 <= 4):5, ' s/b 1');
   writeln('Integer129: ', ord(-14 <= -5):5, ' s/b 1');
   writeln('Integer130: ', ord(-7 <= -7):5, ' s/b 1');
   writeln('Integer131: ', ord(5 <= -5):5, ' s/b 0');
   writeln('Integer132: ', ord(-10 <= -20):5, ' s/b 0');
   writeln('Integer133: ', ord(9 >= -3):5, ' s/b 1');
   writeln('Integer134: ', ord(-4 >= -10):5, ' s/b 1');
   writeln('Integer135: ', ord(-13 >= -13):5, ' s/b 1');
   writeln('Integer136: ', ord(-6 >= 6):5, ' s/b 0');
   writeln('Integer137: ', ord(-20 >= -10):5, ' s/b 0');
   writeln('Integer138: ', abs(-6):1, ' s/b 6');
   writeln('Integer139: ', tsncst:1, ' s/b -52');
   
(*******************************************************************************

                            Subranges

*******************************************************************************)

   writeln;
   writeln('******************* Subranges *******************');
   writeln;

   (* subrange unsigned variables *)
   srx := 43; sry := 78; srz := sry;
   writeln('Subrange1:   ', srx + sry:1, ' s/b 121');
   writeln('Subrange2:   ', sry - srx:1, ' s/b 35');
   writeln('Subrange3:   ', srx * sry:1, ' s/b 3354');
   writeln('Subrange4:   ', sry div srx:1, ' s/b 1');
   writeln('Subrange5:   ', sry mod srx:1, ' s/b 35');
   writeln('Subrange6:   ', succ(srx):1, ' s/b 44');
   writeln('Subrange7:   ', pred(srx):1, ' s/b 42');
   writeln('Subrange8:   ', chr(sry), ' s/b N');
   writeln('Subrange9:   ', chr(srx):1, ' s/b +');
   writeln('Subrange10:  ', ord(odd(srx)):5, ' s/b 1');
   writeln('Subrange11:  ', ord(odd(sry)):5, ' s/b 0');
   writeln('Subrange12:  ', ord(srz = sry):5, ' s/b 1');
   writeln('Subrange13:  ', ord(srx = sry):5, ' s/b 0');
   writeln('Subrange14:  ', ord(srx < sry):5, ' s/b 1');
   writeln('Subrange15:  ', ord(sry < srx):5, ' s/b 0');
   writeln('Subrange16:  ', ord(sry > srx):5, ' s/b 1');
   writeln('Subrange17:  ', ord(srx > sry):5, ' s/b 0');
   writeln('Subrange18:  ', ord(srx <> sry):5, ' s/b 1');
   writeln('Subrange19:  ', ord(sry <> srz):5, ' s/b 0');
   writeln('Subrange20:  ', ord(srx <= sry):5, ' s/b 1');
   writeln('Subrange21:  ', ord(srz <= sry):5, ' s/b 1');
   writeln('Subrange22:  ', ord(sry <= srx):5, ' s/b 0');
   writeln('Subrange23:  ', ord(sry >= srx):5, ' s/b 1');
   writeln('Subrange24:  ', ord(sry >= srz):5, ' s/b 1');
   writeln('Subrange25:  ', ord(srx >= sry):5, ' s/b 0');

   (* signed subrange variables *)
   sras := -14;
   srbs := -32;
   srcs := -14;
   srds := 20;
   sres := -15;
   writeln('Subrange26:  ', sras + srds:1, ' s/b 6');
   writeln('Subrange27:  ', srds + sras:1, ' s/b 6');
   writeln('Subrange28:  ', srbs + srds:1, ' s/b -12');
   writeln('Subrange29:  ', sras + srbs:1, ' s/b -46');
   writeln('Subrange30:  ', srds - sras:1, ' s/b 34');
   writeln('Subrange31:  ', srbs - srds:1, ' s/b -52');
   writeln('Subrange32:  ', srbs - sras:1, ' s/b -18');
   writeln('Subrange33:  ', srds * sras:1, ' s/b -280');
   writeln('Subrange34:  ', sras * srds:1, ' s/b -280');
   writeln('Subrange35:  ', sras * srbs:1, ' s/b 448');
   writeln('Subrange36:  ', srds div sras:1, ' s/b -1');
   writeln('Subrange37:  ', srbs div srds:1, ' s/b -1');
   writeln('Subrange38:  ', srbs div sras:1, ' s/b 2');
   writeln('Subrange39:  ', succ(sras):1, ' s/b -13');
   writeln('Subrange40:  ', pred(srbs):1, ' s/b -33');
   writeln('Subrange41:  ', ord(odd(sras)):5, ' s/b 0');
   writeln('Subrange42:  ', ord(odd(sres)):5, ' s/b 1');
   writeln('Subrange43:  ', ord(sras = srcs):5, ' s/b 1');
   writeln('Subrange44:  ', ord(sras = srbs):5, ' s/b 0');
   writeln('Subrange45:  ', ord(sras <> srbs):5, ' s/b 1');
   writeln('Subrange46:  ', ord(sras <> srcs):5, ' s/b 0');
   writeln('Subrange47:  ', ord(sras < srds):5, ' s/b 1');
   writeln('Subrange48:  ', ord(srbs < sras):5, ' s/b 1');
   writeln('Subrange49:  ', ord(srds < sras):5, ' s/b 0');
   writeln('Subrange50:  ', ord(sras < srbs):5, ' s/b 0');
   writeln('Subrange51:  ', ord(srds > sras):5, ' s/b 1');
   writeln('Subrange52:  ', ord(sras > srbs):5, ' s/b 1');
   writeln('Subrange53:  ', ord(sras > srds):5, ' s/b 0');
   writeln('Subrange54:  ', ord(srbs > sras):5, ' s/b 0');
   writeln('Subrange55:  ', ord(sras <= srds):5, ' s/b 1');
   writeln('Subrange56:  ', ord(srbs <= sras):5, ' s/b 1');
   writeln('Subrange57:  ', ord(sras <= srcs):5, ' s/b 1');
   writeln('Subrange58:  ', ord(srds <= sras):5, ' s/b 0');
   writeln('Subrange59:  ', ord(sras <= srbs):5, ' s/b 0');
   writeln('Subrange60:  ', ord(srds >= sras):5, ' s/b 1');
   writeln('Subrange61:  ', ord(sras >= srbs):5, ' s/b 1');
   writeln('Subrange62:  ', ord(sras >= srcs):5, ' s/b 1');
   writeln('Subrange63:  ', ord(sras >= srds):5, ' s/b 0');
   writeln('Subrange64:  ', ord(srbs >= sras):5, ' s/b 0');
   writeln('Subrange65:  ', abs(sras):1, ' s/b 14');

(*******************************************************************************

                         Characters

*******************************************************************************)
 
   writeln;
   writeln('******************* Characters*******************');
   writeln;

   (* character variables *)
   ca := 'g'; cb := 'g'; cc := 'u';
   writeln('Character1:   ', ca, ' ', cb, ' ', cc, ' s/b g g u');
   writeln('Character2:   ', succ(ca), ' s/b h');
   writeln('Character3:   ', pred(cb), ' s/b f');
   writeln('Character4:   ', ord(ca):1, ' s/b 103');
   writeln('Character5:   ', chr(ord(cc)), ' s/b u');
   writeln('Character6:   ', ord(ca = cb):5, ' s/b 1');
   writeln('Character7:   ', ord(ca = cc):5, ' s/b 0');
   writeln('Character8:   ', ord(ca < cc):5, ' s/b 1');
   writeln('Character9:   ', ord(cc < ca):5, ' s/b 0');
   writeln('Character10:  ', ord(cc > ca):5, ' s/b 1');
   writeln('Character11:  ', ord(ca > cc):5, ' s/b 0');
   writeln('Character12:  ', ord(ca <> cc):5, ' s/b 1');
   writeln('Character13:  ', ord(ca <> cb):5, ' s/b 0');
   writeln('Character14:  ', ord(ca <= cc):5, ' s/b 1');
   writeln('Character15:  ', ord(ca <= cb):5, ' s/b 1');
   writeln('Character16:  ', ord(cc <= ca):5, ' s/b 0');
   writeln('Character17:  ', ord(cc >= cb):5, ' s/b 1');
   writeln('Character18:  ', ord(cb >= ca):5, ' s/b 1');
   writeln('Character19:  ', ord(cb >= cc):5, ' s/b 0');
   sa := 'porker    '; sb := 'porker    '; sc := 'parker    ';
   writeln('Character20:  ', sa, sb, sc,
      ' s/b porker    porker    parker');
   writeln('Character21:  ', ord(sa = sb):5, ' s/b 1');
   writeln('Character22:  ', ord(sa = sc):5, ' s/b 0');
   writeln('Character23:  ', ord(sc < sa):5, ' s/b 1');
   writeln('Character24:  ', ord(sa < sc):5, ' s/b 0');
   writeln('Character25:  ', ord(sa > sc):5, ' s/b 1');
   writeln('Character26:  ', ord(sc > sa):5, ' s/b 0');
   writeln('Character27:  ', ord(sa <> sc):5, ' s/b 1');
   writeln('Character28:  ', ord(sa <> sb):5, ' s/b 0');
   writeln('Character29:  ', ord(sc <= sa):5, ' s/b 1');
   writeln('Character30:  ', ord(sa <= sb):5, ' s/b 1');
   writeln('Character40:  ', ord(sa <= sc):5, ' s/b 0');
   writeln('Character41:  ', ord(sa >= sc):5, ' s/b 1');
   writeln('Character42:  ', ord(sa >= sb):5, ' s/b 1');
   writeln('Character43:  ', ord(sc >= sa):5, ' s/b 0');
   write('Character44:  ');
   for ca := 'a' to 'z' do write(ca);
   writeln(' s/b abcdefghijklmnopqrstuvwxyz');
   write('Character45:  ');
   for ca := 'z' downto 'a' do write(ca);
   writeln(' s/b zyxwvutsrqponmlkjihgfedcba');
   write('Character46:  ');
   x := 0;
   for ca := 'a' to 'z' do begin car[ca] := x; x := x + 1 end;
   for ca := 'z' downto 'a' do write(car[ca]:1, ' ');
   writeln;
   writeln('Character46: s/b 25 24 23 22 21 20 19 18 17 16 15',
      ' 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0');
   r.rc := 'n'; writeln('Character47: ', r.rc, ' s/b n');
   r.rs := 'junky01234'; writeln('Character48: ', r.rs,
                           ' s/b junky01234');
   for i := 1 to 10 do sar[i] := '0123456789';
   sar[1] := 'trash     ';
   sar[2] := 'finnork   ';
   sar[10] := 'crapola   ';
   writeln('Character49:  ');
   for i := 10 downto 1 do writeln(sar[i]);
   writeln('Character49: s/b');
   writeln('crapola');   
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('0123456789');
   writeln('finnork');   
   writeln('trash');     
   writeln('Character50:  ');
   for ca := '0' to '9' do
   begin
     case ca of
       '5': write('five ');
       '3': write('three ');
       '6': write('six ');
       '8': write('eight ');
       '0': write('zero ');
       '9': write('nine ');
       '7': write('seven ');
       '4': write('four ');
       '1': write('one ');
       '2': write('two ');
     end
   end;
   writeln;
   writeln(' s/b zero one two three four five six ',
           'seven eight nine');

   (* character constants *) 
   writeln('Character51:  ', 'a', ' s/b a');
   writeln('Character52:  ', succ('a'), ' s/b b');
   writeln('Character53:  ', pred('z'), ' s/b y');
   writeln('Character54:  ', ord('c'):1, ' s/b 99');
   writeln('Character55:  ', chr(ord('g')), ' s/b g');
   writeln('Character56:  ', ord('q' = 'q'):5, ' s/b 1');
   writeln('Character57:  ', ord('r' = 'q'):5, ' s/b 0');
   writeln('Character58:  ', ord('b' < 't'):5, ' s/b 1');
   writeln('Character59:  ', ord('g' < 'c'):5, ' s/b 0');
   writeln('Character50:  ', ord('f' > 'e'):5, ' s/b 1');
   writeln('Character61:  ', ord('f' > 'g'):5, ' s/b 0');
   writeln('Character62:  ', ord('h' <> 'l'):5, ' s/b 1');
   writeln('Character63:  ', ord('i' <> 'i'):5, ' s/b 0');
   writeln('Character64:  ', ord('v' <= 'y'):5, ' s/b 1');
   writeln('Character65:  ', ord('y' <= 'y'):5, ' s/b 1');
   writeln('Character66:  ', ord('z' <= 'y'):5, ' s/b 0');
   writeln('Character67:  ', ord('l' >= 'b'):5, ' s/b 1');
   writeln('Character68:  ', ord('l' >= 'l'):5, ' s/b 1');
   writeln('Character69:  ', ord('l' >= 'm'):5, ' s/b 0');
   writeln('Character70:  ', ord('finnork' = 'finnork'):5, ' s/b 1');
   writeln('Character71:  ',
      ord('finoork' = 'finnork'):5, ' s/b 0');
   writeln('Character72:  ', ord('oliab' < 'olibb'):5, ' s/b 1');
   writeln('Character73:  ', ord('olibb' < 'oliab'):5, ' s/b 0');
   writeln('Character74:  ', ord('olibb' > 'oliab'):5, ' s/b 1');
   writeln('Character75:  ', ord('oliab' > 'olibb'):5, ' s/b 0');
   writeln('Character76:  ', ord('fark ' <> 'farks'):5, ' s/b 1');
   writeln('Character77:  ', ord('farks' <> 'farks'):5, ' s/b 0');
   writeln('Character78:  ', ord('farka' <= 'farkz'):5, ' s/b 1');
   writeln('Character79:  ', ord('farks' <= 'farks'):5, ' s/b 1');
   writeln('Character80:  ', ord('farkz' <= 'farks'):5, ' s/b 0');
   writeln('Character81:  ', ord('topnat' >= 'topcat'):5, ' s/b 1');
   writeln('Character82:  ', ord('topcat' >= 'topcat'):5, ' s/b 1');
   writeln('Character83:  ', ord('topcat' >= 'topzat'):5, ' s/b 0');
   writeln('Character84:  ', scst, ' s/b this is a string');
   writeln('Character85:  ', ccst, ' s/b v');
   writeln('Character86:  ');
   for i := 15 downto 1 do writeln('hello, world': i);
   writeln('Character86:  s/b:');
   writeln('   hello, world');
   writeln('  hello, world');
   writeln(' hello, world ');
   writeln('hello, world');
   writeln('hello, worl');
   writeln('hello, wor');
   writeln('hello, wo');
   writeln('hello, w');
   writeln('hello, ');
   writeln('hello,');
   writeln('hello');
   writeln('hell');
   writeln('hel');
   writeln('he');
   writeln('h');

(*******************************************************************************

                            Booleans

*******************************************************************************)
 
   writeln;
   writeln('******************* Booleans *******************');
   writeln;

   (* boolean variables *)
   ba := true; bb := false; bc := true;
   writeln('Boolean1:   ', ord(ba):5, ' ', ord(bb):5, ' s/b 1 0');
   writeln('Boolean2:   ', ord(succ(bb)):5, ' s/b 1');
   writeln('Boolean3:   ', ord(pred(ba)):5, ' s/b 0');
   writeln('Boolean4:   ', ord(bb):1, ' s/b 0');
   writeln('Boolean5:   ', ord(ba):1, ' s/b 1');
   writeln('Boolean6:   ', ord(ba = bc):5, ' s/b 1');
   writeln('Boolean7:   ', ord(bb = bb):5, ' s/b 1');
   writeln('Boolean8:   ', ord(ba = bb):5, ' s/b 0');
   writeln('Boolean9:   ', ord(bb < ba):5, ' s/b 1');
   writeln('Boolean10:  ', ord(ba < bb):5, ' s/b 0');
   writeln('Boolean11:  ', ord(ba > bb):5, ' s/b 1');
   writeln('Boolean12:  ', ord(bb > ba):5, ' s/b 0');
   writeln('Boolean13:  ', ord(ba <> bb):5, ' s/b 1');
   writeln('Boolean14:  ', ord(ba <> bc):5, ' s/b 0');
   writeln('Boolean15:  ', ord(bb <= ba):5, ' s/b 1');
   writeln('Boolean16:  ', ord(ba <= bc):5, ' s/b 1');
   writeln('Boolean17:  ', ord(ba <= bb):5, ' s/b 0');
   writeln('Boolean18:  ', ord(ba >= bb):5, ' s/b 1');
   writeln('Boolean19:  ', ord(bb >= bb):5, ' s/b 1');
   writeln('Boolean20:  ', ord(bb >= ba):5, ' s/b 0');
(*

P4 cannot execute this test because it relies on value outside the
range of boolean.
 
   write('Boolean21:  ');
   for ba := false to true do write(ord(ba):5, ' ');
   writeln('s/b false 1');

*)
   write('Boolean23:  ');
   ba := 1 > 0; writeln(ord(ba):5, ' s/b 1');
   write('Boolean24:  ');
   ba := 1 < 0; writeln(ord(ba):5, ' s/b 0');
 
   (* boolean constants *)
   writeln('Boolean25:  ', ord(true):5, ' ', ord(false):5, ' s/b 1 0');
   writeln('Boolean26:  ', ord(succ(false)):5, ' s/b 1');
   writeln('Boolean27:  ', ord(pred(true)):5, ' s/b 0');
   writeln('Boolean28:  ', ord(false):1, ' s/b 0');
   writeln('Boolean29:  ', ord(true):1, ' s/b 1');
   writeln('Boolean30:  ', ord(true = true):5, ' s/b 1');
   writeln('Boolean31:  ', ord(false = false):5, ' s/b 1');
   writeln('Boolean32:  ', ord(true = false):5, ' s/b 0');
   writeln('Boolean33:  ', ord(false < true):5, ' s/b 1');
   writeln('Boolean34:  ', ord(true < false):5, ' s/b 0');
   writeln('Boolean35:  ', ord(true > false):5, ' s/b 1');
   writeln('Boolean36:  ', ord(false > true):5, ' s/b 0');
   writeln('Boolean37:  ', ord(true <> false):5, ' s/b 1');
   writeln('Boolean38:  ', ord(true <> true):5, ' s/b 0');
   writeln('Boolean39:  ', ord(false <= true):5, ' s/b 1');
   writeln('Boolean40:  ', ord(true <= true):5, ' s/b 1');
   writeln('Boolean41:  ', ord(true <= false):5, ' s/b 0');
   writeln('Boolean42:  ', ord(true >= false):5, ' s/b 1');
   writeln('Boolean43:  ', ord(false >= false):5, ' s/b 1');
   writeln('Boolean44:  ', ord(false >= true):5, ' s/b 0');
   writeln('Boolean45:');
   for i := 10 downto 1 do writeln(ord(false):i);
   writeln('Boolean45: s/b:');
   writeln('         0');
   writeln('        0');
   writeln('       0');
   writeln('      0');
   writeln('     0');
   writeln('    0');
   writeln('   0');
   writeln('  0');
   writeln(' 0');
   writeln('0');
   writeln('Boolean46:');
   for i := 10 downto 1 do writeln(ord(true):i);
   writeln('Boolean46: s/b:');
   writeln('         1');
   writeln('        1');
   writeln('       1');
   writeln('      1');
   writeln('     1');
   writeln('    1');
   writeln('   1');
   writeln('  1');
   writeln(' 1');
   writeln('1');
  

(*******************************************************************************

                            Scalar variables

*******************************************************************************)
 
   writeln;
   writeln('******************* Scalar *******************');
   writeln;

   (* scalar variables *)
   sva := wed; svb := mon; svc := wed;
   writeln('Scalar1:   ', ord(succ(svb) = tue):5, ' s/b 1');
   writeln('Scalar2:   ', ord(pred(sva) = tue):5, ' s/b 1');
   writeln('Scalar3:   ', ord(svb):1, ' s/b 0');
   writeln('Scalar4:   ', ord(sva):1, ' s/b 2');
   writeln('Scalar5:   ', ord(sva = svc):5, ' s/b 1');
   writeln('Scalar6:   ', ord(svb = svb):5, ' s/b 1');
   writeln('Scalar7:   ', ord(sva = svb):5, ' s/b 0');
   writeln('Scalar8:   ', ord(svb < sva):5, ' s/b 1');
   writeln('Scalar9:   ', ord(sva < svb):5, ' s/b 0');
   writeln('Scalar10:  ', ord(sva > svb):5, ' s/b 1');
   writeln('Scalar11:  ', ord(svb > sva):5, ' s/b 0');
   writeln('Scalar12:  ', ord(sva <> svb):5, ' s/b 1');
   writeln('Scalar13:  ', ord(sva <> svc):5, ' s/b 0');
   writeln('Scalar14:  ', ord(svb <= sva):5, ' s/b 1');
   writeln('Scalar15:  ', ord(sva <= svc):5, ' s/b 1');
   writeln('Scalar16:  ', ord(sva <= svb):5, ' s/b 0');
   writeln('Scalar17:  ', ord(sva >= svb):5, ' s/b 1');
   writeln('Scalar18:  ', ord(svb >= svb):5, ' s/b 1');
   writeln('Scalar19:  ', ord(svb >= sva):5, ' s/b 0');
   write('Scalar20:  ');
   for sva := mon to sun do write(ord(sva):1, ' ');
   writeln('s/b 0 1 2 3 4 5 6');
   write('Scalar21:  ');
   for svb := sun downto mon do write(ord(svb):1, ' ');
   writeln('s/b 6 5 4 3 2 1 0');

   (* scalar constants *) 
   writeln('Scalar1:   ', ord(succ(mon) = tue):5, ' s/b 1');
   writeln('Scalar2:   ', ord(pred(fri) = thur):5, ' s/b 1');
   writeln('Scalar3:   ', ord(wed):1, ' s/b 2');
   writeln('Scalar4:   ', ord(sun):1, ' s/b 6');
   writeln('Scalar5:   ', ord(thur = thur):5, ' s/b 1');
   writeln('Scalar6:   ', ord(fri = fri):5, ' s/b 1');
   writeln('Scalar7:   ', ord(tue = wed):5, ' s/b 0');
   writeln('Scalar8:   ', ord(mon < wed):5, ' s/b 1');
   writeln('Scalar9:   ', ord(fri < fri):5, ' s/b 0');
   writeln('Scalar10:  ', ord(sun > sat):5, ' s/b 1');
   writeln('Scalar11:  ', ord(fri > sun):5, ' s/b 0');
   writeln('Scalar12:  ', ord(thur <> tue):5, ' s/b 1');
   writeln('Scalar13:  ', ord(wed <> wed):5, ' s/b 0');
   writeln('Scalar14:  ', ord(mon <= fri):5, ' s/b 1');
   writeln('Scalar15:  ', ord(fri <= fri):5, ' s/b 1');
   writeln('Scalar16:  ', ord(sat <= fri):5, ' s/b 0');
   writeln('Scalar17:  ', ord(fri >= tue):5, ' s/b 1');
   writeln('Scalar18:  ', ord(tue >= tue):5, ' s/b 1');
   writeln('Scalar19:  ', ord(tue >= sat):5, ' s/b 0');

(*******************************************************************************

                            Reals

*******************************************************************************)

   writeln;
   writeln('******************* Reals ******************************');
   writeln;

   (* formats, input (compiler) and output *)
   writeln('Real1:   ', 1.554:15, ' s/b  1.55400000e+00');
   writeln('Real2:   ', 0.00334:15, ' s/b  3.34000000e-03');
   writeln('Real3:   ', 0.00334e-21:15, ' s/b  3.34000000e-24');
   writeln('Real4:   ', 4e-45:15, ' s/b  4.00000000e-45');
   writeln('Real5:   ', -5.565:15, ' s/b -5.56500000e+03');
   writeln('Real6:   ', -0.00944:15, ' s/b -9.44000000e-03');
   writeln('Real7:   ', -0.006364e+32:15, ' s/b -6.36400000e+29');
   writeln('Real8:   ', -2e-14:15, ' s/b -2.00000000e-14');
   writeln('Real9:');
   writeln('         11111111112222222222333333333344444444445');
   writeln('12345678901234567890123456789012345678901234567890');
   for i := 1 to 20 do writeln(1.23456789012345678901234567890:i);
   writeln('s/b (note precision dropoff at right):');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.2e+000');
   writeln(' 1.23e+000');
   writeln(' 1.234e+000');
   writeln(' 1.2345e+000');
   writeln(' 1.23456e+000');
   writeln(' 1.234567e+000');
   writeln(' 1.2345678e+000');
   writeln(' 1.23456789e+000');
   writeln(' 1.234567890e+000');
   writeln(' 1.2345678901e+000');
   writeln(' 1.23456789012e+000');
   writeln(' 1.234567890123e+000');

   (* unsigned variables *)
   ra := 435.23; 
   rb := 983.67; 
   rc := rb;
   rd := 0.3443;
   writeln('Real11:  ', ra + rb:15, ' s/b  1.41890000e+03');
   writeln('Rea112:  ', rb - ra:15, ' s/b  5.48439999e+02');
   writeln('Real13:  ', ra * rb:15, ' s/b  4.28122700e+05');
   writeln('Real14:  ', rb / ra:15, ' s/b  2.26011500e+00');
   writeln('Real15:  ', ord(rc = rb):5, ' s/b 1');
   writeln('Real16:  ', ord(ra = rb):5, ' s/b 0');
   writeln('Real17:  ', ord(ra < rb):5, ' s/b 1');
   writeln('Real18:  ', ord(rb < ra):5, ' s/b 0');
   writeln('Real19:  ', ord(rb > ra):5, ' s/b 1');
   writeln('Real20:  ', ord(ra > rb):5, ' s/b 0');
   writeln('Real21:  ', ord(ra <> rb):5, ' s/b 1');
   writeln('Real22:  ', ord(rb <> rc):5, ' s/b 0');
   writeln('Real23:  ', ord(ra <= rb):5, ' s/b 1');
   writeln('Real24:  ', ord(rc <= rb):5, ' s/b 1');
   writeln('Real25:  ', ord(rb <= ra):5, ' s/b 0');
   writeln('Real26:  ', ord(rb >= ra):5, ' s/b 1');
   writeln('Real27:  ', ord(rb >= rc):5, ' s/b 1');
   writeln('Real28:  ', ord(ra >= rb):5, ' s/b 0');
   writeln('Real29:  ', abs(ra):15, ' s/b  4.35230e+02');
   writeln('Real30:  ', sqr(ra):15, ' s/b  1.89425e+05');
   writeln('Real31:  ', sqrt(rb):15, ' s/b  3.13635e+01');
   writeln('Real32:  ', sin(rb):15, ' s/b -3.44290e-01');
   writeln('Real33:  ', arctan(ra):15, ' s/b  1.56850e+00');
   writeln('Real34:  ', exp(rd):15, ' s/b  1.41100e+00');
   writeln('Real35:  ', ln(ra):15, ' s/b  6.07587e+00');
   writeln('Real36:  ', trunc(ra):1, ' s/b 435');

   (* unsigned constants *)
   writeln('Real39:  ', 344.939 + 933.113:15, ' s/b  1.278052e+03');
   writeln('Real40:  ', 883.885 - 644.939:15, ' s/b  2.389460e+02');
   writeln('Real41:  ', 754.74 * 138.75:15, ' s/b  1.047202e+05');
   writeln('Real42:  ', 634.3 / 87373.99:15, ' s/b  7.259598e-03');
   writeln('Real43:  ', ord(77.44 = 77.44):5, ' s/b 1');
   writeln('Real44:  ', ord(733.9 = 959.2):5, ' s/b 0');
   writeln('Real45:  ', ord(883.22 < 8383.33):5, ' s/b 1');
   writeln('Real46:  ', ord(475.322 < 234.93):5, ' s/b 0');
   writeln('Real47:  ', ord(7374.3 > 6442.34):5, ' s/b 1');
   writeln('Real48:  ', ord(985.562 > 1001.95):5, ' s/b 0');
   writeln('Real49:  ', ord(030.11 <> 0938.44):5, ' s/b 1');
   writeln('Real50:  ', ord(1.233 <> 1.233):5, ' s/b 0');
   writeln('Real51:  ', ord(8484.002 <= 9344.003):5, ' s/b 1');
   writeln('Real52:  ', ord(9.11 <= 9.11):5, ' s/b 1');
   writeln('Real53:  ', ord(93.323 <= 90.323):5, ' s/b 0');
   writeln('Real54:  ', ord(6543.44 >= 5883.33):5, ' s/b 1');
   writeln('Real55:  ', ord(3247.03 >= 3247.03):5, ' s/b 1');
   writeln('Real56:  ', ord(28343.22 >= 30044.45):5, ' s/b 0');
   writeln('Real57:  ', abs(34.93):15, ' s/b  3.493000e+01');
   writeln('Real58:  ', sqr(2.34):15, ' s/b  5.475600e+00');
   writeln('Real59:  ', sqrt(9454.32):15, ' s/b  9.723333e+01');
   writeln('Real60:  ', sin(34.22):15, ' s/b  3.311461e-01');
   writeln('Real61:  ', arctan(343.2):15, ' s/b  1.567883e+00');
   writeln('Real62:  ', exp(0.332):15, ' s/b  1.393753e+00');
   writeln('Real63:  ', ln(83.22):15, ' s/b  4.421488e+00');
   writeln('Real64:  ', trunc(24.344):1, ' s/b 24');
   writeln('Real67:  ', rcnst:15, ' s/b  4.333000e+01');

   (* signed variables *)
   ra := -734.2;
   rb := -7634.52;
   rc := ra;
   rd := 1034.54;
   re := -0.38483;
   writeln('Real68:  ', ra + rd:15, ' s/b  3.003400e+02');
   writeln('Real69:  ', rd + ra:15, ' s/b  3.003400e+02');
   writeln('Real70:  ', rb + rd:15, ' s/b -6.599980e+03');
   writeln('Real71:  ', ra + rb:15, ' s/b -8.368720e+03');
   writeln('Real72:  ', rd - ra:15, ' s/b  1.768740e+03');
   writeln('Real73:  ', rb - rd:15, ' s/b -8.669061e+03');
   writeln('Real74:  ', rb - ra:15, ' s/b -6.900320e+03');
   writeln('Real75:  ', rd * ra:15, ' s/b -7.595593e+05');
   writeln('Real76:  ', ra * rd:15, ' s/b -7.595593e+05');
   writeln('Real77:  ', ra * rb:15, ' s/b  5.605265e+06');
   writeln('Real78:  ', rd / ra:15, ' s/b -1.409071e+00');
   writeln('Real79:  ', rb / rd:15, ' s/b -7.379627e+00');
   writeln('Real80:  ', rb / ra:15, ' s/b  1.039842e+01');
   writeln('Real81:  ', ord(ra = rc):5, ' s/b 1');
   writeln('Real82:  ', ord(ra = rb):5, ' s/b 0');
   writeln('Real83:  ', ord(ra <> rb):5, ' s/b 1');
   writeln('Real84:  ', ord(ra <> rc):5, ' s/b 0');
   writeln('Real85:  ', ord(ra < rd):5, ' s/b 1');
   writeln('Real86:  ', ord(rb < ra):5, ' s/b 1');
   writeln('Real87:  ', ord(rd < ra):5, ' s/b 0');
   writeln('Real88:  ', ord(ra < rb):5, ' s/b 0');
   writeln('Real89:  ', ord(rd > ra):5, ' s/b 1');
   writeln('Real90:  ', ord(ra > rb):5, ' s/b 1');
   writeln('Real91:  ', ord(ra > rd):5, ' s/b 0');
   writeln('Real92:  ', ord(rb > ra):5, ' s/b 0');
   writeln('Real93:  ', ord(ra <= rd):5, ' s/b 1');
   writeln('Real94:  ', ord(rb <= ra):5, ' s/b 1');
   writeln('Real95:  ', ord(ra <= rc):5, ' s/b 1');
   writeln('Real96:  ', ord(rd <= ra):5, ' s/b 0');
   writeln('Real97:  ', ord(ra <= rb):5, ' s/b 0');
   writeln('Real98:  ', ord(rd >= ra):5, ' s/b 1');
   writeln('Real99:  ', ord(ra >= rb):5, ' s/b 1');
   writeln('Real100: ', ord(ra >= rc):5, ' s/b 1');
   writeln('Real101: ', ord(ra >= rd):5, ' s/b 0');
   writeln('Real102: ', ord(rb >= ra):5, ' s/b 0');
   writeln('Real103: ', abs(ra):15, ' s/b  7.34200e+02');
   writeln('Real104: ', sqr(ra):15, ' s/b  5.39050e+05');
   writeln('Real105: ', sin(rb):15, ' s/b -4.34850e-01');
   writeln('Real106: ', arctan(ra):15, ' s/b -1.56943e+00');
   writeln('Real107: ', exp(re):15, ' s/b  6.80566e-01');
   writeln('Real108: ', trunc(ra):15, ' s/b -734');

   (* signed constants *)
   writeln('Real111: ', 45.934 + (-30.834):15, ' s/b  1.510000e+01');
   writeln('Real112: ', -25.737 + 70.87:15, ' s/b  4.513300e+01');
   writeln('Real113: ', -62.63 + 23.99:15, ' s/b -3.864000e+01');
   writeln('Real114: ', -20.733 + (-15.848):15, ' s/b -3.658100e+01');
   writeln('Real115: ', 20.774 - (-14.774):15, ' s/b  3.554800e+01');
   writeln('Real116: ', -34.523 - 14.8754:15, ' s/b -4.939840e+01');
   writeln('Real117: ', -56.664 - (-12.663):15, ' s/b -4.400100e+01');
   writeln('Real118: ', 5.663 * (-4.664):15, ' s/b -2.641223e+01');
   writeln('Real119: ', (-18.62) * 7.997:15, ' s/b -1.489041e+02');
   writeln('Real120: ', (-40.552) * (-13.774):15, ' s/b  5.585632e+02');
   writeln('Real121: ', 30.6632 / (-5.874):15, ' s/b -5.220157e+00');
   writeln('Real122: ', (-50.636) / 2.8573:15, ' s/b -1.772163e+01');
   writeln('Real123: ', (-20.7631) / (-4.85734):15, ' s/b  4.274582e+00');
   writeln('Real124: ', ord(-5.775 = -5.775):5, ' s/b 1');
   writeln('Real125: ', ord(-5.6364 = 5.8575):5, ' s/b 0');
   writeln('Real126: ', ord(-21.6385 <> -40.764):5, ' s/b 1');
   writeln('Real127: ', ord(-21.772 <> -21.772):5, ' s/b 0');
   writeln('Real128: ', ord(-3.512 < 5.8467):5, ' s/b 1');
   writeln('Real129: ', ord(-32.644 < -20.9074):5, ' s/b 1');
   writeln('Real130: ', ord(20.763 < -20.743):5, ' s/b 0');
   writeln('Real131: ', ord(-15.663 < -40.784):5, ' s/b 0');
   writeln('Real132: ', ord(70.766 > -4.974):5, ' s/b 1');
   writeln('Real133: ', ord(-23.6532 > -34.774):5, ' s/b 1');
   writeln('Real134: ', ord(-5.773 > 5.9874):5, ' s/b 0');
   writeln('Real135: ', ord(-60.663 > -59.78):5, ' s/b 0');
   writeln('Real136: ', ord(-12.542 <= 4.0848):5, ' s/b 1');
   writeln('Real137: ', ord(-14.8763 <= -5.0847):5, ' s/b 1');
   writeln('Real138: ', ord(-7.8373 <= -7.8373):5, ' s/b 1');
   writeln('Real139: ', ord(5.4564 <= -5.4564):5, ' s/b 0');
   writeln('Real140: ', ord(-10.72633 <= -20.984):5, ' s/b 0');
   writeln('Real141: ', ord(9.834 >= -3.9383):5, ' s/b 1');
   writeln('Real142: ', ord(-4.562 >= -10.74):5, ' s/b 1');
   writeln('Real143: ', ord(-13.63 >= -13.63):5, ' s/b 1');
   writeln('Real144: ', ord(-6.74 >= 6.74):5, ' s/b 0');
   writeln('Real145: ', ord(-20.7623 >= -10.574):5, ' s/b 0');
   writeln('Real146: ', abs(-6.823):15, ' s/b  6.823000e+00');
   writeln('Real147  ', sqr(-348.22):15, ' s/b  1.212572e+05');
   writeln('Real148: ', sin(-733.22):15, ' s/b  9.421146e-01');
   writeln('Real149: ', arctan(-8387.22):15, ' s/b -1.570677e+00');
   writeln('Real150: ', exp(-0.8743):15, ' s/b  4.171539e-01');
   writeln('Real151: ', trunc(-33.422):1, ' s/b -33');
   writeln('Real154: ', rscst:15, ' s/b -8.422000e+01');

(*******************************************************************************

                            Sets

*******************************************************************************)

   writeln;
   writeln('******************* sets ******************************');
   writeln;

   (* sets of integers *)
   write('Set1:  ');
   sta := [];       
   for i := 1 to 10 do if odd(i) then sta := sta+[i, i+10];
   for i := 1 to 20 do if i in sta then write('1') else write('0');
   write(' s/b ');
   writeln('10101010101010101010');
   write('Set2:  ');
   sta := [1, 4, 5];
   stb := [2, 6, 10];
   for i := 1 to 10 do if i in sta+stb then write('1') else write('0');
   write(' s/b ');
   writeln('1101110001');
   write('Set3:  ');
   sta := [1, 2, 6, 5, 7];
   stb := [2, 6, 10];
   for i := 1 to 10 do if i in sta*stb then write('1') else write('0');
   write(' s/b ');
   writeln('0100010000');
   write('Set4:  ');
   sta := [2, 4, 7, 8];
   stb := [1, 3, 4, 8, 10];
   for i := 1 to 10 do if i in sta-stb then write('1') else write('0');
   write(' s/b ');
   writeln('0100001000');
   sta := [4, 6, 8, 9];
   stb := [1, 4, 5, 9];
   stc := [4, 6, 8, 9];
   writeln('Set5:  ', ord(sta = stb):5, ' s/b 0');
   writeln('Set6:  ', ord(sta = stc):5, ' s/b 1');
   writeln('Set7:  ', ord(sta <> stb):5, ' s/b 1');
   writeln('Set8:  ', ord(sta <> stc):5, ' s/b 0');
   sta := [1, 2, 5, 7, 10];
   stb := [1, 5, 10];
   stc := [1, 5, 10, 6];
   std := [1, 2, 5, 7, 10];
   writeln('Set9:  ', ord(stb <= sta):5, ' s/b 1');
   writeln('Set10: ', ord(stb <= std):5, ' s/b 1');
   writeln('Set11: ', ord(stc <= sta):5, ' s/b 0');
   writeln('Set12: ', ord(sta >= stb):5, ' s/b 1');
   writeln('Set13: ', ord(std >= stb):5, ' s/b 1');
   writeln('Set14: ', ord(sta >= stc):5, ' s/b 0');
   write('Set15: ');
   i := 2;
   x := 4;
   sta := [i, x, i+x];
   for i := 1 to 10 do if i in sta then write('1') else write('0');
   write(' s/b ');
   writeln('0101010000');
   (* these are just compile time tests *)
   ste := std;
   stf := [1, 2, 5, 7];
   stg := stf;

   (* sets of characters *)
   write('Set16: ');
   csta := [];       
   for ci := 'a' to 'j' do 
      if odd(ord(ci)) then csta := csta+[ci, chr(ord(ci)+10)];
   for ci := 'a' to 't' do if ci in csta then write(ci) else write('_');
   write(' s/b ');
   writeln('a_c_e_g_i_k_m_o_q_s_');
   write('Set17: ');
   csta := ['a', 'c', 'f'];
   cstb := ['c', 'd', 'g'];
   for ci := 'a' to 'j' do if ci in csta+cstb then write(ci) else write('_');
   write(' s/b ');
   writeln('a_cd_fg___');
   write('Set18: ');
   csta := ['d', 'f', 'h', 'a'];
   cstb := ['a', 'b', 'i', 'h'];
   for ci := 'a' to 'j' do if ci in csta*cstb then write(ci) else write('_');
   write(' s/b ');
   writeln('a______h__');
   write('Set19: ');
   csta := ['b', 'd', 'i', 'j'];
   cstb := ['i', 'h', 'd', 'e'];
   for ci := 'a' to 'j' do if ci in csta-cstb then write(ci) else write('_');
   write(' s/b ');
   writeln('_b_______j');
   csta := ['b', 'd', 'h', 'j'];
   cstb := ['a', 'd', 'h', 'c'];
   cstc := ['b', 'd', 'h', 'j'];
   writeln('Set20: ', ord(csta = cstb):5, ' s/b 0');
   writeln('Set21: ', ord(csta = cstc):5, ' s/b 1');
   writeln('Set22: ', ord(csta <> cstb):5, ' s/b 1');
   writeln('Set23: ', ord(csta <> cstc):5, ' s/b 0');
   csta := ['a', 'b', 'f', 'g', 'j'];
   cstb := ['a', 'f', 'g'];
   cstc := ['a', 'f', 'g', 'h'];
   cstd := ['a', 'b', 'f', 'g', 'j'];
   writeln('Set24: ', ord(cstb <= csta):5, ' s/b 1');
   writeln('Set25: ', ord(cstb <= cstd):5, ' s/b 1');
   writeln('Set26: ', ord(cstc <= csta):5, ' s/b 0');
   writeln('Set27: ', ord(csta >= cstb):5, ' s/b 1');
   writeln('Set28: ', ord(cstd >= cstb):5, ' s/b 1');
   writeln('Set29: ', ord(csta >= cstc):5, ' s/b 0');
   write('Set30: ');
   ci := 'a';
   i := 4;
   csta := [ci, chr(ord(ci)+i)];
   for ci := 'a' to 'j' do if ci in csta then write(ci) else write('_');
   write(' s/b ');
   writeln('a___e_____');
   (* these are just compile time tests *)
   cste := cstd;
   cstf := ['a', 'b', 'e', 'f'];
   cstg := cstf;

   (* sets of enumerated *)
   write('Set31: ');
   sena := [];       
   for ei := one to ten do if odd(ord(ei)) then sena := sena+[ei];
   for ei := one to ten do if ei in sena then write('1') else write('0');
   write(' s/b ');
   writeln('0101010101');
   write('Set32: ');
   sena := [one, four, five];
   senb := [two, six, ten];
   for ei := one to ten do if ei in sena+senb then write('1') else write('0');
   write(' s/b ');
   writeln('1101110001');
   write('Set33: ');
   sena := [one, two, six, five, seven];
   senb := [two, six, ten];
   for ei := one to ten do if ei in sena*senb then write('1') else write('0');
   write(' s/b ');
   writeln('0100010000');
   write('Set34: ');
   sena := [two, four, seven, eight];
   senb := [one, three, four, eight, ten];
   for ei := one to ten do if ei in sena-senb then write('1') else write('0');
   write(' s/b ');
   writeln('0100001000');
   sena := [four, six, eight, nine];
   senb := [one, four, five, nine];
   senc := [four, six, eight, nine];
   writeln('Set35: ', ord(sena = senb):5, ' s/b 0');
   writeln('Set36: ', ord(sena = senc):5, ' s/b 1');
   writeln('Set37: ', ord(sena <> senb):5, ' s/b 1');
   writeln('Set38: ', ord(sena <> senc):5, ' s/b 0');
   sena := [one, two, five, seven, ten];
   senb := [one, five, ten];
   senc := [one, five, ten, six];
   send := [one, two, five, seven, ten];
   writeln('Set39: ', ord(senb <= sena):5, ' s/b 1');
   writeln('Set40: ', ord(senb <= send):5, ' s/b 1');
   writeln('Set41: ', ord(senc <= sena):5, ' s/b 0');
   writeln('Set42: ', ord(sena >= senb):5, ' s/b 1');
   writeln('Set43: ', ord(send >= senb):5, ' s/b 1');
   writeln('Set44: ', ord(sena >= senc):5, ' s/b 0');
   write('Set45: ');
   ei := two;
   sena := [ei, succ(ei)];
   for ei := one to ten do if ei in sena then write('1') else write('0');
   write(' s/b ');
   writeln('0110000000');
   (* these are just compile time tests *)
   send := [one, two, five];
   sene := send;
   senf := [one, two, five, seven];
   seng := senf;

   (* sets of boolean *)
(*

None of these will work because P4 cannot execute 
for b := false to true.

   write('Set46: ');
   sba := [];       
   for ba := false to true do if odd(ord(ba)) then sba := sba+[ba];
   for ba := false to true do if ba in sba then write('1') else write('0');
   write(' s/b ');
   writeln('01');
   write('Set47: ');
   sba := [false];
   sbb := [true];
   for ba := false to true do if ba in sba+sbb then write('1') else write('0');
   write(' s/b ');
   writeln('11');
   write('Set48: ');
   sba := [false, true];
   sbb := [false];
   for ba := false to true do if ba in sba*sbb then write('1') else write('0');
   write(' s/b ');
   writeln('10');
   write('Set49: ');
   sba := [true, false];
   sbb := [true];
   for ba := false to true do if ba in sba-sbb then write('1') else write('0');
   write(' s/b ');
   writeln('10');
 
*)
   sba := [true];
   sbb := [false];
   sbc := [true];
   writeln('Set50: ', ord(sba = sbb):5, ' s/b 0');
   writeln('Set51: ', ord(sba = sbc):5, ' s/b 1');
   writeln('Set52: ', ord(sba <> sbb):5, ' s/b 1');
   writeln('Set53: ', ord(sba <> sbc):5, ' s/b 0');
   sba := [true, false];
   sbb := [false];
   sbc := [true];
   sbd := [false];
   writeln('Set54: ', ord(sbb <= sba):5, ' s/b 1');
   writeln('Set55: ', ord(sbb <= sbd):5, ' s/b 1');
   writeln('Set56: ', ord(sbc <= sbb):5, ' s/b 0');
   writeln('Set57: ', ord(sba >= sbb):5, ' s/b 1');
   writeln('Set58: ', ord(sbd >= sbb):5, ' s/b 1');
   writeln('Set59: ', ord(sbb >= sbc):5, ' s/b 0');
   
(*
 Same as above
 
   write('Set60: ');
   ba := false;
   sba := [ba, succ(ba)];
   for ba := false to true do if ba in sba then write('1') else write('0');
   write(' s/b ');
   writeln('11');

*)
   (* these are just compile time tests *)
   sbe := sbd;
   sbf := [true];
   sbg := sbf;

(*******************************************************************************

                            Pointers

*******************************************************************************)

   writeln;
   writeln('******************* Pointers ******************************');
   writeln;

   (* pointers to types *)
   write('Pointer1:   ');
   new(pti);
   pti^ := 4594;
   writeln(pti^:1, ' s/b 4594');
   write('Pointer2:   ');
   new(ptb);
   ptb^ := true;
   writeln(ord(ptb^):5, ' s/b  1');
   write('Pointer3:   ');
   new(ptb);
   ptb^ := false;
   writeln(ord(ptb^):5, ' s/b 0');
   write('Pointer4:   ');
   new(ptc);
   ptc^ := 'p';
   writeln(ord(ptc^), ' s/b 112');
   write('Pointer5:   ');
   new(pte);
   pte^ := six;
   writeln(ord(ptb^):1, ' s/b 5');
   write('Pointer6:   ');
   new(ptes);
   ptes^ := four;
   writeln(ord(ptes^):1, ' s/b 3');
   write('Pointer7:   ');
   new(pts);
   pts^ := 17;
   writeln(pts^:1, ' s/b 17');
   write('Pointer9:   ');
   new(ptst);
   ptst^ := 'my word is';
   writeln(ptst^, ' s/b my word is');
   write('Pointer10:  ');
   new(pta);
   for i := 1 to 10 do pta^[i] := i+10;
   for i := 10 downto 1 do write(pta^[i]:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   write('Pointer11:   ');
   new(ptrc);
   ptrc^.a := 7234;
   ptrc^.b := 'y';
   writeln(ptrc^.a:1, ' ', ptrc^.b, ' s/b 7234 y');
   write('Pointer12:   ');
   new(ptstc);
   ptstc^ := ['b', 'd', 'i', 'j'];
   for ci := 'a' to 'j' do if ci in ptstc^ then write(ci) else write('_');
   writeln(' s/b _b_d____ij');
   write('Pointer13:  ');
   new(ptp);
   new(ptp^);
   ptp^^ := 3732;
   writeln(ptp^^:1, ' s/b 3732');

   (* equality/inequality, nil *)
   write('Pointer14:  ');
   pti := nil;
   writeln(ord(pti = nil):5, ' s/b 1');
   write('Pointer15:  ');
   new(pti);
   writeln(ord(pti = nil):5, ' s/b 0');
   write('Pointer16:  ');
   pti1 := pti;
   writeln(ord(pti = pti1):5, ' s/b 1');
   write('Pointer17:  ');
   pti1 := pti;
   writeln(ord(pti <> pti1):5, ' s/b 0');
   write('Pointer18:  ');
   new(pti1);
   writeln(ord(pti = pti1):5, ' s/b 0');
   write('Pointer19:  ');
   writeln(ord(pti <> pti1):5, ' s/b 1');

(*******************************************************************************

                            Arrays

*******************************************************************************)

   writeln;
   writeln('******************* arrays ******************************');
   writeln;
  
   (* single demension, integer index *)
   write('Array1:   ');
   for i := 1 to 10 do avi[i] := i+10;
   for i := 10 downto 1 do write(avi[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array2:   ');
   for i := 1 to 10 do pavi[i] := i+10;
   for i := 10 downto 1 do write(pavi[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array3:   ');
   for i := 1 to 10 do avis[i] := i+10;
   for i := 10 downto 1 do write(avis[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array4:   ');
   for i := 1 to 10 do pavis[i] := i+10;
   for i := 10 downto 1 do write(pavis[i]:1, ' ');
   writeln(' s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array5:   ');
   for i := 1 to 10 do avb[i] := odd(i);
   for i := 10 downto 1 do write(ord(avb[i]):5, ' ');
   writeln;
   writeln('    s/b:    0   1   0    1    0    1   0   1   0',
           '  1');
   write('Array6:   ');
   for i := 1 to 10 do pavb[i] := odd(i);
   for i := 10 downto 1 do write(ord(pavb[i]):5, ' ');
   writeln;
   writeln('    s/b:    0   1   0    1   0    1   0    1   0',
           '  1');
   write('Array7:   ');
   for i := 1 to 10 do avr[i] := i+10+0.12;
   for i := 10 downto 1 do write(avr[i]:1, ' ');
   writeln;
   writeln('    s/b:   2.0e+001  1.9e+001  1.8e+001  1.7e+001  1.6e+001  ',
           '1.5e+001  1.4e+001  1.3e+001  1.2e+001  1.1e+001');
   write('Array8:   ');
   for i := 1 to 10 do pavr[i] := i+10+0.12;
   for i := 10 downto 1 do write(pavr[i]:1, ' ');
   writeln;
   writeln('    s/b:   2.0e+001  1.9e+001  1.8e+001  1.7e+001  1.6e+001  ',
           '1.5e+001  1.4e+001  1.3e+001  1.2e+001  1.1e+001');
   write('Array9:   ');
   for i := 1 to 10 do avc[i] := chr(i+ord('a'));
   for i := 10 downto 1 do write(avc[i]:1, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array10:  ');
   for i := 1 to 10 do pavc[i] := chr(i+ord('a'));
   for i := 10 downto 1 do write(pavc[i]:1, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array11:  ');
   for i := 1 to 10 do avcs[i] := chr(i+ord('f'));
   for i := 10 downto 1 do write(avcs[i]:1, ' ');
   writeln('s/b p o n m l k j i h g');
   write('Array12:  ');
   for i := 1 to 10 do pavcs[i] := chr(i+ord('f'));
   for i := 10 downto 1 do write(pavcs[i]:1, ' ');
   writeln('s/b p o n m l k j i h g');
   write('Array13:  ');
   for ei := one to ten do ave[ord(ei)+1] := ei;
   for ei := ten downto one do write(ord(ave[ord(ei)+1]):1, ' ');
   writeln('s/b 9 8 7 6 5 4 3 2 1 0');
   write('Array14:  ');
   for ei := one to ten do pave[ord(ei)+1] := ei;
   for ei := ten downto one do write(ord(ave[ord(ei)+1]):1, ' ');
   writeln('s/b 9 8 7 6 5 4 3 2 1 0');
   write('Array15:  ');
   for ei := three to six do aves[ord(ei)+1] := ei;
   for ei := six downto three do write(ord(aves[ord(ei)+1]):1, ' ');
   writeln('s/b 5 4 3 2');
   write('Array16:  ');
   for ei := three to six do paves[ord(ei)+1] := ei;
   for ei := six downto three do write(ord(paves[ord(ei)+1]):1, ' ');
   writeln('s/b 5 4 3 2');
   write('Array17:  ');
   for i := 1 to 10 do avs[i] := [chr(i+ord('a'))];
   for i := 10 downto 1 do
      for ci := 'a' to 'z' do if ci in avs[i] then write(ci, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array18:  ');
   for i := 1 to 10 do pavs[i] := [chr(i+ord('a'))];
   for i := 10 downto 1 do
      for ci := 'a' to 'z' do if ci in pavs[i] then write(ci, ' ');
   writeln('s/b k j i h g f e d c b');
   write('Array19:  ');
   for i := 1 to 10 do 
      begin avrc[i].a := i+10; avrc[i].b := chr(i+ord('a')) end;
   for i := 10 downto 1 do write(avrc[i].a:1, ' ', avrc[i].b, ' ');
   writeln;
   writeln('    s/b:  20 k 19 j 18 i 17 h 16 g 15 f 14 e 13 d 12 c 11 b');
   write('Array20:  ');
   for i := 1 to 10 do 
      begin pavrc[i].a := i+10; pavrc[i].b := chr(i+ord('a')) end;
   for i := 10 downto 1 do write(pavrc[i].a:1, ' ', pavrc[i].b, ' ');
   writeln;
   writeln('    s/b:  20 k 19 j 18 i 17 h 16 g 15 f 14 e 13 d 12 c 11 b');
   write('Array23:  ');
   for i := 1 to 10 do begin new(avp[i]); avp[i]^ := i+10 end;
   for i := 10 downto 1 do write(avp[i]^:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   write('Array24:  ');
   for i := 1 to 10 do begin new(pavp[i]); pavp[i]^ := i+10 end;
   for i := 10 downto 1 do write(pavp[i]^:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');

   (* indexing tests *)
   write('Array27:  ');
   for ci := 'a' to 'j' do cia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(cia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array28:  ');
   for ci := 'a' to 'j' do pcia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(pcia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array29:  ');
   for ci := 'a' to 'j' do csia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(csia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array30:  ');
   for ci := 'a' to 'j' do pcsia[ci] := ord(ci);
   for ci := 'j' downto 'a' do write(chr(pcsia[ci]), ' ');
   writeln(' s/b  j i h g f e d c b a');
   write('Array31:  ');
   for ei := one to ten do eia[ei] := ord(ei);
   for ei := ten downto one do write(eia[ei]:1, ' ');
   writeln(' s/b  9 8 7 6 5 4 3 2 1 0');
   write('Array32:  ');
   for ei := one to ten do peia[ei] := ord(ei);
   for ei := ten downto one do write(peia[ei]:1, ' ');
   writeln(' s/b  9 8 7 6 5 4 3 2 1 0');
   write('Array33:  ');
   for ei := two to six do eia[ei] := ord(ei);
   for ei := six downto two do write(eia[ei]:1, ' ');
   writeln(' s/b  5 4 3 2 1');
   write('Array34:  ');
   for ei := two to six do peia[ei] := ord(ei);
   for ei := six downto two do write(peia[ei]:1, ' ');
   writeln(' s/b  5 4 3 2 1');

   (* multidementional arrays *)
   writeln('Array35:');
   z := 0;
   for x := 1 to 10 do
      for y := 1 to 10 do begin da[y, x] := z; z := z + 1 end;
   for x := 1 to 10 do
   begin
      for y := 1 to 10 do write(da[x][y]:2, ' ');
      writeln;
   end;
   writeln('s/b');
   writeln('0 10 20 30 40 50 60 70 80 90');
   writeln('1 11 21 31 41 51 61 71 81 91'); 
   writeln('2 12 22 32 42 52 62 72 82 92'); 
   writeln('3 13 23 33 43 53 63 73 83 93'); 
   writeln('4 14 24 34 44 54 64 74 84 94'); 
   writeln('5 15 25 35 45 55 65 75 85 95'); 
   writeln('6 16 26 36 46 56 66 76 86 96'); 
   writeln('7 17 27 37 47 57 67 77 87 97'); 
   writeln('8 18 28 38 48 58 68 78 88 98'); 
   writeln('9 19 29 39 49 59 69 79 89 99'); 
   writeln('Array36: ');
   t := 0;
   for i := 1 to 2 do
      for x := 1 to 2 do
         for y := 1 to 2 do
            for z := 1 to 2 do
               for q := 1 to 2 do
                  for n := 1 to 2 do 
                     begin mdar[i][x, y, z][q][n] := t; t := t+1 end;
   for i := 2 downto 1 do
      for x := 2 downto 1 do
         for y := 2 downto 1 do begin

            for z := 2 downto 1 do
               for q := 2 downto 1 do
                  for n := 2 downto 1 do write(mdar[i, x][y, z][q][n]:2, ' ');
            writeln;

         end;
   writeln('s/b:');
   writeln('63 62 61 60 59 58 57 56');
   writeln('55 54 53 52 51 50 49 48');
   writeln('47 46 45 44 43 42 41 40');
   writeln('39 38 37 36 35 34 33 32');
   writeln('31 30 29 28 27 26 25 24');
   writeln('23 22 21 20 19 18 17 16');
   writeln('15 14 13 12 11 10  9  8');
   writeln(' 7  6  5  4  3  2  1  0');

   (* assignments *)
   writeln('Array37: ');
   pavc := 'hello, guy';
   writeln(pavc, ' s/b hello, guy');
   writeln('Array38: ');
   for i := 1 to 10 do avi[i] := i+10;
   avi2 := avi;
   for i := 10 downto 1 do write(avi2[i]:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');
   writeln('Array39: ');
   t := 0;
   for i := 1 to 2 do
      for x := 1 to 2 do
         for y := 1 to 2 do
            for z := 1 to 2 do
               for q := 1 to 2 do
                  for n := 1 to 2 do 
                     begin mdar[i][x, y, z][q][n] := t; t := t+1 end;
   mdar2 := mdar;
   for i := 2 downto 1 do
      for x := 2 downto 1 do
         for y := 2 downto 1 do begin

            for z := 2 downto 1 do
               for q := 2 downto 1 do
                  for n := 2 downto 1 do write(mdar2[i, x][y, z][q][n]:2, ' ');
            writeln;

         end;
   writeln('s/b:');
   writeln('63 62 61 60 59 58 57 56');
   writeln('55 54 53 52 51 50 49 48');
   writeln('47 46 45 44 43 42 41 40');
   writeln('39 38 37 36 35 34 33 32');
   writeln('31 30 29 28 27 26 25 24');
   writeln('23 22 21 20 19 18 17 16');
   writeln('15 14 13 12 11 10  9  8');
   writeln(' 7  6  5  4  3  2  1  0');

(*******************************************************************************

                            Records

*******************************************************************************)

   writeln;
   writeln('******************* records ******************************');
   writeln;

   (* types in records *)
   writeln('Record1:   ');
   arec.i := 64;
   arec.b := false;
   arec.c := 'j';
   arec.e := two;
   arec.es := four;
   arec.s := 12;
   arec.r := 4545.12e-32;
   arec.st := 'what ? who';
   for i := 1 to 10 do arec.a[i] := i+20;
   arec.rc.a := 2324;
   arec.rc.b := 'y';
   arec.stc := ['b', 'c', 'd', 'e', 'i'];
   new(arec.p);
   arec.p^ := 8454;
   writeln(arec.i:1, ' ', ord(arec.b):5, ' ', arec.c:1, ' ', ord(arec.e):1, ' ', 
           ord(arec.es):1, 
           ' ', arec.s:1, ' ', arec.r:15, ' ', arec.st);
   for i := 1 to 10 do write(arec.a[i]:1, ' '); writeln;
   writeln(arec.rc.a:1, ' ', arec.rc.b:1);
   for ci := 'a' to 'j' do if ci in arec.stc then write(ci) else write('_');
   writeln;
   writeln(arec.p^:1);
   writeln('s/b:');
   writeln('64 false j 1 3 12  4.54512000e-29 what ? who'); 
   writeln('21 22 23 24 25 26 27 28 29 30');
   writeln('2324 y');
   writeln('_bcde___i_');
   writeln('8454');
   writeln('Record2:   ');
   parec.i := 64;
   parec.b := false;
   parec.c := 'j';
   parec.e := two;
   parec.es := four;
   parec.s := 12;
   parec.r := 4545.12e-32;
   parec.st := 'what ? who';
   for i := 1 to 10 do parec.a[i] := i+20;
   parec.rc.a := 2324;
   parec.rc.b := 'y';
   parec.stc := ['b', 'c', 'd', 'e', 'i'];
   new(parec.p);
   parec.p^ := 8454;
   writeln(parec.i:1, ' ', ord(parec.b):5, ' ', parec.c:1, ' ', ord(parec.e):1, ' ', 
           ord(parec.es):1, 
           ' ', parec.s:1, ' ', parec.r:15, ' ', parec.st);
   for i := 1 to 10 do write(parec.a[i]:1, ' '); writeln;
   writeln(parec.rc.a:1, ' ', parec.rc.b:1);
   for ci := 'a' to 'j' do if ci in parec.stc then write(ci) else write('_');
   writeln;
   writeln(parec.p^:1);
   writeln('s/b:');
   writeln('64 false j 1 3 12  4.54512000e-29 what ? who'); 
   writeln('21 22 23 24 25 26 27 28 29 30');
   writeln('2324 y');
   writeln('_bcde___i_');
   writeln('8454');

   (* types in variants, and border clipping *)
   write('Record3:   ');
   vra.i := 873;
   vra.vt := vti;
   vra.a := 427;
   vra.vdi := 235;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdi:1, ' ', vra.a:1);
   writeln(' s/b 873 0 235 427');
   write('Record4:   ');
   vra.i := 873;
   vra.vt := vtb;
   vra.b := 427;
   vra.vdb := true;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', ord(vra.vdb):5, ' ', vra.b:1);
   writeln(' s/b 873 1  1 427');
   write('Record5:   ');
   vra.i := 873;
   vra.vt := vtc;
   vra.c := 427;
   vra.vdc := 'f';
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdc, ' ', vra.c:1);
   writeln(' s/b 873 2 f 427');
   write('Record6:   ');
   vra.i := 873;
   vra.vt := vte;
   vra.d := 427;
   vra.vde := nine;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', ord(vra.vde):1, ' ', vra.d:1);
   writeln(' s/b 873 3 8 427');
   write('Record7:   ');
   vra.i := 873;
   vra.vt := vtes;
   vra.e := 427;
   vra.vdes := four;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', ord(vra.vdes):1, ' ', vra.e:1);
   writeln(' s/b 873 4 3 427');
   write('Record8:   ');
   vra.i := 873;
   vra.vt := vts;
   vra.f := 427;
   vra.vds := 12;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vds:1, ' ', vra.f:1);
   writeln(' s/b 873 5 12 427');
   write('Record9:   ');
   vra.i := 873;
   vra.vt := vtr;
   vra.g := 427;
   vra.vdr := 8734.8389;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdr:1, ' ', vra.g:1);
   writeln(' s/b 873 6 8734.8389 427');
   write('Record10:  ');
   vra.i := 873;
   vra.vt := vtst;
   vra.h := 427;
   vra.vdst := 'this one ?';
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdst, ' ', vra.h:1);
   writeln(' s/b 873 7 this one ? 427');
   write('Record11:  ');
   vra.i := 873;
   vra.vt := vta;
   vra.j := 427;
   for i := 1 to 10 do vra.vda[i] := i+10;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ');
   for i := 10 downto 1 do write(vra.vda[i]:1, ' ');
   writeln(vra.j:1);
   writeln('     s/b:  873 8 20 19 18 17 16 15 14 13 12 11 427');
   write('Record12:  ');
   vra.i := 873;
   vra.vt := vtrc;
   vra.k := 427;
   vra.vdrc.a := 2387;
   vra.vdrc.b := 't';
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdrc.a:1, ' ', vra.vdrc.b, ' ',
         vra.k:1);
   writeln(' s/b:  873 9 2387 t 427');
   write('Record13:  ');
   vra.i := 873;
   vra.vt := vtstc;
   vra.l := 427;
   vra.vdstc := ['b', 'c', 'd', 'e', 'f', 'g', 'i'];
   write(vra.i:1, ' ', ord(vra.vt):1, ' ');
   for ci := 'j' downto 'a' do if ci in vra.vdstc then write(ci) else write('_');
   writeln(' ', vra.l:1);
   writeln('     s/b:  873 10 _i_gfedcb_ 427');
   write('Record14:  ');
   vra.i := 873;
   vra.vt := vtp;
   vra.m := 427;
   new(vra.vdp);
   vra.vdp^ := 2394;
   write(vra.i:1, ' ', ord(vra.vt):1, ' ', vra.vdp^:1, ' ', vra.m:1);
   writeln(' s/b 873 11 2394 427');

   (* types of variant tags *)
   write('Record15:  ');
   vvrs.vt := 10;
   vvrs.vi := 2343;
   write(vvrs.vt:1, ' ', vvrs.vi:1);
   writeln(' s/b 10 2343');
   write('Record16:  ');
   vvrs.vt := 19;
   vvrs.vb := true;
   write(vvrs.vt:1, ' ', ord(vvrs.vb):5);
   writeln(' s/b 19  1');
   write('Record17:  ');
   vvrb.vt := true;
   vvrb.vi := 2343;
   write(ord(vvrb.vt):5, ' ', vvrb.vi:1);
   writeln(' s/b  true 2343');
   write('Record18:  ');
   vvrb.vt := false;
   vvrb.vb := true;
   write(ord(vvrb.vt):5, ' ', ord(vvrb.vb):5);
   writeln(' s/b 0  1');
   write('Record19:  ');
   vvre.vt := three;
   vvre.vi := 2343;
   write(ord(vvre.vt):1, ' ', vvre.vi:1);
   writeln(' s/b 2 2343');
   write('Record20:  ');
   vvre.vt := eight;
   vvre.vb := true;
   write(ord(vvre.vt):1, ' ', ord(vvre.vb):5);
   writeln(' s/b 7  1');
   write('Record21:  ');
   vvres.vt := four;
   vvres.vi := 2343;
   write(ord(vvres.vt):1, ' ', vvres.vi:1);
   writeln(' s/b 3 2343');
   write('Record22:  ');
   vvres.vt := five;
   vvres.vb := true;
   write(ord(vvres.vt):1, ' ', ord(vvres.vb):5);
   writeln(' s/b 4  1');

   (* nested records *)
   write('Record23:  ');
   nvr.i := 1;
   nvr.r.i := 2;
   nvr.r.r.i := 3;
   nvr.r.r.r.i := 4;
   nvr.r.r.r.r.i := 5;
   nvr.r.r.r.r.r.i := 6;
   nvr.r.r.r.r.r.r.i := 7;
   nvr.r.r.r.r.r.r.r.i := 8;
   nvr.r.r.r.r.r.r.r.r.i := 9;
   nvr.r.r.r.r.r.r.r.r.r.i := 10;
   writeln(nvr.i:1, ' ', 
           nvr.r.i:1, ' ', 
           nvr.r.r.i:1, ' ',
           nvr.r.r.r.i:1, ' ', 
           nvr.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.i:1, ' ', 
           nvr.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.r.i:1, ' ',
           's/b 1 2 3 4 5 6 7 8 9 10');

   (* 'with' statements *)
   write('Record24:  ');
   with nvr do begin

      i := 10;
      with r do begin

         i := 9;
         with r do begin

            i := 8;
            with r do begin

               i := 7;
               with r do begin

                  i := 6;
                  with r do begin

                     i := 5;
                     with r do begin

                        i := 4;
                        with r do begin

                           i := 3;
                           with r do begin

                              i := 2;
                              with r do begin

                                 i := 2;
                                 with r do begin

                                    i := 1

                                 end

                              end

                           end

                        end

                     end

                  end

               end

            end

         end

      end

   end;
   writeln(nvr.i:1, ' ', 
           nvr.r.i:1, ' ', 
           nvr.r.r.i:1, ' ',
           nvr.r.r.r.i:1, ' ', 
           nvr.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.i:1, ' ', 
           nvr.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.r.i:1, ' ',
           's/b 10 9 8 7 6 5 4 3 2 1');
   write('Record25:  ');
   with nvr, r, r, r, r, r, r, r, r, r do i := 76;
   writeln(nvr.i:1, ' ', 
           nvr.r.i:1, ' ', 
           nvr.r.r.i:1, ' ',
           nvr.r.r.r.i:1, ' ', 
           nvr.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.i:1, ' ', 
           nvr.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.i:1, ' ',
           nvr.r.r.r.r.r.r.r.r.r.i:1, ' ',
           's/b 10 9 8 7 6 5 4 3 2 76');
   write('Record26:  ');
   new(rpa);
   with rpa^ do begin

      i := 1;
      with rc do b := 'g'
 
   end;
   writeln(rpa^.i:1, ' ', rpa^.rc.b, ' s/b 1 g');
   write('Record27:  ');
   for i := 1 to 10 do with ara[i] do a := i+10;
   for i := 10 downto 1 do with ara[i] do write(a:1, ' ');
   writeln('s/b 20 19 18 17 16 15 14 13 12 11');

(*******************************************************************************

                         Procedures and functions

*******************************************************************************)
 
   writeln;
   writeln('************ Procedures and functions ******************');
   writeln;
   write('ProcedureFunction1:   ');
   x := 45; y := 89;
   junk1(x, y);
   writeln(' s/b 45 89');
   write('ProcedureFunction2:   ');
   x := 45; junk2(x);
   writeln(x:1, ' s/b 46');
   write('ProcedureFunction3:   ');
   s := 'total junk';
   junk3(s);
   writeln(' s/b total junk');
   write('ProcedureFunction4:   ');
   s := 'total junk';
   junk4(s);
   writeln(' s/b tota? junk');
   writeln(s, ' s/b total junk');
   write('ProcedureFunction5:   ');
   writeln(junk5(34):1, ' s/b 35');
   write('ProcedureFunction6:   ');
   i := junk7(10, 9, 8);
   writeln(' ', i:1);
   writeln('             s/b:    10 9 8 6 5 4 3 2 1 78');
   writeln('ProcedureFunction7:');
   for i := 1 to 10 do ai[i] := i+10;
   arec.i := 64;
   arec.b := false;
   arec.c := 'j';
   arec.e := two;
   arec.es := four;
   arec.s := 12;
   arec.r := 4545.12e-32;
   arec.st := 'what ? who';
   for i := 1 to 10 do arec.a[i] := i+20;
   arec.rc.a := 2324;
   arec.rc.b := 'y';
   arec.stc := ['b', 'c', 'd', 'e', 'i'];
   new(arec.p);
   arec.p^ := 8454;
   vrec.a := 23487;
   vrec.b := 'n';
   vrec.c := false;
   vrec.d := 'help me123';
   new(ip);
   ip^ := 734;
   junk8(93, true, 'k', eight, five, 10, 3.1414, 'hello, guy', ai, arec, vrec,
         ['a', 'b', 'c', 'd', 'h'], ip); 
   writeln('s/b:');
   writeln('93  1 k 7 4 10  3.14140000e+00 hello, guy');
   writeln('11 12 13 14 15 16 17 18 19 20');
   writeln('64 0 j 1 3 12  4.54500000e-29 what ? who'); 
   writeln('21 22 23 24 25 26 27 28 29 30');
   writeln('2324 y');
   writeln('_bcde___i_');
   writeln('8454');
   writeln('23487 n 0');
   writeln('help me123');
   writeln('abcd___h__');
   writeln('734');
   write('ProcedureFunction10:   ');
   junk14;
   writeln(' s/b 62 76');

end.
