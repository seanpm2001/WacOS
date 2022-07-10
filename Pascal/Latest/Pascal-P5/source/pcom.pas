(*$l-,p*)
{*******************************************************************************
*                                                                              *
*                         PASCAL-P5 PORTABLE INTERPRETER                       *
*                                                                              *
* LICENSING:                                                                   *
*                                                                              *
* Copyright (c) 2020, Scott A. Franco                                          *
* All rights reserved.                                                         *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
* 1. Redistributions of source code must retain the above copyright notice,    *
*    this list of conditions and the following disclaimer.                     *
* 2. Redistributions in binary form must reproduce the above copyright         *
*    notice, this list of conditions and the following disclaimer in the       *
*    documentation and/or other materials provided with the distribution.      *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"  *
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE    *
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE   *
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE     *
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR          *
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF         *
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS     *
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN      *
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)      *
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE   *
* POSSIBILITY OF SUCH DAMAGE.                                                  *
*                                                                              *
* The views and conclusions contained in the software and documentation are    *
* those of the authors and should not be interpreted as representing official  *
* policies, either expressed or implied, of the Pascal-P5 project.             *
*                                                                              *
*                     Portable Pascal assembler/interpreter                    *
*                     *************************************                    *
*                                                                              *
*                                 Pascal P5                                    *
*                                                                              *
*                                 ETH May 76                                   *
*                                                                              *
* Authors:                                                                     *
*    Urs Ammann                                                                *
*    Kesav Nori                                                                *
*    Christian Jacobi                                                          *
*    K. Jensen                                                                 *
*    N. Wirth                                                                  *
*                                                                              *
*    Address:                                                                  *
*       Institut Fuer Informatik                                               *
*       Eidg. Technische Hochschule                                            *
*       CH-8096 Zuerich                                                        *
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
*    Scott A. Franco                                                           *
*    samiam@moorecad.com                                                       *
*                                                                              *
*    The comments marked with brackets are mine [saf]                          *
*                                                                              *
* Please see accompanying documentation concerning this software.              *
*                                                                              *
* Note that the previous version of P4 added some type specified instructions  *
* that used to be unified, typeless instructions.                              *
*                                                                              *
* P5 errors added:                                                             *
*                                                                              *
* 182 identifier too long                                                      *
* 183 For index variable must be local to this block                           *
* 184 Interprocedure goto does not reference outter block of destination       *
* 185 Goto references deeper nested statement                                  *
* 186 Label referenced by goto at lesser statement level                       *
* 187 Goto references label in different nested statement                      *
* 188 Label referenced by goto in different nested statement                   *
* 189 Parameter lists of formal and actual parameters not congruous.           *
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
* not represent files as full1y expressed variables, such as within an array   *
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
* ---------------------------------------------------------------------------- *
*                                                                              *
*                                   LICENSE                                    *
*                                                                              *
* ---------------------------------------------------------------------------- *
*                                                                              *
* This software is based on, and represents an enhanced version, of Pascal-P4, *
* and was enhanced from that version substantially.                            *
*                                                                              *
* Pascal-P4 is unlicensed and exists in the public domain. It has:             *
*                                                                              *
* 1. Been acknowledged as public domain by the author, Niklaus Wirth at ETH    *
*    Zurich.                                                                   *
*                                                                              *
* 2. Has been freely distributed since 1976 with only charges for printing and *
*    shipping costs.                                                           *
*                                                                              *
* 3. Has been used as the basis for many projects, both paid and free, by      *
*    other authors.                                                            *
*                                                                              *
* I, Scott Franco, have extensively expanded the original software. The        *
* the changes made by me are held in copyright by me and released under the    *
* BSD "2-clause" license, the least restrictive open source license available. *
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

program pascalcompiler(output,prd,prr);

label 99; { terminate immediately }

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

   displimit   = 300;
   maxlevel    = 255;
   { strglgth used to define the size of all strings in pcom and pint. With the
     string quanta system, string lengths are effectively unlimited, but there
     it still sets the size of some buffers in pcom. }
   strglgth    = 250;
   { maximum number of digits in real, including sign and exponent }
   digmax      = 250;
   { lcaftermarkstack is a very pcom specific way of stating the size of a mark
     in pint. However, it is used frequently in Perberton's documentation, so I
     left it, but equated it to the more portable marksize. }
   lcaftermarkstack = -marksize;
   (* stackelsize = minimum size for 1 stackelement
                  = k*stackal
      stackal     = scm(all other al-constants)
      charmax     = scm(charsize,charal)
                    scm = smallest common multiple
      lcaftermarkstack >= maxresult+3*ptrsize+max(x-size)
                        = k1*stackelsize          *)
   parmal     = stackal;
   parmsize   = stackelsize;
   recal      = stackal;
   maxaddr    =  maxint;
   maxsp      = 49;   { number of standard procedures/functions }
   maxins     = 85;   { maximum number of instructions }
   maxids     = 250;  { maximum characters in id string (basically, a full line) }
   maxstd     = 39;   { number of standard identifiers }
   maxres     = 35;   { number of reserved words }
   reslen     = 9;    { maximum length of reserved words }
   varsqt     = 10;   { variable string quanta }
   prtlln     = 10;   { number of label characters to print in dumps }
   varmax     = 1000; { maximum number of logical variants to track }

   { default field sizes for write }
   intdeff    = 11; { default field length for integer }
   reldeff    = 22; { default field length for real }
   chrdeff    = 1;  { default field length for char (usually 1) }
   boldeff    = 5;  { default field length for boolean (usually 5 for 'false' }

   { version numbers }

   majorver   = 1; { major version number }
   minorver   = 4; { minor version number }
   experiment = false; { is version experimental? }

type                                                        (*describing:*)
                                                            (*************)

     {marktype= ^integer;}
                                                            (*basic symbols*)
                                                            (***************)

     symbol = (ident,intconst,realconst,stringconst,notsy,mulop,addop,relop,
               lparent,rparent,lbrack,rbrack,comma,semicolon,period,arrow,
               colon,becomes,range,labelsy,constsy,typesy,varsy,funcsy,progsy,
               procsy,setsy,packedsy,arraysy,recordsy,filesy,beginsy,ifsy,
               casesy,repeatsy,whilesy,forsy,withsy,gotosy,endsy,elsesy,untilsy,
               ofsy,dosy,tosy,downtosy,thensy,nilsy,othersy);
     operatort = (mul,rdiv,andop,idiv,imod,plus,minus,orop,ltop,leop,geop,gtop,
                  neop,eqop,inop,noop);
     setofsys = set of symbol;
     chtp = (letter,number,special,illegal,
             chstrquo,chcolon,chperiod,chlt,chgt,chlparen,chspace,chlcmt);
     { Here is the variable length string containment to save on space. strings
       strings are only stored in their length rounded to the nearest 10th. }
     strvsp = ^strvs; { pointer to variable length id string }
     strvs = record { id string variable length }
                 str:   packed array [1..varsqt] of char; { data contained }
                 next:  strvsp { next }
               end;

                                                            (*constants*)
                                                            (***********)
     setty = set of setlow..sethigh;
     cstclass = (reel,pset,strg);
     csp = ^ constant;
     constant = record
                       next: csp; { next entry link }
                       case cclass: cstclass of
                         reel: (rval: strvsp);
                         pset: (pval: setty);
                         strg: (slgth: 0..strglgth; sval: strvsp)
                       end;

     valu = record case intval: boolean of
                     true:  (ival: integer);
                     false: (valp: csp)
                   end;

                                                           (*data structures*)
                                                           (*****************)
     levrange = 0..maxlevel; addrrange = -maxaddr..maxaddr; stkoff = -maxaddr..0;
     structform = (scalar,subrange,pointer,power,arrays,records,files,
                   tagfld,variant);
     declkind = (standard,declared);
     varinx = 0..varmax;
     vartbl = array [0..varmax] of integer; { variant value to logical table }
     vartpt = ^vartbl;
     stp = ^ structure;
     ctp = ^ identifier;

     structure = record
                   snm: integer; { serial number }
                   next: stp; { next entry link }
                   marked: boolean;   (*for test phase only*)
                   size: addrrange;
                   packing: boolean; { packing status }
                   case form: structform of
                     scalar:   (case scalkind: declkind of
                                  declared: (fconst: ctp); standard: ());
                     subrange: (rangetype: stp; min,max: valu);
                     pointer:  (eltype: stp);
                     power:    (elset: stp; matchpack: boolean);
                     arrays:   (aeltype,inxtype: stp);
                     records:  (fstfld: ctp; recvar: stp; recyc: stp);
                     files:    (filtype: stp);
                     tagfld:   (tagfieldp: ctp; fstvar: stp; vart: vartpt;
                                varts: varinx);
                     variant:  (nxtvar,subvar,caslst: stp; varval: valu;
                                varln: integer)
                   end;

                                                            (*names*)
                                                            (*******)

     idclass = (types,konst,vars,field,proc,func);
     setofids = set of idclass;
     idkind = (actual,formal);
     idstr = packed array [1..maxids] of char;
     restr = packed array [1..reslen] of char;
     nmstr = packed array [1..digmax] of char;
     csstr = packed array [1..strglgth] of char;
     identifier = record
                   snm: integer; { serial number }
                   name: strvsp; lastuse: integer; llink, rlink: ctp;
                   idtype: stp; next: ctp; keep: boolean; refer: boolean;
                   defined: boolean;
                   case klass: idclass of
                     types: ();
                     konst: (values: valu);
                     vars:  (vkind: idkind; vlev: levrange; vaddr: addrrange;
                             threat: boolean; forcnt: integer);
                     field: (fldaddr: addrrange; varnt: stp; varlb: ctp;
                             tagfield: boolean; taglvl: integer;
                             varsaddr: addrrange; varssize: addrrange;
                             vartl: integer);
                     proc, func:  (pfaddr: addrrange; pflist: ctp; { param list }
                                   asgn: boolean; { assigned }
                                   case pfdeckind: declkind of
                              standard: (key: 1..18);
                              declared: (pflev: levrange; pfname: integer;
                                          case pfkind: idkind of
                                           actual: (forwdecl, externl: boolean);
                                           formal: ()))
                   end;


     disprange = 0..displimit;
     where = (blck,crec,vrec,rec);

                                                            (*expressions*)
                                                            (*************)
     attrkind = (cst,varbl,expr);
     vaccess = (drct,indrct,inxd);

     attr = record typtr: stp;
              case kind: attrkind of
                cst:   (cval: valu);
                varbl: (packing: boolean; packcom: boolean;
                        tagfield: boolean; taglvl: integer; varnt: stp;
                        ptrref: boolean; vartagoff: addrrange;
                        varssize: addrrange; vartl: integer; ptrrec: boolean;
                        case access: vaccess of
                          drct: (vlevel: levrange; dplmt: addrrange);
                          indrct: (idplmt: addrrange);
           inxd: ());
      expr: ()
              end;

                                                                 (*labels*)
                                                                 (********)
     lbp = ^ labl;
     labl = record { 'goto' label }
                   nextlab: lbp;     { next list link }
                   defined: boolean; { label defining point was seen }
                   labval,           { numeric value of label }
                   labname: integer; { internal sequental name of label }
                   vlevel: levrange; { procedure level of definition }
                   slevel:  integer; { statement level of definition }
                   ipcref:  boolean; { was referenced by another proc/func }
                   minlvl:  integer; { minimum goto reference statement lvl }
                   bact:    boolean; { containing block is active }
                   refer:   boolean  { was referred to }
            end;

     { external file tracking entries }
     extfilep = ^filerec;
     filerec = record filename:idstr; nextfile:extfilep end;

     { case statement tracking entries }
     cip = ^caseinfo;
     caseinfo = record next: cip;
                  csstart: integer;
                  cslab: integer
                end;

     { tag tracking entries }
     ttp = ^tagtrk;
     tagtrk = record
                ival: integer;
                next: ttp
              end;

     { 'with' tracking entries }
     wtp = ^wthtrk;
     wthtrk = record next: wtp;
                 sl: integer
              end;

(*-------------------------------------------------------------------------*)

var

    { !!! remove this statement for self compile }
#ifndef SELF_COMPILE
    prd,prr: text;                  { output code file }
#endif

                                    (*returned by source program scanner
                                     insymbol:
                                     **********)

    sy: symbol;                     (*last symbol*)
    op: operatort;                  (*classification of last symbol*)
    val: valu;                      (*value of last constant*)
    lgth: integer;                  (*length of last string constant*)
    id: idstr;                      (*last identifier (possibly truncated)*)
    kk: 1..maxids;                  (*nr of chars in last identifier*)
    ch: char;                       (*last character*)
    eol: boolean;                   (*end of line flag*)


                                    (*counters:*)
                                    (***********)

    chcnt: integer;                 (*character counter*)
    ic,gc: addrrange;               (*data location and instruction counter*)
    lc:    stkoff;
    linecount: integer;

                                    (*switches:*)
                                    (***********)

    dp,                             (*declaration part*)

    list: boolean;                  { -- l: source program listing }
    prcode: boolean;                { -- c: print symbolic code }
    prtables: boolean;              { -- t: displaying ident and struct tables }
    chkvar: boolean;                { -- v: check variant records }
    debug: boolean;                 { -- d: Debug checks }
    chkref: boolean;                { -- r: Reference checks }
    chkudtc, chkudtf: boolean;      { -- u: Check undefined tagfields, candidate
                                         and final }
    dodmplex: boolean;              { -- x: dump lexical }
    doprtryc: boolean;              { -- z: dump recycling tracker counts }
    doprtlab: boolean;              { -- b: print labels }
    dodmpdsp: boolean;              { -- y: dump the display }
    chkvbk: boolean;                { -- i: check VAR block violations }

    { switches passed through to pint }

    { -- o: check arithmetic overflow }
    { -- e: list instructions (after assembly) }
    { -- g: dump label definitions }
    { -- a: dump runtime storage }
    { -- f: trace system routine executions }
    { -- m: trace instructions }
    { -- j: do post mortem dump }
    { -- h: add source line sets to code }
    { -- k: trace source line executions (number only) }
    { -- w: dump heap space after execution }
    { -- n: obey heap space recycle requests }
    { -- p: check reuse of freed entry }
    { -- q: check undefined accesses }

    option: array ['a'..'z'] of     { option array }
              boolean;


                                    (*pointers:*)
                                    (***********)
    parmptr,
    intptr,realptr,charptr,
    boolptr,nilptr,textptr: stp;    (*pointers to entries of standard ids*)
    uvarptr,
    uprcptr,ufctptr,                (*pointers to entries for undeclared ids*)
    fwptr: ctp;                     (*head of chain of forw decl type ids*)
    outputptr,inputptr: ctp;        { pointers to default files }
    usclrptr: ctp;                  { used to satisfy broken record tag fields }
    fextfilep: extfilep;            (*head of chain of external files*)
    wthstk: wtp;                    { stack of with entries active }

                                    (*bookkeeping of declaration levels:*)
                                    (************************************)

    level: levrange;                (*current static level*)
    scopen: integer;                { scope open count }
    disx,                           (*level of last id searched by searchid*)
    top: disprange;                 (*top of display*)

    display:                        (*where:   means:*)
      array [disprange] of
        packed record               (*=blck:   id is variable id*)
          fname: ctp; flabel: lbp;  (*=crec:   id is field id in record with*)
          fconst: csp; fstruct: stp;
          packing: boolean;         { used for with derived from packed }
          packcom: boolean;         { used for with derived from packed }
          ptrrec: boolean;          { record derived from pointer }
          case occur: where of      (*   constant address*)
            crec: (clev: levrange;  (*=vrec:   id is field id in record with*)
                  cdspl: addrrange);(*   variable address*)
            vrec: (vdspl: addrrange);
            blck: (bname: ctp);     { block id }
            rec: ()
        end;                        (* --> procedure withstatement*)


                                    (*error messages:*)
                                    (*****************)

    errinx: 0..10;                  (*nr of errors in current source line*)
    errlist:
      array [1..10] of
        packed record pos: integer;
                      nmr: 1..500
               end;



                                    (*expression compilation:*)
                                    (*************************)

    gattr: attr;                    (*describes the expr currently compiled*)

                                    (*structured constants:*)
                                    (***********************)

    constbegsys,simptypebegsys,typebegsys,blockbegsys,selectsys,facbegsys,
    statbegsys,typedels: setofsys;
    chartp : array[char] of chtp;
    rw:  array [1..maxres(*nr. of res. words*)] of restr;
    frw: array [1..10] of 1..36(*nr. of res. words + 1*);
    rsy: array [1..maxres(*nr. of res. words*)] of symbol;
    ssy: array [char] of symbol;
    rop: array [1..maxres(*nr. of res. words*)] of operatort;
    sop: array [char] of operatort;
    na:  array [1..maxstd] of restr;
    mn:  array [0..maxins] of packed array [1..4] of char;
    sna: array [1..maxsp] of packed array [1..4] of char;
    cdx: array [0..maxins] of integer;
    cdxs: array [1..6, 1..7] of integer;
    pdx: array [1..maxsp] of integer;
    ordint: array [char] of integer;

    intlabel,mxint10: integer;
    inputhdf: boolean; { 'input' appears in header files }
    inputerr: boolean; { 'input' missing error flagged }
    outputhdf: boolean; { 'output' appears in header files }
    outputerr: boolean; { 'output' missing error flagged }
    errtbl: array [1..500] of boolean; { error occrence tracking }
    toterr: integer; { total errors in program }

    { Recycling tracking counters, used to check for new/dispose mismatches. }
    strcnt: integer; { strings }
    cspcnt: integer; { constants }
    stpcnt: integer; { structures }
    ctpcnt: integer; { identifiers }
    lbpcnt: integer; { label counts }
    filcnt: integer; { file tracking counts }
    cipcnt: integer; { case entry tracking counts }
    ttpcnt: integer; { tag tracking entry counts }
    wtpcnt: integer; { with tracking entry counts }

    { serial numbers to label structure and identifier entries for dumps }
    ctpsnm: integer;
    stpsnm: integer;

    f: boolean; { flag for if error number list entries were printed }
    i: 1..500; { index for error number tracking array }
    c: char;

(*-------------------------------------------------------------------------*)

                           { recycling controls }

(*-------------------------------------------------------------------------*)

  { get string quanta }
  procedure getstr(var p: strvsp);
  begin
     new(p); { get new entry }
     strcnt := strcnt+1 { count }
  end;

  { recycle string quanta list }
  procedure putstrs(p: strvsp);
  var p1: strvsp;
  begin
    while p <> nil do begin
      p1 := p; p := p^.next; dispose(p1); strcnt := strcnt-1
    end
  end;

  { get label entry }
  procedure getlab(var p: lbp);
  begin
     new(p); { get new entry }
     lbpcnt := lbpcnt+1 { add to count }
  end;

  { recycle label entry }
  procedure putlab(p: lbp);
  begin
     dispose(p); { release entry }
     lbpcnt := lbpcnt-1 { remove from count }
  end;

  { push constant entry to list }
  procedure pshcst(p: csp);
  begin
     { push to constant list }
     p^.next := display[top].fconst;
     display[top].fconst := p;
     cspcnt := cspcnt+1 { count entries }
  end;

  { recycle constant entry }
  procedure putcst(p: csp);
  begin
     { recycle string if present }
     if p^.cclass = strg then putstrs(p^.sval)
     else if p^.cclass = reel then putstrs(p^.rval);
     { release entry }
     case p^.cclass of
       reel: dispose(p, reel);
       pset: dispose(p, pset);
       strg: dispose(p, strg)
     end;
     cspcnt := cspcnt-1 { remove from count }
  end;

  { push structure entry to list }
  procedure pshstc(p: stp);
  begin
     { push to structures list }
     p^.next := display[top].fstruct;
     display[top].fstruct := p;
     stpcnt := stpcnt+1; { count entries }
     stpsnm := stpsnm+1; { identify entry in dumps }
     p^.snm := stpsnm
  end;

  { recycle structure entry }
  procedure putstc(p: stp);
  begin
     { release entry }
     case p^.form of
       scalar:   if p^.scalkind = declared then dispose(p, scalar, declared)
                                           else dispose(p, scalar, standard);
       subrange: dispose(p, subrange);
       pointer:  dispose(p, pointer);
       power:    dispose(p, power);
       arrays:   dispose(p, arrays);
       records:  dispose(p, records);
       files:    dispose(p, files);
       tagfld:   begin dispose(p^.vart); dispose(p, tagfld) end;
       variant:  dispose(p, variant);
     end;
     stpcnt := stpcnt-1
  end;

  { initialize and register identifier entry }
  procedure ininam(p: ctp);
  begin
     ctpcnt := ctpcnt+1; { count entry }
     p^.keep := false; { clear keepme flag }
     p^.refer := false; { clear referred flag }
     p^.defined := true; { set defined by default }
     ctpsnm := ctpsnm+1; { identify entry in dumps }
     p^.snm := ctpsnm;
     p^.lastuse := scopen { set scope serial count }
  end;

  procedure putnam(p: ctp); forward;

  { recycle identifier list }
  procedure putidlst(p: ctp);
  var p1: ctp;
  begin
    while p <> nil do begin
      { scavenge the parameter list }
      p1 := p; p := p1^.next;
      putnam(p1) { release }
    end
  end;

  { recycle identifier entry }
  procedure putnam{(p: ctp)};
  begin
     if (p^.klass = proc) or (p^.klass = func) then putidlst(p^.pflist);
     putstrs(p^.name); { release name string }
     { release entry according to class }
     case p^.klass of
       types: dispose(p, types);
       konst: dispose(p, konst);
       vars:  dispose(p, vars);
       field: dispose(p, field);
       proc: if p^.pfdeckind = standard then dispose(p, proc, standard)
                                        else if p^.pfkind = actual then
                                            dispose(p, proc, declared, actual)
                                          else dispose(p, proc, declared, formal);
       func: if p^.pfdeckind = standard then dispose(p, func, standard)
                                        else if p^.pfkind = actual then
                                            dispose(p, func, declared, actual)
                                          else dispose(p, func, declared, formal)
     end;
     ctpcnt := ctpcnt-1 { remove from count }
  end;

  { recycle identifier tree }
  procedure putnams(p: ctp);
  begin
    if p <> nil then begin
      putnams(p^.llink); { release left }
      putnams(p^.rlink); { release right }
      { "keep" means it is a parameter and stays with it's procedure or
        function entry. }
      if not p^.keep then putnam(p) { release the id entry }
    end
  end;

  { scrub display level }
  procedure putdsp(l: disprange);
     var llp: lbp; lvp: csp; lsp: stp;
     { release substructure }
     procedure putsub(p: stp);
        var p1: stp;
     begin
        { clear record recycle list if record }
        if p^.form = records then begin
           { clear structure list }
           while p^.recyc <> nil do begin
              { remove top of list }
              p1 := p^.recyc; p^.recyc := p1^.next;
              putsub(p1) { release that element }
           end;
           putnams(p^.fstfld) { clear id list }
        end else if p^.form = tagfld then begin
              if p^.tagfieldp <> nil then
                 { recycle anonymous tag fields }
                 if p^.tagfieldp^.name = nil then putnam(p^.tagfieldp)
        end;
        putstc(p) { release head entry }
     end;
  begin { putdsp }
    putnams(display[l].fname); { dispose of identifier tree }
    { dispose of label list }
    while display[l].flabel <> nil do begin
      llp := display[l].flabel; display[l].flabel := llp^.nextlab; putlab(llp)
    end;
    { dispose of constant list }
    while display[l].fconst <> nil do begin
      lvp := display[l].fconst; display[l].fconst := lvp^.next; putcst(lvp)
    end;
    { dispose of structure list }
    while display[l].fstruct <> nil do begin
      { remove top from list }
      lsp := display[l].fstruct; display[l].fstruct := lsp^.next; putsub(lsp)
    end
  end; { putdsp }

  { scrub all display levels until given }
  procedure putdsps(l: disprange);
  var t: disprange;
  begin
    if l > top then begin
      writeln('*** Error: Compiler internal error');
      goto 99
    end;
    t := top;
    while t > l do begin
      putdsp(t); t := t-1
    end
  end;

  { get external file entry }
  procedure getfil(var p: extfilep);
  begin
     new(p); { get new entry }
     filcnt := filcnt+1 { count entry }
  end;

  { recycle external file entry }
  procedure putfil(p: extfilep);
  begin
     dispose(p); { release entry }
     filcnt := filcnt-1 { count entry }
  end;

  { get case tracking entry }
  procedure getcas(var p: cip);
  begin
     new(p); { get new entry }
     cipcnt := cipcnt+1 { count entry }
  end;

  { recycle case tracking entry }
  procedure putcas(p: cip);
  begin
     dispose(p); { release entry }
     cipcnt := cipcnt-1 { count entry }
  end;

  { get tag tracking entry }
  procedure gettag(var p: ttp);
  begin
     new(p); { get new entry }
     ttpcnt := ttpcnt+1 { count entry }
  end;

  { recycle tag tracking entry }
  procedure puttag(p: ttp);
  begin
     dispose(p); { release entry }
     ttpcnt := ttpcnt-1 { count entry }
  end;

  { push to with stack }
  procedure pshwth(sl: integer);
  var p: wtp;
  begin
    new(p); { get a new entry }
    p^.next := wthstk; { push to stack }
    wthstk := p;
    p^.sl := sl; { mark level }
    wtpcnt := wtpcnt+1 { count entry }
  end;

  { pop from with stack }
  procedure popwth;
  var p: wtp;
  begin
    if wthstk = nil then begin
      writeln; writeln('*** Compiler error: with stack underflow');
      goto 99
    end else begin
      p := wthstk;
      wthstk := p^.next;
      dispose(p);
      wtpcnt := wtpcnt-1
    end
  end;

(*-------------------------------------------------------------------------*)

                { character and string quanta functions }

(*-------------------------------------------------------------------------*)

  { find lower case of character }
  function lcase(c: char): char;
  begin
    if c in ['A'..'Z'] then c := chr(ord(c)-ord('A')+ord('a'));
    lcase := c
  end { lcase };

  { find reserved word string equal to id string }
  function strequri(a: restr; var b: idstr): boolean;
  var m: boolean; i: integer;
  begin
    m := true;
    for i := 1 to reslen do if lcase(a[i]) <> lcase(b[i]) then m := false;
    for i := reslen+1 to maxids do if b[i] <> ' ' then m := false;
    strequri := m
  end { equstr };

  { write variable length id string to output }
  procedure writev(var f: text; s: strvsp; fl: integer);
  var i: integer; c: char;
  begin i := 1;
    while fl > 0 do begin
      c := ' '; if s <> nil then begin c := s^.str[i]; i := i+1 end;
      write(f, c); fl := fl-1;
      if i > varsqt then begin s := s^.next; i := 1 end
    end
  end;

  { find padded length of variable length id string }
  function lenpv(s: strvsp): integer;
  var i, l, lc: integer;
  begin l := 1; lc := 0;
    while s <> nil do begin
      for i := 1 to varsqt do begin
        if s^.str[i] <> ' ' then lc := l;
        l := l+1; { count characters }
      end;
      s := s^.next
    end;
    lenpv := lc
  end;

  { find padded length of id string }
  function lenp(var s: idstr): integer;
  var l: integer;
  begin
     l := maxids; while (l > 1) and (s[l] = ' ') do l := l-1;
     if s[l] = ' ' then l := 0;
     lenp := l
  end;

  { assign reserved word to id string }
  procedure strassir(var a: idstr; var b: restr);
  var i: integer;
  begin
    for i := 1 to maxids do a[i] := ' ';
    for i := 1 to reslen do a[i] := b[i]
  end;

  { assign identifier fixed to variable length string, including allocation }
  procedure strassvf(var a: strvsp; var b: idstr);
  var i, j, l: integer; p, lp: strvsp;
  begin l := maxids; p := nil; a := nil; j := 1;
    while (l > 1) and (b[l] = ' ') do l := l-1; { find length of fixed string }
    if b[l] = ' ' then l := 0;
    for i := 1 to l do begin
      if j > varsqt then p := nil;
      if p = nil then begin
        getstr(p); p^.next := nil; j := 1;
        if a = nil then a := p else lp^.next := p; lp := p
      end;
      p^.str[j] := b[i]; j := j+1
    end;
    if p <> nil then for j := j to varsqt do p^.str[j] := ' '
  end;

  { assign reserved word fixed to variable length string, including allocation }
  procedure strassvr(var a: strvsp; b: restr);
  var i, j, l: integer; p, lp: strvsp;
  begin l := reslen; p := nil; a := nil; lp := nil; j := 1;
    while (l > 1) and (b[l] = ' ') do l := l-1; { find length of fixed string }
    if b[l] = ' ' then l := 0;
    for i := 1 to l do begin
      if j > varsqt then p := nil;
      if p = nil then begin
        getstr(p); p^.next := nil; j := 1;
        if a = nil then a := p else lp^.next := p; lp := p
      end;
      p^.str[j] := b[i]; j := j+1
    end;
    if p <> nil then for j := j to varsqt do p^.str[j] := ' '
  end;

  { assign number string fixed to variable length string, including allocation }
  procedure strassvd(var a: strvsp; b: nmstr);
  var i, j, l: integer; p, lp: strvsp;
  begin l := digmax; p := nil; a := nil; lp := nil; j := 1;
    while (l > 1) and (b[l] = ' ') do l := l-1; { find length of fixed string }
    if b[l] = ' ' then l := 0;
    for i := 1 to l do begin
      if j > varsqt then p := nil;
      if p = nil then begin
        getstr(p); p^.next := nil; j := 1;
        if a = nil then a := p else lp^.next := p; lp := p
      end;
      p^.str[j] := b[i]; j := j+1
    end;
    if p <> nil then for j := j to varsqt do p^.str[j] := ' '
  end;

  { assign constant string fixed to variable length string, including allocation }
  procedure strassvc(var a: strvsp; b: csstr; l: integer);
  var i, j: integer; p, lp: strvsp;
  begin p := nil; a := nil; lp := nil; j := 1;
    for i := 1 to l do begin
      if j > varsqt then p := nil;
      if p = nil then begin
        getstr(p); p^.next := nil; j := 1;
        if a = nil then a := p else lp^.next := p; lp := p
      end;
      p^.str[j] := b[i]; j := j+1
    end;
    if p <> nil then for j := j to varsqt do p^.str[j] := ' '
  end;

  { assign variable length string to fixed identifier }
  procedure strassfv(var a: idstr; b: strvsp);
  var i, j: integer;
  begin for i := 1 to maxids do a[i] := ' '; i := 1;
     while b <> nil do begin
        for j := 1 to varsqt do begin a[i] := b^.str[j]; i := i+1 end;
        b := b^.next
     end
  end;

  { compare variable length id strings }
  function strequvv(a, b: strvsp): boolean;
  var m: boolean; i: integer;
  begin
    m := true;
    while (a <> nil) and (b <> nil) do begin
      for i := 1 to varsqt do if lcase(a^.str[i]) <> lcase(b^.str[i]) then m := false;
      a := a^.next; b := b^.next
    end;
    if a <> b then m := false;
    strequvv := m
  end;

  { compare variable length id strings, a < b }
  function strltnvv(a, b: strvsp): boolean;
  var i: integer; ca, cb: char;
  begin ca := ' '; cb := ' ';
    while (a <> nil) or (b <> nil) do begin
      i := 1;
      while (i <= varsqt) and ((a <> nil) or (b <> nil)) do begin
        if a <> nil then ca := lcase(a^.str[i]) else ca := ' ';
        if b <> nil then cb := lcase(b^.str[i]) else cb := ' ';
        if ca <> cb then begin a := nil; b := nil end;
        i := i+1
      end;
      if a <> nil then a := a^.next; if b <> nil then b := b^.next
    end;
    strltnvv := ca < cb
  end;

  { compare variable length id string to fixed }
  function strequvf(a: strvsp; var b: idstr): boolean;
  var m: boolean; i, j: integer; c: char;
  begin
    m := true; j := 1;
    for i := 1 to maxids do begin
      c := ' '; if a <> nil then begin c := a^.str[j]; j := j+1 end;
      if lcase(c) <> lcase(b[i]) then m := false;
      if j > varsqt then begin a := a^.next; j := 1 end
    end;
    strequvf := m
  end;

  { compare variable length id string to fixed, a < b }
  function strltnvf(a: strvsp; var b: idstr): boolean;
  var i, j, f: integer; c: char;
  begin
    i := 1; j := 1;
    while i < maxids do begin
      c := ' '; if a <> nil then begin c := a^.str[j]; j := j+1 end;
      if lcase(c) <> lcase(b[i]) then begin f := i; i := maxids end else i := i+1;
      if j > varsqt then begin a := a^.next; j := 1 end
    end;
    strltnvf := lcase(c) < lcase(b[f])
  end;

  { get character from variable length string }
  function strchr(a: strvsp; x: integer): char;
  var c: char; i: integer; q: integer;
  begin
     c := ' '; i := 1; q := 1;
     while i < x do begin
        if q >= varsqt then begin q := 1; if a <> nil then a := a^.next end
        else q := q+1;
        i := i+1
     end;
     if a <> nil then c := a^.str[q];
     strchr := c
   end;

  { put character to variable length string }
  procedure strchrass(var a: strvsp; x: integer; c: char);
  var i: integer; q: integer; p, l: strvsp;
  procedure getsqt;
  var y: integer;
  begin
     if p = nil then begin getstr(p); for y := 1 to varsqt do p^.str[y] := ' ';
        p^.next := nil; if a = nil then a := p else l^.next := p
     end
  end;
  begin
     i := 1; q := 1; p := a; l := nil;
     getsqt;
     while i < x do begin
        if q >= varsqt then begin q := 1; l := p; p := p^.next; getsqt end
        else q := q+1;
        i := i+1
     end;
     p^.str[q] := c
   end;

(*-------------------------------------------------------------------------*)

                                   { math }

(*-------------------------------------------------------------------------*)

function addovf(a, b: integer): boolean;

begin
  addovf := false;
  if (a<0) = (b<0) then if maxint-abs(a) < abs(b) then addovf := true
end;

function mltovf(a, b: integer): boolean;
begin
  mltovf := false;
  if (a <> 0) and (b <> 0) then
    if abs(a) > maxint div abs(b) then mltovf := true
end;

(*-------------------------------------------------------------------------*)

  { dump the display }
  procedure prtdsp;
  var i: integer;
  procedure prtlnk(p: ctp; f: integer);
  var i: integer;
  begin
    if p <> nil then begin
      for i := 1 to f do write(' ');
      writev(output, p^.name, 10); writeln;
      if p^.llink <> nil then prtlnk(p^.llink, f+3);
      if p^.rlink <> nil then prtlnk(p^.rlink, f+3)
    end
  end;
  begin
    writeln;
    writeln('Display:');
    writeln;
    for i := 0 to displimit do if display[i].fname <> nil then begin

       writeln('level ', i:1);
       writeln;
       prtlnk(display[i].fname, 0);
       writeln

    end;
    writeln;
  end;

  procedure markline;
  begin
    writeln(prr, ':', linecount:1)
  end;

  procedure endofline;
    var lastpos,freepos,currpos,currnmr,f,k: integer;
  begin
    if errinx > 0 then   (*output error messages*)
      begin write(output,linecount:6,' ****  ':9);
        lastpos := -1; freepos := 1;
        for k := 1 to errinx do
          begin
            with errlist[k] do
              begin currpos := pos; currnmr := nmr end;
            if currpos = lastpos then write(output,',')
            else
              begin
                while freepos < currpos do
                  begin write(output,' '); freepos := freepos + 1 end;
                write(output,'^');
                lastpos := currpos
              end;
            if currnmr < 10 then f := 1
            else if currnmr < 100 then f := 2
              else f := 3;
            write(output,currnmr:f);
            freepos := freepos + f + 1
          end;
        writeln(output); errinx := 0
      end;
    linecount := linecount + 1;
    if list and (not eof(prd)) then
#ifdef NO_PREAMBLE
      begin write(output,' ':6,'  ':2);
        if dp then write(output,' ':7) else write(output,' ':7);
        write(output,' ')
      end;
#else
      begin write(output,linecount:6,'  ':2);
        if dp then write(output,lc:7) else write(output,ic:7);
        write(output,' ')
      end;
#endif
    { output line marker in intermediate file }
    if not eof(prd) then markline;
    chcnt := 0
  end  (*endofline*) ;

  procedure errmsg(ferrnr: integer);
  begin case ferrnr of
    1:   write('Error in simple type');
    2:   write('Identifier expected');
    3:   write('''program'' expected');
    4:   write(''')'' expected');
    5:   write(''':'' expected');
    6:   write('Illegal symbol');
    7:   write('Error in parameter list');
    8:   write('''of'' expected');
    9:   write('''('' expected');
    10:  write('Error in type');
    11:  write('''['' expected');
    12:  write(''']'' expected');
    13:  write('''end'' expected');
    14:  write(''';'' expected');
    15:  write('Integer expected');
    16:  write('''='' expected');
    17:  write('''begin'' expected');
    18:  write('Error in declaration part');
    19:  write('Error in field-list');
    20:  write(''','' expected');
    21:  write('''.'' expected');
    25:  write('Illegal source character');
    26:  write('String constant too long');
    30:  write('''..'' expected');

    50:  write('Error in constant');
    51:  write(''':='' expected');
    52:  write('''then'' expected');
    53:  write('''until'' expected');
    54:  write('''do'' expected');
    55:  write('''to''/''downto'' expected');
    56:  write('''if'' expected');
    57:  write('''file'' expected');
    58:  write('Error in factor');
    59:  write('Error in variable');

    101: write('Identifier declared twice');
    102: write('Low bound exceeds highbound');
    103: write('Identifier is not of appropriate class');
    104: write('Identifier not declared');
    105: write('Sign not allowed');
    106: write('Number expected');
    107: write('Incompatible subrange types');
    109: write('Type must not be real');
    110: write('Tagfield type must be scalar or subrange');
    111: write('Incompatible with tagfield type');
    112: write('Index type must not be real');
    113: write('Index type must be scalar or subrange');
    114: write('Base type must not be real');
    115: write('Base type must be scalar or subrange');
    116: write('Error in type of standard procedure parameter');
    117: write('Unsatisfied forward reference');
    118: write('Forward reference type identifier in variable declaration');
    119: write('Forward declared; repetition of parameter list not allowed');
    120: write('Function result type must be scalar, subrange or point');
    121: write('File value parameter, or parameter containing file, not allowed');
    122: write('Forward declared function; repetition of result type not allowed');
    123: write('Missing result type in function declaration');
    124: write('F-format for real only');
    125: write('Error in type of standard function parameter');
    126: write('Number of parameters does not agree with declaration');
    127: write('Illegal parameter substitution');
    128: write('Result type of parameter function does not agree with declaration');
    129: write('Type conflict of operands');
    130: write('Expression is not of set type');
    131: write('Tests on equality allowed only');
    132: write('Strict inclusion not allowed');
    133: write('File comparison not allowed');
    134: write('Illegal type of operand(s)');
    135: write('Type of operand must be Boolean');
    136: write('Set element type must be scalar or subrange');
    137: write('Set element types not compatible');
    138: write('Type of variable is not array');
    139: write('Index type is not compatible with declaration');
    140: write('Type of variable is not record');
    141: write('Type of variable must be file or pointer');
    142: write('Illegal parameter substitution');
    143: write('Illegal type of loop control variable');
    144: write('Illegal type of expression');
    145: write('Type conflict');
    146: write('Assignment of files not allowed');
    147: write('Label type incompatible with selecting expression');
    148: write('Subrange bounds must be scalar');
    149: write('Index type must not be integer');
    150: write('Assignment to standard function is not allowed');
    151: write('Assignment to formal function is not allowed');
    152: write('No such field in this record');
    153: write('Type error in read');
    154: write('Actual parameter must be a variable');
    155: write('Control variable must ~ot be declared on intermediate');
    156: write('Multidefined case label');
    157: write('Too many cases in case statement');
    158: write('Missing corresponding variant declaration');
    159: write('Real or string tagfields not allowed');
    160: write('Previous declaration was not forward');
    161: write('Again forward declared');
    162: write('Parameter size must be constant');
    163: write('Missing variant in declaration');
    164: write('Substitution of standard proc/func not allowed');
    165: write('Multidefined label');
    166: write('Multideclared label');
    167: write('Undeclared label');
    168: write('Undefined label');
    169: write('Error in base set');
    170: write('Value parameter expected');
    171: write('Standard file was redeclared');
    172: write('Undeclared external file');
    173: write('Fortran procedure or function expected');
    174: write('Pascal procedure or function expected');
    175: write('Missing file "input" in program heading');
    176: write('Missing file "output" in program heading');
    177: write('Assiqnment to function identifier not allowed here');
    178: write('Multidefined record variant');
    179: write('X-opt of actual proc/funcdoes not match formal declaration');
    180: write('Control variable must not be formal');
    181: write('Constant part of address out of ranqe');
    182: write('identifier too long');
    183: write('For index variable must be local to this block');
    184: write('Interprocedure goto does not reference outter block of destination');
    185: write('Goto references deeper nested statement');
    186: begin write('Label referenced by goto at lesser statement level or ');
               write('differently nested statement') end;
    187: write('Goto references label in different nested statement');
    188: write('Label referenced by goto in different nested statement');
    189: write('Parameter lists of formal and actual parameters not congruous');
    190: write('File component may not contain other files');
    191: write('Cannot assign from file or component containing files');
    192: write('Assignment to function that is not active');
    193: write('Function does not assign to result');
    194: write('Exponent too large');
    195: write('For loop index is threatened');
    197: write('Var parameter cannot be packed');
    198: write('Var parameter cannot be a tagfield');
    199: write('Var parameter must be same type');
    200: write('Tagfield constants must cover entire tagfield type');
    201: write('Error in real constant: digit expected');
    202: write('String constant must not exceed source line');
    203: write('Integer constant exceeds range');
    204: write('8 or 9 in octal number');
    205: write('Zero string not allowed');
    206: write('Integer part of real constant exceeds ranqe');
    236: write('Type error in write');
    239: write('Variant case exceeds allowable range');
    240: write('Header parameter already included');
    241: write('Invalid tolken separator');
    242: write('Identifier referenced before defining point');
    243: write('Type referenced is incomplete');
    244: write('Tagfield constant exceeds type range');
    245: write('Array size too large');

    250: write('Too many nestedscopes of identifiers');
    251: write('Too many nested procedures and/or functions');
    252: write('Too many forward references of procedure entries');
    253: write('Procedure too long');
    254: write('Too many long constants in this procedure');
    255: write('Too many errors on this source line');
    256: write('Too many external references');
    257: write('Too many externals');
    258: write('Too many local files');
    259: write('Expression too complicated');
    260: write('Too many exit labels');
    261: write('Label beyond valid integral value (>9999)');

    300: write('Division by zero');
    301: write('No case provided for this value');
    302: write('Index expression out of bounds');
    303: write('Value to be assigned is out of bounds');
    304: write('Element expression out of range');

    398: write('Implementation restriction');
    399: write('Feature not implemented');

    400,
    500: write('Compiler internal error');
    end
  end;

  procedure error(ferrnr: integer);
  begin

    { This diagnostic is here because error buffers error numbers til the end
      of line, and sometimes you need to know exactly where they occurred. }

#ifdef IMM_ERR
    writeln; writeln('error: ', ferrnr:1);
#endif

    errtbl[ferrnr] := true; { track this error }
    if errinx >= 9 then
      begin errlist[10].nmr := 255; errinx := 10 end
    else
      begin errinx := errinx + 1;
        errlist[errinx].nmr := ferrnr
      end;
    errlist[errinx].pos := chcnt;
    toterr := toterr+1
  end (*error*) ;

  procedure insymbol;
    (*read next basic symbol of source program and return its
    description in the global variables sy, op, id, val and lgth*)
    var i,k, ks: integer;
        digit: nmstr; { temp holding for digit string }
        rvalb: nmstr; { temp holding for real string }
        string: csstr;
        lvp: csp; test, ferr: boolean;
        ev: integer;
        syv: boolean;

    procedure nextch;
    begin if eol then
      begin if list then writeln(output); endofline
      end;
      if not eof(prd) then
       begin eol := eoln(prd); read(prd,ch);
        if list then write(output,ch);
        chcnt := chcnt + 1
       end
      else
        begin writeln(output,'   *** eof ','encountered');
          test := false
        end
    end;

    procedure options;
    var
      ch1 : char; dummy: boolean;
      procedure switch(var opt: boolean );
      begin
        nextch;
        if (ch='+') or (ch='-') then begin
          opt := ch = '+';
          option[ch1] := opt;
          writeln(prr, 'o ', ch1, ch);
          nextch;
        end else begin { just default to on }
          opt := true;
          option[ch1] := true;
          writeln(prr, 'o ', ch1, '+')
        end
      end; { switch() }
    begin { options() }
      repeat
        nextch;
        ch1 := lcase(ch);
        if ch1 = 't' then switch(prtables)
        else if ch1 = 'l' then begin
          switch(list);
          if not list then writeln(output)
        end
        else if ch1 = 'd' then switch(debug)
        else if ch1 = 'c' then switch(prcode)
        else if ch1 = 'v' then switch(chkvar)
        else if ch1 = 'r' then switch(chkref)
        else if ch1 = 'u' then switch(chkudtc)
        else if ch1 = 'x' then switch(dodmplex)
        else if ch1 = 'z' then switch(doprtryc)
        else if ch1 = 'b' then switch(doprtlab)
        else if ch1 = 'y' then switch(dodmpdsp)
        else if ch1 = 'i' then switch(chkvbk)
        else if ch1 in ['a'..'z'] then
          switch(dummy) { pass through unknown options }
        else begin
          { skip all likely option chars }
          while ch in ['a'..'z','A'..'Z','+','-','0'..'9','_'] do
            nextch;
        end;
      until ch <> ',';
    end (*options*) ;

    procedure skpcmt;
    var iscmte: boolean; lastch: char;
    procedure nextchcc;
    begin
      lastch := ch;
      nextch
    end;
    begin
       lastch := ' ';
       if ch = '$' then options;
       repeat
         while (ch <> '}') and (ch <> '*') and not eof(prd) do nextchcc;
         iscmte := ch = '}';
         nextchcc
       until iscmte or (ch = ')') or eof(prd);
       if not iscmte then nextch
    end;

  begin (*insymbol*)
    repeat { until sy resolved }
      syv := true;
      { Skip both spaces and controls. This allows arbitrary formatting characters
        in the source. }
      repeat while (ch <= ' ') and not eol do nextch;
        test := eol;
        if test then nextch
      until not test;
      if chartp[ch] = illegal then
        begin sy := othersy; op := noop;
          error(25); nextch
        end
      else
      case chartp[ch] of
        letter:
          begin k := 0; ferr := true;
            repeat
              if k < maxids then
               begin k := k + 1; id[k] := ch end
              else if ferr then begin error(182); ferr := false end;
              nextch
            until chartp[ch] in [special,illegal,chstrquo,chcolon,
                                  chperiod,chlt,chgt,chlparen,chspace,chlcmt];
            if k >= kk then kk := k
            else
              repeat id[kk] := ' '; kk := kk - 1
              until kk = k;
            sy := ident; op := noop;
            if k <= reslen then
              for i := frw[k] to frw[k+1] - 1 do
                if strequri(rw[i], id) then
                  begin sy := rsy[i]; op := rop[i] end;
        end;
        number:
          begin op := noop; i := 0;
            repeat i := i+1; if i<= digmax then digit[i] := ch; nextch
            until chartp[ch] <> number;
            { separator must be non-alpha numeric or 'e' }
            if (chartp[ch] = letter) and not (lcase(ch) = 'e') then error(241);
            if ((ch = '.') and (prd^ <> '.') and (prd^ <> ')')) or
               (lcase(ch) = 'e') then
              begin
                k := i;
                if ch = '.' then
                  begin k := k+1; if k <= digmax then digit[k] := ch;
                    nextch;
                    if chartp[ch] <> number then begin
                      error(201); k := k+1; if k <= digmax then digit[k] := '0'
                    end else
                      repeat k := k + 1;
                        if k <= digmax then digit[k] := ch; nextch
                      until chartp[ch] <>  number
                  end;
                if lcase(ch) = 'e' then
                  begin k := k+1; if k <= digmax then digit[k] := ch;
                    ks := k;
                    nextch;
                    if (ch = '+') or (ch ='-') then
                      begin k := k+1; if k <= digmax then digit[k] := ch;
                        nextch
                      end;
                    if chartp[ch] <> number then begin
                      error(201); k := k+1; if k < digmax then digit[k] := '0'
                    end else begin ev := 0; ferr := true;
                      repeat k := k+1;
                        if k <= digmax then digit[k] := ch; nextch;
                        if ferr then begin
                          if ev <= mxint10 then
                            ev := ev*10+ordint[digit[k]]
                          else begin error(194); ferr := false end
                        end
                      until chartp[ch] <> number;
                      if (ev > maxexp) then begin
                        { clear out error exponent }
                        while ks <= k do begin digit[ks] := '0'; ks := ks+1 end;
                        if ferr then error(194);
                      end
                    end
                   end;
                 new(lvp,reel); pshcst(lvp); sy:= realconst;
                 lvp^.cclass := reel;
                 with lvp^ do
                   begin for i := 1 to digmax do rvalb[i] := ' ';
                     if k <= digmax then
                       for i := 2 to k + 1 do rvalb[i] := digit[i-1]
                     else begin error(203); rvalb[2] := '0';
                            rvalb[3] := '.'; rvalb[4] := '0'
                          end;
                     { place buffered real string in constant }
                     strassvd(rval, rvalb)
                   end;
                 val.intval := false;
                 val.valp := lvp
              end
            else
              begin
                if i > digmax then
                  begin error(203); val.intval := true; val.ival := 0 end
                else
                  with val do
                    begin val.intval := true; ival := 0;
                      for k := 1 to i do
                        begin
                          if ival <= mxint10 then
                            ival := ival*10+ordint[digit[k]]
                          else begin error(203); ival := 0 end
                        end;
                      sy := intconst
                    end
              end
          end;
        chstrquo:
          begin lgth := 0; sy := stringconst;  op := noop;
            for i := 1 to strglgth do string[i] := ' ';
            repeat
              repeat nextch; lgth := lgth + 1;
                     if lgth <= strglgth then string[lgth] := ch
              until (eol) or (ch = '''');
              if eol then error(202) else nextch
            until ch <> '''';
            string[lgth] := ' '; { get rid of trailing quote }
            lgth := lgth - 1;   (*now lgth = nr of chars in string*)
            if lgth = 1 then
              begin val.intval := true; val.ival := ord(string[1]) end
            else
              begin
                if lgth = 0 then begin
                  { can't let zero length propagate up, we change to space }
                  error(205); val.intval := true; val.ival := ord(' '); lgth := 1
                end else begin
                  new(lvp,strg); pshcst(lvp);
                  lvp^.cclass:=strg;
                  if lgth > strglgth then
                    begin error(26); lgth := strglgth end;
                  with lvp^ do
                    begin slgth := lgth; strassvc(sval, string, strglgth) end;
                  val.intval := false;
                  val.valp := lvp
                end
              end
          end;
        chcolon:
          begin op := noop; nextch;
            if ch = '=' then
              begin sy := becomes; nextch end
            else sy := colon
          end;
        chperiod:
          begin op := noop; nextch;
            if ch = '.' then begin sy := range; nextch end
            else if ch = ')' then begin sy := rbrack; nextch end
            else sy := period
          end;
        chlt:
          begin nextch; sy := relop;
            if ch = '=' then
              begin op := leop; nextch end
            else
              if ch = '>' then
                begin op := neop; nextch end
              else op := ltop
          end;
        chgt:
          begin nextch; sy := relop;
            if ch = '=' then
              begin op := geop; nextch end
            else op := gtop
          end;
        chlparen:
         begin nextch;
           if ch = '*' then begin nextch; skpcmt; syv := false end
           else if ch = '.' then begin sy := lbrack; nextch end
           else sy := lparent;
           op := noop
         end;
        chlcmt: begin nextch; skpcmt; syv := false end;
        special:
          begin sy := ssy[ch]; op := sop[ch];
            nextch
          end;
        chspace: sy := othersy
      end (*case*)
    until syv;

    if dodmplex then begin {  lexical dump }

      writeln;
      write('symbol: ');
      case sy of
         ident:       write('ident: ', id:10);
         intconst:    write('int const: ', val.ival:1);
         realconst:   begin write('real const: ');
                            writev(output, val.valp^.rval, 9) end;
         stringconst: begin write('string const: ''');
                            writev(output, val.valp^.sval, val.valp^.slgth);
                            write('''') end;
         notsy: write('not'); mulop: write('*'); addop: write('+');
         relop: write('<'); lparent: write('('); rparent: write(')');
         lbrack: write('['); rbrack: write(']'); comma: write(',');
         semicolon: write(';'); period: write('.'); arrow: write('^');
         colon: write(':'); becomes: write(':='); range: write('..');
         labelsy: write('label'); constsy: write('const'); typesy: write('type');
         varsy: write('var'); funcsy: write('function'); progsy: write('program');
         procsy: write('procedure'); setsy: write('set');
         packedsy: write('packed'); arraysy: write('array');
         recordsy: write('record'); filesy: write('file');
         beginsy: write('begin'); ifsy: write('if'); casesy: write('case');
         repeatsy: write('repeat'); whilesy: write('while');
         forsy: write('for'); withsy: write('with'); gotosy: write('goto');
         endsy: write('end'); elsesy: write('else'); untilsy: write('until');
         ofsy: write('of'); dosy: write('do'); tosy: write('to');
         downtosy: write('downto'); thensy: write('then');
         othersy: write('<other>');
      end;
      writeln

    end

  end (*insymbol*) ;

  procedure searchidlvs(var fcp: ctp; mp: strvsp; top: disprange);
    label 1;
    var lcp: ctp;
        disxl: disprange;
  begin
    for disxl := top downto 0 do
      begin lcp := display[disxl].fname;
        while lcp <> nil do begin
          if strequvv(lcp^.name, mp) then goto 1
          else
            if strltnvv(lcp^.name, mp) then lcp := lcp^.rlink
            else lcp := lcp^.llink
        end
      end;
      lcp := nil; { make sure this is not found }
1:  fcp := lcp
  end (*searchidne*) ;

  procedure enterid(fcp: ctp);
    (*enter id pointed at by fcp into the name-table,
     which on each declaration level is organised as
     an unbalanced binary tree*)
    var lcp, lcp1: ctp; lleft: boolean;
  begin
    if top > 0 then begin
      searchidlvs(lcp, fcp^.name, top-1);
      if lcp <> nil then
        if lcp^.lastuse >= scopen then error(242)
    end;
    lcp := display[top].fname;
    if lcp = nil then
      display[top].fname := fcp
    else
      begin
        repeat lcp1 := lcp;
          if strequvv(lcp^.name, fcp^.name) then begin
            (*name conflict, follow right link*)
            error(101); lcp := lcp^.rlink; lleft := false
          end else
            if strltnvv(lcp^.name, fcp^.name) then
              begin lcp := lcp^.rlink; lleft := false end
            else begin lcp := lcp^.llink; lleft := true end
        until lcp = nil;
        if lleft then lcp1^.llink := fcp else lcp1^.rlink := fcp
      end;
    fcp^.llink := nil; fcp^.rlink := nil; fcp^.lastuse := scopen
  end (*enterid*) ;

  procedure searchsection(fcp: ctp; var fcp1: ctp);
    (*to find record fields and forward declared procedure id's
     --> procedure proceduredeclaration
     --> procedure selector*)
     label 1;
  begin
    while fcp <> nil do
      if strequvf(fcp^.name, id) then goto 1
      else if strltnvf(fcp^.name, id) then fcp := fcp^.rlink
        else fcp := fcp^.llink;
1:  fcp1 := fcp
  end (*searchsection*) ;

  function match(var a, b: idstr): boolean; forward;

  procedure searchidnenm(fidcls: setofids; var fcp: ctp; var mm: boolean; rs: boolean);
    label 1;
    var lcp, lcp1: ctp;
        disxl: disprange; m: boolean; ids: idstr;
  begin
    mm := false;
    for disxl := top downto 0 do
      begin lcp := display[disxl].fname;
        while lcp <> nil do begin
          if rs then begin
            strassfv(ids, lcp^.name);
            m := match(ids, id)
          end else m := strequvf(lcp^.name, id);
          if m then begin
            lcp1 := lcp;
            if lcp1^.klass in fidcls then begin lcp := lcp1; disx := disxl; goto 1 end
            else begin mm := true; lcp := lcp^.rlink end
          end else
            if strltnvf(lcp^.name, id) then lcp := lcp^.rlink
            else lcp := lcp^.llink
        end
      end;
      disx := 0;
      lcp := nil; { make sure this is not found }
1:  fcp := lcp
  end (*searchidnenm*) ;

  procedure searchidne(fidcls: setofids; var fcp: ctp; rs: boolean);
    var mm: boolean;
  begin
    searchidnenm(fidcls, fcp, mm, rs);
    if mm then error(103)
  end (*searchidne*) ;

  procedure searchid(fidcls: setofids; var fcp: ctp);
    var lcp: ctp;
  begin
    searchidne(fidcls, lcp, false); { perform no error search }
    if lcp <> nil then begin { found }
      lcp^.refer := true;
      if not lcp^.defined then error(243);
      lcp^.lastuse := scopen
    end else begin (*search not successful --> procedure simpletype*)
      error(104);
      searchidne(fidcls, lcp, true); { perform respell search }
      if lcp = nil then begin { no respell, coin entry }
        { form a dummy entry by type to return }
        if types in fidcls then begin
          new(lcp,types); ininam(lcp);
          with lcp^ do
            begin klass := types; strassvf(name, id); idtype := nil end
        end else
          if vars in fidcls then begin
            new(lcp,vars); ininam(lcp);
            with lcp^ do
              begin klass := vars; strassvf(name, id); idtype := nil;
                vkind := actual; next := nil; vlev := 0; vaddr := 0;
                threat := false; forcnt := 0
              end
          end else
            if field in fidcls then begin
              new(lcp,field); ininam(lcp);
              with lcp^ do
                begin klass := field; strassvf(name, id); idtype := nil;
                  next := nil; fldaddr := 0
                end
            end else
              if konst in fidcls then begin
                new(lcp,konst); ininam(lcp);
                with lcp^ do
                  begin klass := konst; strassvf(name, id); idtype := nil;
                    next := nil; values.intval := true; values.ival := 0
                  end
              end else
                if proc in fidcls then begin
                  new(lcp,proc,declared,actual); ininam(lcp);
                  with lcp^ do
                    begin klass := proc; pfdeckind := declared; pfkind := actual;
                      strassvf(name, id); idtype := nil; forwdecl := false;
                      next := nil; externl := false; pflev := 0; pfname := 0;
                      pflist := nil
                    end
                end else begin
                  new(ufctptr,func,declared,actual); ininam(ufctptr);
                  with ufctptr^ do
                  begin klass := func; pfdeckind := declared; pfkind := actual;
                    strassvf(name, id); idtype := nil; next := nil;
                    forwdecl := false; externl := false; pflev := 0; pfname := 0;
                    pflist := nil;
                  end
                end;
        enterid(lcp)
      end
    end;
    fcp := lcp
  end (*searchid*) ;

  procedure getbounds(fsp: stp; var fmin,fmax: integer);
    (*get internal bounds of subrange or scalar type*)
    (*assume fsp<>intptr and fsp<>realptr*)
  begin
    fmin := 0; fmax := 0;
    if fsp <> nil then
    with fsp^ do
      if form = subrange then
        begin fmin := min.ival; fmax := max.ival end
      else
          if fsp = charptr then
            begin fmin := ordminchar; fmax := ordmaxchar
            end
          else
            if fsp = intptr then
              begin fmin := -maxint; fmax := maxint
              end
            else
              if fconst <> nil then
                fmax := fconst^.values.ival
  end (*getbounds*) ;

  function isbyte(fsp: stp): boolean;
    { check structure is byte }
  var fmin, fmax: integer;
  begin
    getbounds(fsp, fmin, fmax);
    isbyte := (fmin >= 0) and (fmax <= 255)
  end;

  function basetype(fsp: stp): stp;
    { remove any subrange types }
  function issub(fsp: stp): boolean;
  begin
     if fsp <> nil then issub := fsp^.form = subrange
     else issub := false
  end;
  begin
    if fsp <> nil then
      while issub(fsp) do
        fsp := fsp^.rangetype;
    basetype := fsp
  end;

  { alignment for general memory placement }
  function alignquot(fsp: stp): integer;
  begin
    alignquot := 1;
    if fsp <> nil then
      with fsp^ do
        case form of
          scalar:   if fsp=intptr then alignquot := intal
                    else if fsp=boolptr then alignquot := boolal
                    else if scalkind=declared then alignquot := intal
                    else if fsp=charptr then alignquot := charal
                    else if fsp=realptr then alignquot := realal
                    else (*parmptr*) alignquot := parmal;
          subrange: alignquot := alignquot(rangetype);
          pointer:  alignquot := adral;
          power:    alignquot := setal;
          files:    alignquot := fileal;
          arrays:   alignquot := alignquot(aeltype);
          records:  alignquot := recal;
          variant,tagfld: error(500)
        end
  end (*alignquot*);

  procedure alignu(fsp: stp; var flc: addrrange);
    var k,l: integer;
  begin
    k := alignquot(fsp);
    l := flc-1;
    flc := l + k  -  (k+l) mod k
  end (*align*);

  procedure alignd(fsp: stp; var flc: stkoff);
    var k,l: integer;
  begin
    k := alignquot(fsp);
    l := flc+1;
    flc := l - k  +  (k-l) mod k;
  end (*align*);

  { align for stack }
  function aligns(flc: addrrange): addrrange;
    var l: integer;
  begin
    l := flc+1;
    flc := l - stackal  +  (stackal-l) mod stackal;
    aligns := flc
  end (*aligns*);

  procedure printtables(fb: boolean);
    (*print data structure and name table*)

    var i, lim: disprange;

    procedure marker;
      (*mark data structure entries to avoid multiple printout*)
      var i: integer;

      procedure markctp(fp: ctp); forward;

      procedure markstp(fp: stp);
        (*mark data structures, prevent cycles*)
      begin
        if fp <> nil then
          with fp^ do
            begin marked := true;
              case form of
              scalar:   ;
              subrange: markstp(rangetype);
              pointer:  (*don't mark eltype: cycle possible; will be marked
                        anyway, if fp = true*) ;
              power:    markstp(elset) ;
              arrays:   begin markstp(aeltype); markstp(inxtype) end;
              records:  begin markctp(fstfld); markstp(recvar) end;
              files:    markstp(filtype);
              tagfld:   markstp(fstvar);
              variant:  begin markstp(nxtvar); markstp(subvar) end;
              end (*case*)
            end (*with*)
      end (*markstp*);

      procedure markctp{(fp: ctp)};
      begin
        if fp <> nil then
          with fp^ do
            begin markctp(llink); markctp(rlink);
              markstp(idtype)
            end
      end (*markctp*);

    begin (*marker*)
      for i := top downto lim do
        markctp(display[i].fname)
    end (*marker*);

    procedure wrtctp(ip: ctp);
    begin
      if ip = nil then write('<nil>':intdig) else write(ip^.snm:intdig)
    end;

    procedure wrtstp(sp: stp);
    begin
      if sp = nil then write('<nil>':intdig) else write(sp^.snm:intdig)
    end;

    procedure followctp(fp: ctp); forward;

    procedure followstp(fp: stp);
    begin
      if fp <> nil then
        with fp^ do
          if marked then
            begin marked := false; write('S: '); wrtstp(fp);
              write(' ', size:intdig, ' ');
              case form of
              scalar:   begin write('scalar':intdig, ' ');
                          if scalkind = standard then
                            write('standard':intdig)
                          else write('declared':intdig,' '); wrtctp(fconst);
                          writeln(output)
                        end;
              subrange: begin
                          write('subrange':intdig,' '); wrtstp(rangetype); write(' ');
                          if rangetype <> realptr then
                            write(min.ival:intdig, ' ', max.ival:intdig)
                          else
                            if (min.valp <> nil) and (max.valp <> nil) then begin
                              write(' '); writev(output, min.valp^.rval, 9);
                              write(' '); writev(output, max.valp^.rval, 9)
                            end;
                          writeln; followstp(rangetype);
                        end;
              pointer:  begin write('pointer':intdig,' '); wrtstp(eltype);
                              writeln end;
              power:    begin write('set':intdig,' '); wrtstp(elset); writeln;
                          followstp(elset)
                        end;
              arrays:   begin
                          write('array':intdig,' '); wrtstp(aeltype);
                          write(' '); wrtstp(inxtype); writeln;
                          followstp(aeltype); followstp(inxtype)
                        end;
              records:  begin
                          write('record':intdig,' '); wrtctp(fstfld); write(' ');
                          wrtstp(recvar); write(' '); wrtstp(recyc); writeln;
                          followctp(fstfld); followstp(recvar)
                        end;
              files:    begin write('file':intdig,' '); wrtstp(filtype); writeln;
                          followstp(filtype)
                        end;
              tagfld:   begin write('tagfld':intdig,' '); wrtctp(tagfieldp);
                          write(' '); wrtstp(fstvar); writeln(' ', varts:intdig);
                          followstp(fstvar)
                        end;
              variant:  begin write('variant':intdig,' '); wrtstp(nxtvar);
                          write(' '); wrtstp(subvar); write(' '); wrtstp(caslst);
                          writeln(' ',varval.ival:intdig, ' ', varln:intdig);
                          followstp(nxtvar); followstp(subvar)
                        end;
              end (*case*)
            end (*if marked*)
    end (*followstp*);

    procedure followctp{(fp: ctp)};
    begin
      if fp <> nil then
        with fp^ do
          begin write('C: '); wrtctp(fp); write(' ');
                writev(output, name, intdig); write(' '); wrtctp(llink);
                write(' '); wrtctp(rlink); write(' '); wrtstp(idtype); write(' ');
            case klass of
              types: write('type':intdig);
              konst: begin write('constant':intdig,' '); wrtctp(next); write(' ');
                       if idtype <> nil then
                         if idtype = realptr then
                           begin
                             if values.valp <> nil then begin
                               writev(output, values.valp^.rval, 9)
                             end
                           end
                         else
                           if idtype^.form = arrays then  (*stringconst*)
                             begin
                               if values.valp <> nil then
                                 begin
                                   with values.valp^ do
                                     writev(output, sval, slgth)
                                 end
                             end
                           else write(values.ival:intdig)
                     end;
              vars:  begin write('variable':intdig, ' ');
                       if vkind = actual then write('actual':intdig)
                       else write('formal':intdig);
                       write(' '); wrtctp(next);
                       write(' ', vlev:intdig,' ',vaddr:intdig, ' ');
                       if threat then write('threat':intdig) else write(' ':intdig);
                       write(' ', forcnt:intdig, ' ');
                     end;
              field: begin write('field':intdig,' '); wrtctp(next); write(' ');
                           write(fldaddr:intdig,' '); wrtstp(varnt); write(' ');
                           wrtctp(varlb); write(' ');
                       if tagfield then write('tagfield':intdig) else write(' ':intdig);
                       write(' ', taglvl:intdig, ' ',varsaddr:intdig, ' ',
                             varssize:intdig, ' ', vartl:intdig)
                     end;
              proc,
              func:  begin
                       if klass = proc then write('procedure':intdig, ' ')
                       else write('function':intdig, ' ');
                       if asgn then write('assigned':intdig, ' ') else write(' ':intdig, ' ');
                       if pfdeckind = standard then
                         write('standard':intdig, '-', key:intdig)
                       else
                         begin write('declared':intdig,'-'); wrtctp(pflist); write('-');
                           write(pflev:intdig,' ',pfname:intdig, ' ');
                           if pfkind = actual then
                             begin write('actual':intdig, ' ');
                               if forwdecl then write('forward':intdig, ' ')
                               else write('notforward':intdig, ' ');
                               if externl then write('extern':intdig)
                               else write('not extern':intdig);
                             end
                           else write('formal':intdig)
                         end
                     end;
            end (*case*);
            writeln;
            followctp(llink); followctp(rlink);
            followstp(idtype)
          end (*with*)
    end (*followctp*);

  begin (*printtables*)
    writeln(output); writeln(output); writeln(output);
    if fb then lim := 0
    else begin lim := top; write(' local') end;
    writeln(' tables:', top:1, '-', lim:1, ':'); writeln(output);
    writeln('C: ', 'Entry #':intdig, ' ', 'Id':intdig, ' ', 'llink':intdig, ' ',
            'rlink':intdig, ' ', 'Typ':intdig, ' ', 'Class':intdig);
    writeln('S: ', 'Entry #':intdig, ' ', 'Size':intdig, ' ', 'Form ':intdig);
    write('===============================================================');
    writeln('==========================');
    marker;
    for i := top downto lim do
      begin writeln('Level: ', i:1); followctp(display[i].fname) end;
    writeln(output);
    if not eol then write(' ':chcnt+16)
  end (*printtables*);

  procedure chkrefs(p: ctp; var w: boolean);
  begin
    if chkref then begin
      if p <> nil then begin
        chkrefs(p^.llink, w); { check left }
        chkrefs(p^.rlink, w); { check right }
        if not p^.refer then begin
          if not w then writeln; writev(output, p^.name, 10);
          writeln(' unreferenced'); w := true
        end
      end
    end
  end;

  procedure chkudf(p: ctp);
  begin
    if p <> nil then begin
      chkudf(p^.llink); { check left }
      chkudf(p^.rlink); { check right }
      if p^.klass in [proc, func] then
        if p^.pfdeckind = declared then
          if p^.pfkind = actual then
            if p^.forwdecl then begin
        writeln; write('*** Error: Forward not completed: '); writev(output, p^.name, 10);
        writeln; toterr := toterr+1
      end
    end
  end;

  procedure genlabel(var nxtlab: integer);
  begin intlabel := intlabel + 1;
    nxtlab := intlabel
  end (*genlabel*);

  procedure prtlabel(labname: integer);
  begin
    write(prr, 'l '); write(prr, labname:1)
  end;

  procedure putlabel(labname: integer);
  begin
    if prcode then begin prtlabel(labname); writeln(prr) end
  end (*putlabel*);

  procedure searchlabel(var llp: lbp; level: disprange);
  var fllp: lbp; { found label entry }
  begin
    fllp := nil; { set no label found }
    llp := display[level].flabel; { index top of label list }
    while llp <> nil do begin { traverse }
      if llp^.labval = val.ival then begin { found }
        fllp := llp; { set entry found }
        llp := nil { stop }
      end else llp := llp^.nextlab { next in list }
    end;
    llp := fllp { return found entry or nil }
  end;

  procedure newlabel(var llp: lbp);
  var lbname: integer;
  begin
    with display[top] do
      begin getlab(llp);
        with llp^ do
          begin labval := val.ival; if labval > 9999 then error(261);
            genlabel(lbname); defined := false; nextlab := flabel;
            labname := lbname; vlevel := level; slevel := 0;
            ipcref := false; minlvl := maxint; bact := false;
            refer := false
          end;
        flabel := llp
      end
  end;

  procedure prtlabels;
  var llp: lbp; { found label entry }
  begin
    writeln;
    writeln('Labels: ');
    writeln;
    llp := display[level].flabel; { index top of label list }
    while llp <> nil do with llp^ do begin { traverse }
      writeln('label: ', labval:1, ' defined: ', defined,
              ' internal: ', labname:1, ' vlevel: ', vlevel:1,
              ' slevel: ', slevel:1, ' ipcref: ', ipcref:1,
              ' minlvl: ', minlvl:1);
      writeln('   bact: ', bact);
      llp := llp^.nextlab { next in list }
    end
  end;

  { respell b to match a }
  function match {(var a, b: idstr): boolean};
  var m: boolean; mc,i,la,lb: integer;

  function gap(var a, b: idstr; la, lb: integer): boolean;
  var i,fi: integer; m: boolean;
  begin m := false;
    if la = lb+1 then begin
      i := 1; fi := la;
      while i < la do
        if a[i] <> b[i] then begin fi := i; i := la end
        else i := i+1;
      m := true;
      for i := fi+1 to la do
         if a[i] <> b[i-1] then m := false
    end;
    gap := m
  end; { gap }

  begin { match }
    la := lenp(a);
    lb := lenp(b);
    m := false;
    if la = lb then begin
      mc := 0;
      for i := 1 to la do if lcase(a[i]) = lcase(b[i]) then mc := mc+1;
      m := (mc >= la-1) and (mc > 1);
      if not m and (mc >= la-2) then begin
        m := false;
        for i := 1 to la-1 do begin
          if (a[i] <> a[i+1]) and (a[i] = b[i+1]) and (a[i+1] = b[i]) then
            m := true
        end
      end
    end;
    if not m then begin
      m := gap(a, b, la, lb) and (lb > 1);
      if not m then
         m := gap(b, a, lb, la) and (la > 1)
    end;
    match := m
  end; { match }

  function matres(ss: setofsys): symbol;
  var sy: symbol; ri: 1..maxres; b: idstr;
  begin
    sy := ident; ri := 1;
    while ri < maxres do begin
      if rsy[ri] in ss then begin
        strassir(b, rw[ri]);
        if match(b, id) then begin sy := rsy[ri]; ri := maxres end
        else ri := ri+1
      end else ri := ri+1
    end;
    if (sy = ident) and (ri = maxres) and (rsy[ri] in ss) then begin
      strassir(b, rw[ri]);
      if match(b, id) then sy := rsy[ri]
    end;
    matres := sy
  end;

  procedure skip(fsys: setofsys);
    (*skip input string until relevant symbol found*)
  begin
    if not eof(prd) then
      begin while not(sy in fsys) and (not eof(prd)) do insymbol;
        if not (sy in fsys) then insymbol
      end
  end (*skip*) ;

  procedure perror(err: integer; ss: setofsys; cs: setofsys);
  var nsy: symbol;
  begin
     error(err);
     if (sy = ident) and (cs <> []) then begin
       nsy := matres(cs);
       if (nsy = ident) and (ss <> []) then
         begin if not eof(prd) then insymbol; skip(ss) end
       else sy := nsy
     end else if ss <> [] then skip(ss)
  end;

  procedure block(fsys: setofsys; fsy: symbol; fprocp: ctp);
    var lsy: symbol;
        stalvl: integer; { statement nesting level }

    { check integer or subrange of }
    function intt(fsp: stp): boolean;
      var t: stp;
    begin intt := false;
      if fsp <> nil then begin
        t := basetype(fsp);
        if t = intptr then intt := true
      end
    end;

    procedure constant(fsys: setofsys; var fsp: stp; var fvalu: valu);
      var lsp: stp; lcp: ctp; sign: (none,pos,neg);
          lvp: csp; i: 2..strglgth;
    begin lsp := nil; fvalu.intval := true; fvalu.ival := 0;
      if not(sy in constbegsys) then
        begin error(50); skip(fsys+constbegsys) end;
      if sy in constbegsys then
        begin
          if sy = stringconst then
            begin
              if lgth = 1 then lsp := charptr
              else
                begin
                  new(lsp,arrays); pshstc(lsp);
                  with lsp^ do
                    begin aeltype := charptr; inxtype := nil;
                       size := lgth*charsize; form := arrays;
                       packing := true
                    end
                end;
              fvalu := val; insymbol
            end
          else
            begin
              sign := none;
              if (sy = addop) and (op in [plus,minus]) then
                begin if op = plus then sign := pos else sign := neg;
                  insymbol
                end;
              if sy = ident then
                begin searchid([konst],lcp);
                  with lcp^ do
                    begin lsp := idtype; fvalu := values end;
                  if sign <> none then
                    if intt(lsp) then
                      begin if sign = neg then fvalu.ival := -fvalu.ival end
                    else
                      if lsp = realptr then
                        begin
                          if sign = neg then
                            begin new(lvp,reel); pshcst(lvp); lvp^.rval := nil;
                              if strchr(fvalu.valp^.rval, 1) = '-' then
                                strchrass(lvp^.rval, 1, '+')
                              else strchrass(lvp^.rval, 1, '-');
                              for i := 2 to digmax do
                                strchrass(lvp^.rval, i, strchr(fvalu.valp^.rval, i));
                              fvalu.valp := lvp;
                            end
                          end
                        else error(105);
                  insymbol;
                end
              else
                if sy = intconst then
                  begin if sign = neg then val.ival := -val.ival;
                    lsp := intptr; fvalu := val; insymbol
                  end
                else
                  if sy = realconst then
                    begin if sign = neg then strchrass(val.valp^.rval, 1, '-');
                      lsp := realptr; fvalu := val; insymbol
                    end
                  else
                    begin error(106); skip(fsys) end
            end;
          if not (sy in fsys) then
            begin error(6); skip(fsys) end
          end;
      fsp := lsp
    end (*constant*) ;

    function stringt(fsp: stp) : boolean; forward;

    function comptypes(fsp1,fsp2: stp) : boolean;
      (*decide whether structures pointed at by fsp1 and fsp2 are compatible*)
    begin
      comptypes := false; { set default is false }
      { remove any subranges }
      fsp1 := basetype(fsp1);
      fsp2 := basetype(fsp2);
      { Check equal. Aliases of the same type will also be equal. }
      if fsp1 = fsp2 then comptypes := true
      else
        if (fsp1 <> nil) and (fsp2 <> nil) then begin
          if fsp1^.form = fsp2^.form then
            case fsp1^.form of
              scalar: ;
              { Subranges are compatible if either type is a subrange of the
                other, or if the base type is the same. }
              subrange: ; { done above }
              { Sets are compatible if they have the same base types and packed/
                unpacked status, or one of them is the empty set. The empty set
                is indicated by a nil base type, which is identical to a base
                type in error. Either way, we treat them as compatible.

                Set types created for set constants have a flag that disables
                packing matches. This is because set constants can be packed or
                unpacked by context. }
              power: comptypes := (comptypes(fsp1^.elset, fsp2^.elset) and
                                    ((fsp1^.packing = fsp2^.packing) or
                                     not fsp1^.matchpack or
                                     not fsp2^.matchpack)) or
                                  (fsp1^.elset = nil) or (fsp2^.elset = nil);
              { Arrays are compatible if they are string types and equal in size }
              arrays: comptypes := stringt(fsp1) and stringt(fsp2) and
                                   (fsp1^.size = fsp2^.size );
              { Pointers, must either be the same type or aliases of the same
                type, or one must be nil. The nil pointer is indicated by a nil
                base type, which is identical to a base type in error. Either
                way, we treat them as compatible. }
              pointer: comptypes := (fsp1^.eltype = nil) or (fsp2^.eltype = nil);
              { records and files must either be the same type or aliases of the
                same type }
              records: ;
              files:
            end (*case*)
        end else comptypes := true { one of the types is in error }
    end (*comptypes*) ;

    { check structure is, or contains, a file }
    function filecomponent(fsp: stp): boolean;
    var f: boolean;
      { tour identifier tree }
      function filecomponentre(lcp: ctp): boolean;
      var f: boolean;
      begin
        f := false; { set not file by default }
        if lcp <> nil then with lcp^ do begin
          if filecomponent(idtype) then f := true;
          if filecomponentre(llink) then f := true;
          if filecomponentre(rlink) then f := true
        end;
        filecomponentre := f
      end;
    begin
      f := false; { set not a file by default }
      if fsp <> nil then with fsp^ do case form of
        scalar:   ;
        subrange: ;
        pointer:  ;
        power:    ;
        arrays:   if filecomponent(aeltype) then f := true;
        records:  if filecomponentre(fstfld) then f := true;
        files:    f := true;
        tagfld:   ;
        variant:  ;
      end;
      filecomponent := f
    end;

    function stringt;
    var fmin, fmax: integer;
    begin stringt := false;
      if fsp <> nil then
        if fsp^.form = arrays then
          if fsp^.packing then begin
          { if the index is nil, either the array is a string constant or the
            index type was in error. Either way, we call it a string }
          if fsp^.inxtype = nil then begin fmin := 1; fmax := fsp^.size end
          else getbounds(fsp^.inxtype,fmin,fmax);
          if (fsp^.aeltype = charptr) and
             ((basetype(fsp^.inxtype) = intptr) or (fsp^.inxtype = nil)) and
             (fmin = 1) and (fmax > 1) then stringt := true
        end
    end (*stringt*) ;

    { resolve all pointer references in the forward list }
    procedure resolvep;
    var ids: idstr; lcp1, lcp2: ctp; mm, fe: boolean;
    begin
      ids := id;
      fe := true;
      while fwptr <> nil do begin
        lcp1 := fwptr;
        fwptr := lcp1^.next;
        strassfv(id, lcp1^.name);
        searchidnenm([types], lcp2, mm, false);
        if lcp2 <> nil then begin
          lcp1^.idtype^.eltype := lcp2^.idtype;
          lcp2^.refer := true
        end else begin
          if fe then begin error(117); writeln(output) end;
          write('*** undefined type-id forward reference: ');
          writev(output, lcp1^.name, prtlln); writeln;
          fe := false
        end;
        putnam(lcp1)
      end;
      id := ids
    end;

    procedure typ(fsys: setofsys; var fsp: stp; var fsize: addrrange);
      var lsp,lsp1,lsp2: stp; oldtop: disprange; lcp: ctp;
          lsize,displ: addrrange; lmin,lmax,span: integer;
          test: boolean; ispacked: boolean;

      procedure simpletype(fsys:setofsys; var fsp:stp; var fsize:addrrange);
        var lsp,lsp1: stp; lcp,lcp1: ctp; ttop: disprange;
            lcnt: integer; lvalu: valu; t: integer;
      begin fsize := 1;
        if not (sy in simptypebegsys) then
          begin error(1); skip(fsys + simptypebegsys) end;
        if sy in simptypebegsys then
          begin
            if sy = lparent then
              begin ttop := top;   (*decl. consts local to innermost block*)
                while display[top].occur <> blck do top := top - 1;
                new(lsp,scalar,declared); pshstc(lsp);
                with lsp^ do
                  begin size := intsize; form := scalar;
                    scalkind := declared; packing := false
                  end;
                lcp1 := nil; lcnt := 0;
                repeat insymbol;
                  if sy = ident then
                    begin new(lcp,konst); ininam(lcp);
                      with lcp^ do
                        begin strassvf(name, id); idtype := lsp; next := lcp1;
                          values.intval := true; values.ival := lcnt;
                          klass := konst
                        end;
                      enterid(lcp);
                      lcnt := lcnt + 1;
                      lcp1 := lcp; insymbol
                    end
                  else error(2);
                  if not (sy in fsys + [comma,rparent]) then
                    begin error(6); skip(fsys + [comma,rparent]) end
                until sy <> comma;
                lsp^.fconst := lcp1; top := ttop;
                if sy = rparent then insymbol else error(4);
                { resize for byte if needed }
                if isbyte(lsp) then lsp^.size := 1;
                fsize := lsp^.size
              end
            else
              begin
                if sy = ident then
                  begin searchid([types,konst],lcp);
                    insymbol;
                    if lcp^.klass = konst then
                      begin new(lsp,subrange); pshstc(lsp);
                        with lsp^, lcp^ do
                          begin form := subrange; rangetype := idtype;
                            if stringt(rangetype) then
                              begin error(148); rangetype := nil end;
                            if rangetype = realptr then
                              begin error(109); rangetype := nil end;
                            if not values.intval then
                              begin min.intval := true; min.ival := 1 end
                            else min := values;
                            size := intsize; packing := false
                          end;
                        if sy = range then insymbol else error(30);
                        constant(fsys,lsp1,lvalu);
                        if not lvalu.intval then
                          begin lsp^.max.intval := true; lsp^.max.ival := 1 end
                        else lsp^.max := lvalu;
                        if lsp^.rangetype <> lsp1 then error(107);
                        if isbyte(lsp) then lsp^.size := 1
                      end
                    else
                      begin lsp := lcp^.idtype;
                        if lsp <> nil then fsize := lsp^.size
                      end
                  end (*sy = ident*)
                else
                  begin new(lsp,subrange); pshstc(lsp);
                    lsp^.form := subrange; lsp^.packing := false;
                    constant(fsys + [range],lsp1,lvalu);
                    if stringt(lsp1) then
                      begin error(148); lsp1 := nil end;
                    if lsp1 = realptr then begin error(109); lsp1 := nil end;
                    with lsp^ do begin
                      rangetype:=lsp1;
                      if lvalu.intval then min:=lvalu else
                        begin min.intval := true; min.ival := 1 end;
                      size:=intsize
                    end;
                    if sy = range then insymbol else error(30);
                    constant(fsys,lsp1,lvalu);
                    lsp^.max := lvalu;
                    if lsp^.rangetype <> lsp1 then error(107);
                    if isbyte(lsp) then lsp^.size := 1;
                    fsize := lsp^.size
                  end;
                if lsp <> nil then
                  with lsp^ do
                    if form = subrange then begin
                      if rangetype <> nil then
                        if rangetype = realptr then
                          begin error(109); rangetype := intptr end;
                      if min.ival > max.ival then
                        begin error(102);
                          { swap to fix and suppress further errors }
                          t := min.ival; min.ival := max.ival; max.ival := t
                        end
                    end
              end;
            fsp := lsp;
            if not (sy in fsys) then
              begin error(6); skip(fsys) end
          end
            else fsp := nil
      end (*simpletype*) ;

      procedure fieldlist(fsys: setofsys; var frecvar: stp; vartyp: stp;
                          varlab: ctp; lvl: integer);
        var lcp,lcp1,nxt,nxt1: ctp; lsp,lsp1,lsp2,lsp3,lsp4: stp;
            minsize,maxsize,lsize: addrrange; lvalu: valu;
            test: boolean; mm: boolean; varlnm, varcn, varcnt: varinx;
            tagp,tagl: ttp; mint, maxt: integer; ferr: boolean;
        procedure ordertag(var tp: ttp);
          var lp, p, p2, p3: ttp;
        begin
          if tp <> nil then begin
            lp := tp; tp := tp^.next; lp^.next := nil;
            while tp <> nil do begin
              p := tp;  tp := tp^.next; p^.next := nil; p2 := lp; p3 := nil;
              while (p^.ival > p2^.ival) and (p2^.next <> nil) do
                begin p3 := p2; p2 := p2^.next end;
              if p^.ival > p2^.ival then p2^.next := p
              else if p3 = nil then begin p^.next := lp; lp := p end
              else begin p^.next := p3^.next; p3^.next := p end
            end
          end;
          tp := lp
        end;
      begin nxt1 := nil; lsp := nil;
        if not (sy in (fsys+[ident,casesy])) then
          begin error(19); skip(fsys + [ident,casesy]) end;
        while sy = ident do
          begin nxt := nxt1;
            repeat
              if sy = ident then
                begin new(lcp,field); ininam(lcp);
                  with lcp^ do
                    begin strassvf(name, id); idtype := nil; next := nxt;
                      klass := field; varnt := vartyp; varlb := varlab;
                      tagfield := false; taglvl := lvl; varsaddr := 0;
                      varssize := 0; vartl := -1
                    end;
                  nxt := lcp;
                  enterid(lcp);
                  insymbol
                end
              else error(2);
              if not (sy in [comma,colon]) then
                begin error(6); skip(fsys + [comma,colon,semicolon,casesy])
                end;
              test := sy <> comma;
              if not test  then insymbol
            until test;
            if sy = colon then insymbol else error(5);
            typ(fsys + [casesy,semicolon],lsp,lsize);
            while nxt <> nxt1 do
              with nxt^ do
                begin alignu(lsp,displ);
                  idtype := lsp; fldaddr := displ;
                  nxt := next; displ := displ + lsize
                end;
            nxt1 := lcp;
            while sy = semicolon do
              begin insymbol;
                if not (sy in fsys + [ident,casesy,semicolon]) then
                  begin error(19); skip(fsys + [ident,casesy]) end
              end
          end (*while*);
        nxt := nil;
        while nxt1 <> nil do
          with nxt1^ do
            begin lcp := next; next := nxt; nxt := nxt1; nxt1 := lcp end;
        if sy = casesy then
          begin new(lsp,tagfld); pshstc(lsp);
            with lsp^ do
              begin form:=tagfld; tagfieldp := nil; fstvar := nil;
                    packing := false; new(vart);
                    for varcn := 0 to varmax do vart^[varcn] := 0
              end;
            varlnm := 1; varcnt := 0;
            frecvar := lsp;
            insymbol;
            lcp := nil;
            if sy = ident then
              begin
                { find possible type first }
                searchidnenm([types],lcp1,mm,false);
                { now set up as field id }
                new(lcp,field); ininam(lcp);
                with lcp^ do
                  begin strassvf(name, id); idtype := nil; klass:=field;
                    next := nil; fldaddr := displ; varnt := vartyp;
                    varlb := varlab; tagfield := true; taglvl := lvl;
                    varsaddr := 0; varssize := 0; vartl := -1
                  end;
                lsp^.tagfieldp := lcp;
                insymbol;
                if sy = colon then begin
                  enterid(lcp); insymbol;
                  if sy = ident then begin searchid([types],lcp1); insymbol end
                  else begin error(2); skip(fsys + [ofsy,lparent]); lcp1 := nil end
                end else begin
                   if lcp1 = nil then begin error(104); lcp1 := usclrptr end;
                   { If type only (undiscriminated variant), kill the id. }
                   if mm then error(103);
                   putstrs(lcp^.name); { release name string }
                   lcp^.name := nil { set no tagfield }
                end;
                if lcp1 <> nil then begin
                  lsp1 := lcp1^.idtype;
                  if lsp1 <> nil then
                    begin alignu(lsp1,displ);
                      lcp^.fldaddr := displ;
                      { only allocate field if named or if undiscriminated
                        tagfield checks are on }
                      if (lcp^.name <> nil) or chkudtf then
                        displ := displ+lsp1^.size;
                      if (lsp1^.form <= subrange) or stringt(lsp1) then
                        begin if comptypes(realptr,lsp1) then error(109)
                          else if stringt(lsp1) then error(159);
                          lcp^.idtype := lsp1
                        end
                      else error(110);
                    end
                  end
              end
            else begin error(2); skip(fsys + [ofsy,lparent]) end;
            lsp^.size := displ;
            if sy = ofsy then insymbol else error(8);
            lsp1 := nil; minsize := displ; maxsize := displ;
            tagl := nil;
            mint := -maxint; maxt := maxint;
            if lsp^.tagfieldp <> nil then
              if lsp^.tagfieldp^.idtype <> nil then begin
              getbounds(lsp^.tagfieldp^.idtype, mint, maxt);
              if maxt-mint+1 > varmax then error(239)
            end;
            repeat lsp2 := nil;
              if not (sy in fsys + [semicolon]) then
              begin
                repeat constant(fsys + [comma,colon,lparent],lsp3,lvalu);
                  gettag(tagp); tagp^.ival := lvalu.ival; tagp^.next := tagl;
                  tagl := tagp;
                  if lsp^.tagfieldp <> nil then begin
                    if not comptypes(lsp^.tagfieldp^.idtype,lsp3)then error(111);
                    if (lvalu.ival < mint) or (lvalu.ival > maxt) then error(244)
                  end;
                  new(lsp3,variant); pshstc(lsp3);
                  with lsp3^ do
                    begin form := variant; varln := varlnm;
                          nxtvar := lsp1; subvar := lsp2; varval := lvalu;
                          caslst := lsp2; packing := false
                    end;
                  if not addovf(lvalu.ival, -mint) then
                    if (lvalu.ival >= mint) and (lvalu.ival <= maxt) and
                       (lvalu.ival-mint <= varmax) then
                    lsp^.vart^[lvalu.ival-mint] := varlnm; { set case to logical }
                  lsp4 := lsp1;
                  while lsp4 <> nil do
                    with lsp4^ do
                      begin
                        if varval.ival = lvalu.ival then error(178);
                        lsp4 := nxtvar
                      end;
                  lsp1 := lsp3; lsp2 := lsp3;
                  test := sy <> comma;
                  if not test then insymbol
                until test;
                varcnt := varcnt+1;
                if sy = colon then insymbol else error(5);
                if sy = lparent then insymbol else error(9);
                alignu(nilptr, displ); { max align all variants }
                if lcp <> nil then lcp^.varsaddr := displ;
                fieldlist(fsys + [rparent,semicolon],lsp2,lsp3,lcp, lvl+1);
                if displ > maxsize then maxsize := displ;
                if lcp <> nil then lcp^.varssize := maxsize-lcp^.varsaddr;
                while lsp3 <> nil do
                  begin lsp4 := lsp3^.subvar; lsp3^.subvar := lsp2;
                    lsp3^.size := displ;
                    lsp3 := lsp4
                  end;
                if sy = rparent then
                  begin insymbol;
                    if not (sy in fsys + [semicolon]) then
                      begin error(6); skip(fsys + [semicolon]) end
                  end
                else error(4);
              end;
              varlnm := varlnm+1;
              test := sy <> semicolon;
              if not test then
                begin displ := minsize;
                      insymbol
                end
            until test;
            displ := maxsize;
            lsp^.fstvar := lsp1;
            lsp^.varts := 0;
            if lcp <> nil then begin
              if varcnt >= 0 then begin
                if not addovf(maxt, -mint) then begin
                  lsp^.varts := maxt-mint;
                  if not addovf(lsp^.varts, 1) then lsp^.varts := lsp^.varts+1
                end else lsp^.varts := varmax;
                if lsp^.varts > varmax then lsp^.varts := varmax
              end;
              { output LVN table }
              if prcode then begin
                write(prr, 'v ');
                genlabel(lcp^.vartl); prtlabel(lcp^.vartl);
                write(prr, ' ', lsp^.varts:1);
                write(prr, ' ', mint:1);
                for varcn := 0 to lsp^.varts-1 do
                  write(prr, ' ', lsp^.vart^[varcn]:1);
                writeln(prr)
              end
            end;
            if lsp^.tagfieldp <> nil then begin
              ordertag(tagl);
              tagp := tagl; ferr := false;
              while (tagp <> nil) and (mint <= maxt) and not ferr do begin
                if tagp^.ival <> mint then begin error(200); ferr := true end
                else begin mint := mint+1; tagp := tagp^.next end
              end;
              if (mint <= maxt) and not ferr then error(200)
            end;
            while tagl <> nil do
              begin tagp := tagl; tagl := tagl^.next; puttag(tagp) end
          end
        else frecvar := nil
      end (*fieldlist*) ;

    begin (*typ*)
      lsp := nil;
      if not (sy in typebegsys) then
        perror(10, fsys+typebegsys+[ident], fsys+typebegsys);
      if sy in typebegsys then
        begin
          if sy in simptypebegsys then simpletype(fsys,fsp,fsize)
          else
    (*^*)     if sy = arrow then
              begin new(lsp,pointer); pshstc(lsp); fsp := lsp;
                with lsp^ do
                  begin eltype := nil; size := ptrsize; form:=pointer;
                        packing := false end;
                insymbol;
                if sy = ident then
                  begin { forward reference everything }
                    new(lcp,types); ininam(lcp);
                    with lcp^ do
                      begin strassvf(name,id); idtype := lsp;
                        next := fwptr; klass := types
                      end;
                    fwptr := lcp;
                    insymbol;
                  end
                else error(2);
              end
            else
              begin
                ispacked := false; { set not packed by default }
                if sy = packedsy then
                  begin insymbol; ispacked := true; { packed }
                    if not (sy in typedels) then
                      begin
                        error(10); skip(fsys + typedels)
                      end
                  end;
    (*array*)     if sy = arraysy then
                  begin insymbol;
                    if sy = lbrack then insymbol else error(11);
                    lsp1 := nil;
                    repeat new(lsp,arrays); pshstc(lsp);
                      with lsp^ do
                        begin aeltype := lsp1; inxtype := nil; form:=arrays;
                              packing := ispacked end;
                      lsp1 := lsp;
                      simpletype(fsys + [comma,rbrack,ofsy],lsp2,lsize);
                      lsp1^.size := lsize;
                      if lsp2 <> nil then
                        if lsp2^.form <= subrange then
                          begin
                            if lsp2 = realptr then
                              begin error(109); lsp2 := nil end
                            else
                              if lsp2 = intptr then
                                begin error(149); lsp2 := nil end;
                            lsp^.inxtype := lsp2
                          end
                        else begin error(113); lsp2 := nil end;
                      test := sy <> comma;
                      if not test then insymbol
                    until test;
                    if sy = rbrack then insymbol else error(12);
                    if sy = ofsy then insymbol else error(8);
                    typ(fsys,lsp,lsize);
                    repeat
                      with lsp1^ do
                        begin lsp2 := aeltype; aeltype := lsp;
                          if inxtype <> nil then
                            begin getbounds(inxtype,lmin,lmax);
                              if addovf(lmax, -lmin) then
                                begin error(245); lsp1^.inxtype := nil end
                              else begin
                                span := lmax-lmin;
                                if addovf(span, 1) then
                                  begin error(245); lsp1^.inxtype := nil end
                                else begin
                                  span := span+1; if span < 1 then error(500);
                                  if mltovf(lsize, span) then
                                    begin error(245); lsp1^.inxtype := nil end
                                  else lsize := lsize*span
                                end
                              end;
                              size := lsize
                            end
                        end;
                      lsp := lsp1; lsp1 := lsp2
                    until lsp1 = nil
                  end
                else
    (*record*)      if sy = recordsy then
                    begin insymbol;
                      oldtop := top;
                      if top < displimit then
                        begin top := top + 1; scopen := scopen+1;
                          with display[top] do
                            begin fname := nil;
                                  flabel := nil;
                                  fconst := nil;
                                  fstruct := nil;
                                  packing := false;
                                  occur := rec
                            end
                        end
                      else error(250);
                      displ := 0;
                      fieldlist(fsys-[semicolon]+[endsy],lsp1,nil,nil,1);
                      new(lsp,records);
                      with lsp^ do
                        begin fstfld := display[top].fname;
                          display[top].fname := nil;
                          recvar := lsp1; size := displ; form := records;
                          packing := ispacked;
                          recyc := display[top].fstruct;
                          display[top].fstruct := nil
                        end;
                      putdsps(oldtop); top := oldtop;
                      { register the record late because of the purge above }
                      pshstc(lsp);
                      if sy = endsy then insymbol else error(13)
                    end
                  else
    (*set*)        if sy = setsy then
                      begin insymbol;
                        if sy = ofsy then insymbol else error(8);
                        simpletype(fsys,lsp1,lsize);
                        if lsp1 <> nil then
                          if lsp1^.form > subrange then
                            begin error(115); lsp1 := nil end
                          else
                            if lsp1 = realptr then
                              begin error(114); lsp1 := nil end
                            else if lsp1 = intptr then
                              begin error(169); lsp1 := nil end
                            else
                              begin getbounds(lsp1,lmin,lmax);
                                if (lmin < setlow) or (lmax > sethigh)
                                  then error(169);
                              end;
                        new(lsp,power); pshstc(lsp);
                        with lsp^ do
                          begin elset:=lsp1; size:=setsize; form:=power;
                                packing := ispacked; matchpack := true end;
                      end
                    else
    (*file*)        if sy = filesy then
                          begin insymbol;
                            if sy = ofsy then insymbol else error(8);
                            typ(fsys,lsp1,lsize);
                            if filecomponent(lsp1) then error(190);
                            new(lsp,files); pshstc(lsp);
                            with lsp^ do
                              begin filtype := lsp1; size := filesize+lsize;
                                form := files; packing := ispacked
                              end
                          end
                    else fsp := nil;
                fsp := lsp
              end;
          if not (sy in fsys) then
            begin error(6); skip(fsys) end
        end
      else fsp := nil;
      if fsp = nil then fsize := 1 else fsize := fsp^.size
    end (*typ*) ;

    procedure labeldeclaration;
      var llp: lbp;
          test: boolean;
    begin
      repeat
        if sy = intconst then begin
          searchlabel(llp, top); { search preexisting label }
          if llp <> nil then error(166) { multideclared label }
          else newlabel(llp);
          insymbol
        end else error(15);
        if not ( sy in fsys + [comma, semicolon] ) then
          begin error(6); skip(fsys+[comma,semicolon]) end;
        test := sy <> comma;
        if not test then insymbol
      until test;
      if sy = semicolon then insymbol else error(14)
    end (* labeldeclaration *) ;

    procedure constdeclaration;
      var lcp: ctp; lsp: stp; lvalu: valu;
    begin
      if sy <> ident then
        begin error(2); skip(fsys + [ident]) end;
      while sy = ident do
        begin new(lcp,konst); ininam(lcp);
          with lcp^ do
            begin strassvf(name, id); idtype := nil; next := nil; klass:=konst;
              refer := false
            end;
          insymbol;
          if (sy = relop) and (op = eqop) then insymbol else error(16);
          constant(fsys + [semicolon],lsp,lvalu);
          enterid(lcp);
          lcp^.idtype := lsp; lcp^.values := lvalu;
          if sy = semicolon then
            begin insymbol;
              if not (sy in fsys + [ident]) then
                begin error(6); skip(fsys + [ident]) end
            end
          else error(14)
        end
    end (*constdeclaration*) ;

    procedure typedeclaration;
      var lcp: ctp; lsp: stp; lsize: addrrange;
    begin
      if sy <> ident then
        begin error(2); skip(fsys + [ident]) end;
      while sy = ident do
        begin new(lcp,types); ininam(lcp);
          with lcp^ do
            begin strassvf(name, id); idtype := nil; klass := types;
              refer := false
            end;
          insymbol;
          if (sy = relop) and (op = eqop) then insymbol else error(16);
          enterid(lcp);
          lcp^.defined := false;
          typ(fsys + [semicolon],lsp,lsize);
          lcp^.defined := true;
          lcp^.idtype := lsp;
          if sy = semicolon then
            begin insymbol;
              if not (sy in fsys + [ident]) then
                begin error(6); skip(fsys + [ident]) end
            end
          else error(14)
        end;
      resolvep
    end (*typedeclaration*) ;

    procedure vardeclaration;
      var lcp,nxt: ctp; lsp: stp; lsize: addrrange;
          test: boolean;
    begin nxt := nil;
      repeat
        repeat
          if sy = ident then
            begin new(lcp,vars); ininam(lcp);
              with lcp^ do
               begin strassvf(name, id); next := nxt; klass := vars;
                  idtype := nil; vkind := actual; vlev := level;
                  refer := false; threat := false; forcnt := 0
                end;
              enterid(lcp);
              nxt := lcp;
              insymbol;
            end
          else error(2);
          if not (sy in fsys + [comma,colon] + typedels) then
            begin error(6); skip(fsys+[comma,colon,semicolon]+typedels) end;
          test := sy <> comma;
          if not test then insymbol
        until test;
        if sy = colon then insymbol else error(5);
        typ(fsys + [semicolon] + typedels,lsp,lsize);
        while nxt <> nil do
          with  nxt^ do
            begin
              idtype := lsp;
              { globals are alloc/increment, locals are decrement/alloc }
              if level <= 1 then
                begin alignu(lsp,gc); vaddr := gc; gc := gc + lsize end
              else
                begin lc := lc - lsize; alignd(lsp,lc); vaddr := lc end;
              nxt := next
            end;
        if sy = semicolon then
          begin insymbol;
            if not (sy in fsys + [ident]) then
              begin error(6); skip(fsys + [ident]) end
          end
        else error(14)
      until (sy <> ident) and not (sy in typedels);
      resolvep
    end (*vardeclaration*) ;

    procedure procdeclaration(fsy: symbol);
      var oldlev: 0..maxlevel; lcp,lcp1: ctp; lsp: stp;
          forw: boolean; oldtop: disprange;
          llc: stkoff; lbname: integer; {markp: marktype;}

      procedure pushlvl(forw: boolean; lcp: ctp);
      begin
        if level < maxlevel then level := level + 1 else error(251);
        if top < displimit then
          begin top := top + 1; scopen := scopen+1;
            with display[top] do
              begin
                if forw then fname := lcp^.pflist
                else fname := nil;
                flabel := nil; fconst := nil; fstruct := nil; packing := false;
                occur := blck;
                bname := lcp
              end
          end
        else error(250);
      end;

      procedure parameterlist(fsy: setofsys; var fpar: ctp);
        var lcp,lcp1,lcp2,lcp3: ctp; lsp: stp; lkind: idkind;
          llc,lsize: addrrange; count: integer;
          oldlev: 0..maxlevel; oldtop: disprange;
          lcs: addrrange;
          test: boolean;
      procedure joinlists;
      var lcp, lcp3: ctp;
      begin
        { we missed the type for this id list, meaning the types are nil. Add
          the new list as is for error recovery }
        if lcp2 <> nil then begin
          lcp3 := lcp2; { save sublist head }
          { find sublist end }
          lcp := nil;
          while lcp2 <> nil do begin lcp := lcp2; lcp2 := lcp2^.next end;
          { join lists }
          lcp^.next := lcp1;
          lcp1 := lcp3
        end
      end;
      begin lcp1 := nil;
        if not (sy in fsy + [lparent]) then
          begin error(7); skip(fsys + fsy + [lparent]) end;
        if sy = lparent then
          begin if forw then error(119);
            insymbol;
            if not (sy in [ident,varsy,procsy,funcsy]) then
              begin error(7); skip(fsys + [ident,rparent]) end;
            while sy in [ident,varsy,procsy,funcsy] do
              begin
                if sy = procsy then
                  begin
                    insymbol; lcp := nil;
                    if sy = ident then
                      begin new(lcp,proc,declared,formal); ininam(lcp);
                        lc := lc-ptrsize*2; { mp and addr }
                        alignd(parmptr,lc);
                        with lcp^ do
                          begin strassvf(name, id); idtype := nil; next := lcp1;
                            pflev := level (*beware of parameter procedures*);
                            klass:=proc;pfdeckind:=declared;
                            pfkind:=formal; pfaddr := lc; keep := true
                          end;
                        enterid(lcp);
                        lcp1 := lcp;
                        insymbol
                      end
                    else error(2);
                    oldlev := level; oldtop := top; pushlvl(false, lcp);
                    lcs := lc; parameterlist([semicolon,rparent],lcp2); lc := lcs;
                    if lcp <> nil then lcp^.pflist := lcp2;
                    if not (sy in fsys+[semicolon,rparent]) then
                      begin error(7);skip(fsys+[semicolon,rparent]) end;
                    level := oldlev; putdsps(oldtop); top := oldtop
                  end
                else
                  begin
                    if sy = funcsy then
                      begin lcp2 := nil;
                        insymbol;
                        if sy = ident then
                          begin new(lcp,func,declared,formal); ininam(lcp);
                            lc := lc-ptrsize*2; { mp and addr }
                            alignd(parmptr,lc);
                            with lcp^ do
                              begin strassvf(name, id); idtype := nil; next := lcp1;
                                pflev := level (*beware param funcs*);
                                klass:=func;pfdeckind:=declared;
                                pfkind:=formal; pfaddr:=lc; keep := true
                              end;
                            enterid(lcp);
                            lcp1 := lcp;
                            insymbol;
                          end
                        else error(2);
                        oldlev := level; oldtop := top; pushlvl(false, lcp);
                        lcs := lc; parameterlist([colon,semicolon,rparent],lcp2); lc := lcs;
                        if lcp <> nil then lcp^.pflist := lcp2;
                        if not (sy in fsys+[colon]) then
                          begin error(7);skip(fsys+[comma,semicolon,rparent]) end;
                        if sy = colon then
                          begin insymbol;
                            if sy = ident then
                              begin searchid([types],lcp2);
                                lsp := lcp2^.idtype;
                                lcp^.idtype := lsp;
                                if lsp <> nil then
                                 if not(lsp^.form in[scalar,subrange,pointer])
                                    then begin error(120); lsp := nil end;
                                insymbol
                              end
                            else error(2);
                            if not (sy in fsys + [semicolon,rparent]) then
                              begin error(7);skip(fsys+[semicolon,rparent])end
                          end
                        else error(5);
                        level := oldlev; putdsps(oldtop); top := oldtop
                      end
                    else
                      begin
                        if sy = varsy then
                          begin lkind := formal; insymbol end
                        else lkind := actual;
                        lcp2 := nil;
                        count := 0;
                        repeat
                          if sy = ident then
                            begin new(lcp,vars); ininam(lcp);
                              with lcp^ do
                                begin strassvf(name,id); idtype:=nil; klass:=vars;
                                  vkind := lkind; next := lcp2; vlev := level;
                                  keep := true; refer := false; threat := false;
                                  forcnt := 0
                                end;
                              enterid(lcp);
                              lcp2 := lcp; count := count+1;
                              insymbol;
                            end
                          else error(2);
                          if not (sy in [comma,colon] + fsys) then
                            begin error(7);skip(fsys+[comma,semicolon,rparent])
                            end;
                          test := sy <> comma;
                          if not test then insymbol
                        until test;
                        if sy = colon then
                          begin insymbol;
                            if sy = ident then
                              begin searchid([types],lcp);
                                lsp := lcp^.idtype;
                                lsize := ptrsize;
                                if lsp <> nil then
                                  if lkind=actual then begin
                                    if lsp^.form<=power then lsize := lsp^.size
                                    else if lsp^.form=files then error(121);
                                    { type containing file not allowed either }
                                    if filecomponent(lsp) then error(121)
                                  end;
                                alignu(parmptr,lsize);
                                lcp3 := lcp2;
                                lc := lc-count*lsize;
                                alignd(parmptr,lc);
                                llc := lc;
                                while lcp2 <> nil do
                                  begin lcp := lcp2;
                                    with lcp2^ do
                                      begin idtype := lsp;
                                        vaddr := llc;
                                        llc := llc+lsize;
                                      end;
                                    lcp2 := lcp2^.next
                                  end;
                                lcp^.next := lcp1; lcp1 := lcp3;
                                insymbol
                              end
                            else begin error(2); joinlists end;
                            if not (sy in fsys + [semicolon,rparent]) then
                              begin error(7);skip(fsys+[semicolon,rparent])end
                          end
                        else begin error(5); joinlists end
                      end;
                  end;
                if sy = semicolon then
                  begin insymbol;
                    if not (sy in fsys + [ident,varsy,procsy,funcsy]) then
                      begin error(7); skip(fsys + [ident,rparent]) end
                  end
              end (*while*) ;
            if sy = rparent then
              begin insymbol;
                if not (sy in fsy + fsys) then
                  begin error(6); skip(fsy + fsys) end
              end
            else error(4);
            lcp3 := nil;
            (*reverse pointers and reserve local cells for copies of multiple
             values*)
            while lcp1 <> nil do
              with lcp1^ do
                begin lcp2 := next; next := lcp3;
                  if klass = vars then
                    if idtype <> nil then
                      if (vkind=actual)and(idtype^.form>power) then
                        begin
                          lc := lc-idtype^.size;
                          alignd(idtype,lc);
                          vaddr := lc
                        end;
                  lcp3 := lcp1; lcp1 := lcp2
                end;
            fpar := lcp3
          end
            else fpar := nil
    end (*parameterlist*) ;

    begin (*procdeclaration*)
      llc := lc; lc := lcaftermarkstack; forw := false;
      if sy = ident then
        begin searchsection(display[top].fname,lcp); (*decide whether forw.*)
          if lcp <> nil then
            begin
              if lcp^.klass = proc then
                forw := lcp^.forwdecl and(fsy=procsy)and(lcp^.pfkind=actual)
              else
                if lcp^.klass = func then
                  forw:=lcp^.forwdecl and(fsy=funcsy)and(lcp^.pfkind=actual)
                else forw := false;
              if not forw then error(160)
            end;
          if not forw then
            begin
              if fsy = procsy then new(lcp,proc,declared,actual)
              else new(lcp,func,declared,actual);
              ininam(lcp);
              with lcp^ do
                begin strassvf(name, id); idtype := nil; next := nil;
                  externl := false; pflev := level; genlabel(lbname);
                  pfdeckind := declared; pfkind := actual; pfname := lbname;
                  if fsy = procsy then klass := proc
                  else begin klass := func; asgn := false end;
                  refer := false
                end;
              enterid(lcp)
            end
          else
            begin lcp1 := lcp^.pflist;
              while lcp1 <> nil do
                begin
                  with lcp1^ do
                    if klass = vars then
                      if idtype <> nil then
                          if vaddr < lc then lc := vaddr;
                  lcp1 := lcp1^.next
                end
            end;
          insymbol
        end
      else
        begin error(2);
          if fsy = procsy then lcp := uprcptr else lcp := ufctptr
        end;
      oldlev := level; oldtop := top; pushlvl(forw, lcp);
      if fsy = procsy then
        begin parameterlist([semicolon],lcp1);
          if not forw then lcp^.pflist := lcp1 else putidlst(lcp1)
        end
      else
        begin parameterlist([semicolon,colon],lcp1);
          if not forw then lcp^.pflist := lcp1 else putidlst(lcp1);
          if sy = colon then
            begin insymbol;
              if sy = ident then
                begin if forw then error(122);
                  searchid([types],lcp1);
                  lsp := lcp1^.idtype;
                  lcp^.idtype := lsp;
                  if lsp <> nil then
                    if not (lsp^.form in [scalar,subrange,pointer]) then
                      begin error(120); lcp^.idtype := nil end;
                  insymbol
                end
              else begin error(2); skip(fsys + [semicolon]) end
            end
          else
            if not forw then error(123)
        end;
      level := oldlev; putdsps(oldtop); top := oldtop;
      if sy = semicolon then insymbol else error(14);
      if (sy = ident) and strequri('forward  ', id) then
        begin
          if forw then error(161)
          else lcp^.forwdecl := true;
          insymbol;
          if sy = semicolon then insymbol else error(14);
          if not (sy in fsys) then
            begin error(6); skip(fsys) end
        end
      else
        begin lcp^.forwdecl := false;
          oldlev := level; oldtop := top; pushlvl(true, lcp);
          repeat block(fsys,semicolon,lcp);
            if sy = semicolon then
              begin if prtables then printtables(false); insymbol;
                if not (sy in [beginsy,procsy,funcsy]) then
                  begin error(6); skip(fsys) end
              end
            else error(14)
          until (sy in [beginsy,procsy,funcsy]) or eof(prd);
          if lcp^.klass = func then
            if lcp <> ufctptr then
              if not lcp^.asgn then error(193); { no function result assign }
          level := oldlev; putdsps(oldtop); top := oldtop;
        end;
      lc := llc;
    end (*procdeclaration*) ;

    procedure body(fsys: setofsys);
      const cstoccmax=4000; cixmax=10000;
      type oprange = 0..maxins;
      var
          llcp:ctp; saveid:idstr;
          cstptr: array [1..cstoccmax] of csp;
          cstptrix: 0..cstoccmax;
          (*allows referencing of noninteger constants by an index
           (instead of a pointer), which can be stored in the p2-field
           of the instruction record until writeout.
           --> procedure load, procedure writeout*)
          entname, segsize, gblsize: integer;
          stackbot, topnew, topmin: integer;
          lcmin: stkoff;
          llc1: stkoff; lcp: ctp;
          llp: lbp;
          fp: extfilep;
          test: boolean;
          printed: boolean;
          frlab: integer;

      { add statement level }
      procedure addlvl;
      begin
        stalvl := stalvl+1
      end;

      { remove statement level }
      procedure sublvl;
      var llp: lbp;
      begin
         stalvl := stalvl-1;
         { traverse label list for current block and remove any label from
           active status whose statement block has closed }
         llp := display[top].flabel;
         while llp <> nil do with llp^ do begin
           if slevel > stalvl then bact := false;
           if refer and (minlvl > stalvl) then
             minlvl := stalvl;
           llp := nextlab { link next }
         end
      end;

      procedure mesl(i: integer);
      begin topnew := topnew + i;
        if topnew < topmin then topmin := topnew;
        if toterr = 0 then
          if (topnew > 0) and prcode then error(500) { stack should never go positive }
      end;

      procedure mes(i: integer);
      begin mesl(cdx[i]) end;

      procedure mest(i: integer; fsp: stp);

        function mestn(fsp: stp): integer;
        var ss: integer;
        begin ss := 1;
          if fsp<>nil then
            with fsp^ do
              case form of
               scalar: if fsp=intptr then ss := 1
                       else
                         if fsp=boolptr then ss := 3
                         else
                           if fsp=charptr then ss := 4
                           else
                             if scalkind = declared then ss := 1
                             else ss := 2;
               subrange: ss := mestn(rangetype);
               pointer:  ss := 5;
               power:    ss := 6;
               records,arrays: ss := 7;
               files:    ss := 5;
               tagfld,variant: error(500)
              end;
          mestn := ss
        end;

      begin (*mest*)
        if (cdx[i] < 1) or (cdx[i] > 7) then error(500);
        mesl(cdxs[cdx[i]][mestn(fsp)]);
      end (*mest*);

      procedure putic;
      begin if ic mod 10 = 0 then writeln(prr,'!',ic:5) end;

      procedure gen0(fop: oprange);
      begin
        if prcode then begin putic; writeln(prr,mn[fop]:4) end;
        ic := ic + 1; mes(fop)
      end (*gen0*) ;

      procedure gen1(fop: oprange; fp2: integer);
        var k, j: integer; p: strvsp;
      begin
        if prcode then
          begin putic; write(prr,mn[fop]:4);
            if fop = 30 then
              begin writeln(prr,sna[fp2]:12);
                mesl(pdx[fp2]);
              end
            else
              begin
                if fop = 38 then
                   begin with cstptr[fp2]^ do begin p := sval; j := 1;
                       write(prr,' ',slgth:4,' ''');
                       for k := 1 to lenpv(p) do begin
                         if p^.str[j] = '''' then write(prr, '''''')
                         else write(prr,p^.str[j]:1);
                         j := j+1; if j > varsqt then begin
                           p := p^.next; j := 1
                         end
                       end
                     end;
                     writeln(prr,'''')
                   end
                else if fop = 42 then writeln(prr,chr(fp2))
                else if fop = 67 then writeln(prr,fp2:4)
                else writeln(prr,fp2:12);
                if fop = 42 then mesl(0)
                else mes(fop)
              end
          end;
        ic := ic + 1
      end (*gen1*) ;

      procedure gen2(fop: oprange; fp1,fp2: integer);
        var k : integer;
      begin
        if prcode then
          begin putic; write(prr,mn[fop]:4);
            case fop of
              45,50,54,56,74,62,63,81,82:
                begin
                  writeln(prr,' ',fp1:3,' ',fp2:8);
                  mes(fop)
                end;
              47,48,49,52,53,55:
                begin write(prr,chr(fp1));
                  if chr(fp1) = 'm' then write(prr,' ',fp2:11);
                  writeln(prr);
                  case chr(fp1) of
                    'i': mesl(cdxs[cdx[fop]][1]);
                    'r': mesl(cdxs[cdx[fop]][2]);
                    'b': mesl(cdxs[cdx[fop]][3]);
                    'c': mesl(cdxs[cdx[fop]][4]);
                    'a': mesl(cdxs[cdx[fop]][5]);
                    's': mesl(cdxs[cdx[fop]][6]);
                    'm': mesl(cdxs[cdx[fop]][7])
                  end
                end;
              51:
                begin
                  case fp1 of
                    1: begin writeln(prr,'i ',fp2:1);
                         mesl(cdxs[cdx[fop]][1])
                       end;
                    2: begin write(prr,'r ');
                         with cstptr[fp2]^ do writev(prr,rval,lenpv(rval));
                         writeln(prr);
                         mesl(cdxs[cdx[fop]][2]);
                       end;
                    3: begin writeln(prr,'b ',fp2:1);
                         mesl(cdxs[cdx[fop]][3])
                       end;
                    4: begin writeln(prr,'n');
                         mesl(-ptrsize)
                       end;
                    6: begin
                    if chartp[chr(fp2)] = illegal then
                         { output illegal characters as numbers }
                         writeln(prr,'c  ':3,fp2:1)
                       else
                         writeln(prr,'c ''':3,chr(fp2),'''');
                         mesl(cdxs[cdx[fop]][4])
                       end;
                    5: begin write(prr,'s(');
                         with cstptr[fp2]^ do
                           for k := setlow to sethigh do
                             (* increased for testing [sam] *)
                             if k in pval then write(prr,k:7(*3*));
                         writeln(prr,')');
                         mesl(cdxs[cdx[fop]][6])
                       end
                  end
                end
            end
          end;
        ic := ic + 1
      end (*gen2*) ;

      procedure gentypindicator(fsp: stp);
      begin
        if fsp<>nil then
          with fsp^ do
            case form of
             scalar: if fsp=intptr then write(prr,'i')
                     else
                       if fsp=boolptr then write(prr,'b')
                       else
                         if fsp=charptr then write(prr,'c')
                         else
                           if scalkind = declared then begin
                             if fsp^.size = 1 then write(prr, 'x')
                             else write(prr,'i')
                           end else write(prr,'r');
             subrange: if fsp^.size = 1 then write(prr, 'x')
                       else gentypindicator(rangetype);
             pointer:  write(prr,'a');
             power:    write(prr,'s');
             records,arrays: write(prr,'m');
             files:    write(prr,'a');
             tagfld,variant: error(500)
            end
      end (*typindicator*);

      procedure gen0t(fop: oprange; fsp: stp);
      begin
        if prcode then
          begin putic;
            write(prr,mn[fop]:4);
            gentypindicator(fsp);
            writeln(prr);
          end;
        ic := ic + 1; mest(fop, fsp)
      end (*gen0t*);

      procedure gen1t(fop: oprange; fp2: integer; fsp: stp);
      begin
        if prcode then
          begin putic;
            write(prr,mn[fop]:4);
            gentypindicator(fsp);
            writeln(prr,' ',fp2:11)
          end;
        ic := ic + 1; mest(fop, fsp)
      end (*gen1t*);

      procedure gen2t(fop: oprange; fp1,fp2: integer; fsp: stp);
      begin
        if prcode then
          begin putic;
            write(prr,mn[fop]: 4);
            gentypindicator(fsp);
            writeln(prr,' ', fp1:3+5*ord(abs(fp1)>99),' ',fp2:11);
          end;
        ic := ic + 1; mest(fop, fsp)
      end (*gen2t*);

      procedure load;
      begin
        with gattr do
          if typtr <> nil then
            begin
              case kind of
                cst:   if (typtr^.form = scalar) and (typtr <> realptr) then
                         if typtr = boolptr then gen2(51(*ldc*),3,cval.ival)
                         else
                           if typtr=charptr then
                             gen2(51(*ldc*),6,cval.ival)
                           else gen2(51(*ldc*),1,cval.ival)
                       else
                         if typtr = nilptr then gen2(51(*ldc*),4,0)
                         else
                           if cstptrix >= cstoccmax then error(254)
                           else
                             begin cstptrix := cstptrix + 1;
                               cstptr[cstptrix] := cval.valp;
                               if typtr = realptr then
                                 gen2(51(*ldc*),2,cstptrix)
                               else
                                 gen2(51(*ldc*),5,cstptrix)
                             end;
                varbl: case access of
                         drct:   if vlevel<=1 then gen1t(39(*ldo*),dplmt,typtr)
                                 else gen2t(54(*lod*),abs(level-vlevel),dplmt,typtr);
                         indrct: gen1t(35(*ind*),idplmt,typtr);
                         inxd:   error(400)
                       end;
                expr:
              end;
              kind := expr;
              { operand is loaded, and subranges are now normalized to their
                base type }
              typtr := basetype(typtr)
            end
      end (*load*) ;

      procedure store(var fattr: attr);
      begin
        with fattr do
          if typtr <> nil then
            case access of
              drct:   if vlevel <= 1 then gen1t(43(*sro*),dplmt,typtr)
                      else gen2t(56(*str*),abs(level-vlevel),dplmt,typtr);
              indrct: if idplmt <> 0 then error(400)
                      else gen0t(26(*sto*),typtr);
              inxd:   error(400)
            end
      end (*store*) ;

      procedure loadaddress;
      begin
        with gattr do
          if typtr <> nil then
            begin
              case kind of
                cst:   if stringt(typtr) then
                         if cstptrix >= cstoccmax then error(254)
                         else
                           begin cstptrix := cstptrix + 1;
                             cstptr[cstptrix] := cval.valp;
                             gen1(38(*lca*),cstptrix)
                           end
                       else error(400);
                varbl: case access of
                         drct:   if vlevel <= 1 then gen1(37(*lao*),dplmt)
                                 else gen2(50(*lda*),abs(level-vlevel),dplmt);
                         indrct: if idplmt <> 0 then
                                   gen1t(34(*inc*),idplmt,nilptr);
                         inxd:   error(400)
                       end;
                expr:  error(400)
              end;
              kind := varbl; access := indrct; idplmt := 0; packing := false
            end
      end (*loadaddress*) ;

      procedure genfjp(faddr: integer);
      begin load;
        if gattr.typtr <> nil then
          if gattr.typtr <> boolptr then error(144);
        if prcode then begin putic; writeln(prr,mn[33]:4,' l':8,faddr:4) end;
        ic := ic + 1; mes(33)
      end (*genfjp*) ;

      procedure genujpxjp(fop: oprange; fp2: integer);
      begin
       if prcode then
          begin putic; writeln(prr, mn[fop]:4, ' l':8,fp2:4) end;
        ic := ic + 1; mes(fop)
      end (*genujpxjp*);

      procedure genipj(fop: oprange; fp1, fp2: integer);
      begin
       if prcode then
          begin putic; writeln(prr, mn[fop]:4,fp1:4,' l':8,fp2:4) end;
        ic := ic + 1; mes(fop)
      end (*genipj*);

      procedure gencupent(fop: oprange; fp1,fp2: integer);
      begin
        if prcode then
          begin putic;
            if fop = 32 then begin { create ents or ente instructions }
              if fp1 = 1 then writeln(prr,mn[fop]:4,'s','l':8,fp2:4)
              else writeln(prr,mn[fop]:4,'e','l':8,fp2:4);
              mes(fop)
            end else begin
              writeln(prr,mn[fop]:4,fp1:4,'l':4,fp2:4);
              mesl(fp1)
            end
          end;
        ic := ic + 1
      end;

      procedure genlpa(fp1,fp2: integer);
      begin
        if prcode then
          begin putic;
            writeln(prr,mn[68]:4,fp2:4,'l':4,fp1:4)
          end;
        ic := ic + 1; mes(68)
      end (*genlpa*);

      procedure genctaivtcvb(fop: oprange; fp1,fp2,fp3: integer; fsp: stp);
      begin if fp3 < 0 then error(511);
        if prcode then
          begin putic; write(prr,mn[fop]:4);
            if fop <> 81(*cta*) then gentypindicator(fsp);
            write(prr,' ',fp1:3,' ',fp2:8,' ');
            mes(fop); putlabel(fp3)
          end;
        ic := ic + 1
        end (*gen2*) ;

      procedure genmst(lev: levrange; lb: integer);
      begin
        if prcode then begin
          putic; write(prr,mn[41(*mst*)]:4); write(prr,lev:12, ' '); prtlabel(lb);
          writeln(prr)
        end
      end;

      procedure checkbnds(fsp: stp);
        var lmin,lmax: integer;
            fsp2: stp;
      begin
        if debug then begin
          { if set use the base type for the check }
          fsp2 := fsp;
          if fsp <> nil then begin
            if fsp^.form = power then fsp := fsp^.elset;
            if fsp <> nil then
              if fsp <> intptr then
                if fsp <> realptr then
                  if fsp^.form <= subrange then
                    begin
                      getbounds(fsp,lmin,lmax);
                      gen2t(45(*chk*),lmin,lmax,fsp2)
                    end
          end
        end
      end (*checkbnds*);

      procedure statement(fsys: setofsys);
        var lcp: ctp; llp: lbp;

        procedure expression(fsys: setofsys; threaten: boolean); forward;

        function taggedrec(fsp: stp): boolean;
        var b: boolean;
        begin b := false;
          if fsp <> nil then
            if fsp^.form = tagfld then b := true
            else if fsp^.form = records then
              if fsp^.recvar <> nil then
                b := fsp^.recvar^.form = tagfld;
          taggedrec := b
        end;

        procedure selector(fsys: setofsys; fcp: ctp);
        var lattr: attr; lcp: ctp; lsize: addrrange; lmin,lmax: integer;
            id: stp;
        function schblk(fcp: ctp): boolean;
        var i: disprange; f: boolean;
        begin
           f := false;
           for i := level downto 2 do if display[i].bname = fcp then f := true;
           schblk := f
        end;
        procedure checkvrnt(lcp: ctp);
        var vp: stp; vl: ctp; gattrs: attr;
        begin
          if chkvar then begin
            if lcp^.klass = field then begin
              vp := lcp^.varnt; vl := lcp^.varlb;
              if (vp <> nil) and (vl <> nil) then
                if (vl^.name <> nil) or chkudtf then begin { is a variant }
                if chkudtf and (vl^.name = nil) and (vp <> nil) then begin
                  { tagfield is unnamed and checking is on, force tagfield
                    assignment }
                  gattrs := gattr;
                  with gattr, vl^ do begin
                    typtr := idtype;
                    case access of
                      drct:   dplmt := dplmt + fldaddr;
                      indrct: begin
                                idplmt := idplmt + fldaddr;
                                gen0t(76(*dup*),nilptr)
                              end;
                      inxd:   error(400)
                    end;
                    loadaddress;
                    gen2(51(*ldc*),1,vp^.varval.ival);
                    if chkvbk then
                      genctaivtcvb(65(*cvb*),vl^.varsaddr-fldaddr,vl^.varssize,
                                   vl^.vartl,vl^.idtype);
                    if debug then
                      genctaivtcvb(82(*ivt*),vl^.varsaddr-fldaddr,vl^.varssize,
                                   vl^.vartl,vl^.idtype);
                    gen0t(26(*sto*),basetype(idtype));
                  end;
                  gattr := gattrs
                end;
                gattrs := gattr;
                with gattr, vl^ do begin
                  typtr := idtype;
                  case access of
                    drct:   dplmt := dplmt + fldaddr;
                    indrct: begin
                              idplmt := idplmt + fldaddr;
                              gen0t(76(*dup*),nilptr)
                            end;
                    inxd:   error(400)
                  end;
                  load;
                  gen0(78(*cks*));
                  while vp <> nil do begin
                    gen1t(75(*ckv*),vp^.varval.ival, basetype(idtype));
                    vp := vp^.caslst
                  end;
                  gen0(77(*cke*));
                end;
                gattr := gattrs
              end
            end
          end
        end;
        begin { selector }
          with fcp^, gattr do
            begin typtr := idtype; kind := varbl; packing := false;
              packcom := false; tagfield := false; ptrref := false;
              vartl := -1; ptrrec := false;
              case klass of
                vars: begin
                    if typtr <> nil then packing := typtr^.packing;
                    if vkind = actual then
                      begin access := drct; vlevel := vlev;
                        dplmt := vaddr
                      end
                    else
                      begin gen2t(54(*lod*),abs(level-vlev),vaddr,nilptr);
                        access := indrct; idplmt := 0
                      end;
                  end;
                field:
                  with display[disx] do begin
                    gattr.packcom := display[disx].packing;
                    if typtr <> nil then
                      gattr.packing := display[disx].packing or typtr^.packing;
                    gattr.tagfield := fcp^.tagfield;
                    gattr.taglvl := fcp^.taglvl;
                    gattr.varnt := fcp^.varnt;
                    gattr.ptrrec := display[disx].ptrrec;
                    if gattr.tagfield then
                      gattr.vartagoff := fcp^.varsaddr-fldaddr;
                    gattr.varssize := fcp^.varssize;
                    gattr.vartl := fcp^.vartl;
                    if occur = crec then
                      begin access := drct; vlevel := clev;
                        dplmt := cdspl + fldaddr
                      end
                    else if occur = vrec then
                      begin
                        { override to local for with statement }
                        gen2t(54(*lod*),0,vdspl,nilptr);
                        access := indrct; idplmt := fldaddr
                      end
                    else
                      begin
                        if level = 1 then gen1t(39(*ldo*),vdspl,nilptr)
                        else gen2t(54(*lod*),0,vdspl,nilptr);
                        access := indrct; idplmt := fldaddr
                      end
                  end;
                func:
                  if pfdeckind = standard then
                    begin error(150); typtr := nil end
                  else
                    begin
                      if pfkind = formal then error(151)
                      else
                        if not schblk(fcp) then error(192);
                        begin access := drct; vlevel := pflev + 1;
                          { determine size of FR. This is a bit of a hack
                            against the fact that int/ptr results fit in
                            the upper half of the FR. }
                          id := basetype(fcp^.idtype);
                          lsize := parmsize; if id <> nil then lsize := id^.size;
                          dplmt := 0   (*impl. relat. addr. of fct. result*)
                        end
                    end
              end (*case*)
            end (*with*);
          if not (sy in selectsys + fsys) then
            begin error(59); skip(selectsys + fsys) end;
          while sy in selectsys do
            begin
        (*[*) if sy = lbrack then
                begin gattr.ptrref := false; gattr.ptrrec := false;
                  repeat lattr := gattr;
                    with lattr do
                      if typtr <> nil then begin
                        if typtr^.form <> arrays then
                          begin error(138); typtr := nil end
                      end;
                    loadaddress;
                    insymbol; expression(fsys + [comma,rbrack], false);
                    load;
                    if gattr.typtr <> nil then
                      if gattr.typtr^.form<>scalar then error(113)
                      else if not comptypes(gattr.typtr,intptr) then
                             gen0t(58(*ord*),gattr.typtr);
                    if lattr.typtr <> nil then
                      with lattr.typtr^ do
                        begin
                          if comptypes(inxtype,gattr.typtr) then
                            begin
                              if inxtype <> nil then
                                begin getbounds(inxtype,lmin,lmax);
                                  if debug then
                                    gen2t(45(*chk*),lmin,lmax,intptr);
                                  if lmin>0 then gen1t(31(*dec*),lmin,intptr)
                                  else if lmin<0 then
                                    gen1t(34(*inc*),-lmin,intptr);
                                  (*or simply gen1(31,lmin)*)
                                end
                            end
                          else error(139);
                          with gattr do
                            begin typtr := aeltype; kind := varbl;
                              access := indrct; idplmt := 0; packing := false;
                              packcom := false; tagfield := false; ptrref := false;
                              vartl := -1; ptrrec := false;
                            end;
                          if gattr.typtr <> nil then
                            begin
                              gattr.packcom := lattr.packing;
                              gattr.packing :=
                                lattr.packing or gattr.typtr^.packing;
                              lsize := gattr.typtr^.size;
                              gen1(36(*ixa*),lsize)
                            end
                        end
                  until sy <> comma;
                  if sy = rbrack then insymbol else error(12)
                end (*if sy = lbrack*)
              else
        (*.*)   if sy = period then
                  begin
                    with gattr do
                      begin
                        if typtr <> nil then begin
                          if typtr^.form <> records then
                            begin error(140); typtr := nil end
                        end;
                        insymbol;
                        if sy = ident then
                          begin
                            if typtr <> nil then
                              begin searchsection(typtr^.fstfld,lcp);
                                if lcp = nil then
                                  begin error(152); typtr := nil end
                                else
                                  with lcp^ do
                                    begin checkvrnt(lcp);
                                      typtr := idtype;
                                      gattr.packcom := gattr.packing;
                                      if typtr <> nil then
                                        gattr.packing :=
                                          gattr.packing or typtr^.packing;
                                      gattr.tagfield := lcp^.tagfield;
                                      gattr.taglvl := lcp^.taglvl;
                                      gattr.varnt := lcp^.varnt;
                                      if gattr.tagfield then
                                        gattr.vartagoff := lcp^.varsaddr-fldaddr;
                                      gattr.varssize := lcp^.varssize;
                                      gattr.vartl := lcp^.vartl;
                                      gattr.ptrrec := gattr.ptrref;
                                      gattr.ptrref := false;
                                      case access of
                                        drct:   dplmt := dplmt + fldaddr;
                                        indrct: idplmt := idplmt + fldaddr;
                                        inxd:   error(400)
                                      end
                                    end
                              end;
                            insymbol
                          end (*sy = ident*)
                        else error(2)
                      end (*with gattr*)
                  end (*if sy = period*)
                else
        (*^*)     begin
                    if gattr.typtr <> nil then
                      with gattr,typtr^ do
                        if form = pointer then
                          begin load; typtr := eltype;
                            if debug then begin
                               if taggedrec(eltype) then
                                 gen2t(80(*ckl*),1,maxaddr,nilptr)
                               else gen2t(45(*chk*),1,maxaddr,nilptr);
                            end;
                            with gattr do
                              begin kind := varbl; access := indrct;
                                idplmt := 0; packing := false;
                                packcom := false; tagfield := false;
                                ptrref := true; vartl := -1; ptrrec := false
                              end
                          end
                        else
                          if form = files then begin loadaddress;
                             { generate buffer validate for file }
                             if typtr = textptr then gen1(30(*csp*), 46(*fbv*))
                             else begin
                               gen2(51(*ldc*),1,filtype^.size);
                               gen1(30(*csp*),47(*fvb*))
                             end;
                             { index buffer }
                             gen1t(34(*inc*),fileidsize,gattr.typtr);
                             typtr := filtype;
                          end else error(141);
                    insymbol
                  end;
              if not (sy in fsys + selectsys) then
                begin error(6); skip(fsys + selectsys) end
            end (*while*)
        end (*selector*) ;

        procedure call(fsys: setofsys; fcp: ctp);
          var lkey: 1..18;

          procedure variable(fsys: setofsys; threaten: boolean);
            var lcp: ctp;
          begin
            if sy = ident then
              begin searchid([vars,field],lcp); insymbol end
            else begin error(2); lcp := uvarptr end;
            if threaten and (lcp^.klass = vars) then with lcp^ do begin
              if vlev < level then threat := true;
              if forcnt > 0 then error(195);
            end;
            selector(fsys,lcp)
          end (*variable*) ;

          procedure getputresetrewriteprocedure;
          begin variable(fsys + [rparent], false); loadaddress;
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> files then error(116);
            if lkey <= 2 then begin
              if gattr.typtr = textptr then gen1(30(*csp*),lkey(*get,put*))
              else begin
                if gattr.typtr <> nil then
                  gen2(51(*ldc*),1,gattr.typtr^.filtype^.size);
                if lkey = 1 then gen1(30(*csp*),38(*gbf*))
                else gen1(30(*csp*),39(*pbf*))
              end
            end else
              if gattr.typtr = textptr then begin
                if lkey = 3 then gen1(30(*csp*),25(*reset*))
                else gen1(30(*csp*),26(*rewrite*))
              end else begin
                if lkey = 3 then gen1(30(*csp*),36(*reset*))
                else gen1(30(*csp*),37(*rewrite*))
              end
          end (*getputresetrewrite*) ;

          procedure pageprocedure;
          begin
            if sy = lparent then
            begin insymbol;
              variable(fsys + [rparent], false); loadaddress;
              if gattr.typtr <> nil then
                if gattr.typtr <> textptr then error(116);
              if sy = rparent then insymbol else error(4)
            end else begin
              if not outputhdf and not outputerr then
                begin error(176); outputerr := true end;
              gen1(37(*lao*),outputptr^.vaddr);
            end;
            gen1(30(*csp*),24(*page*))
          end (*page*) ;

          procedure readprocedure;
            var lsp : stp;
                txt: boolean; { is a text file }
                deffil: boolean; { default file was loaded }
                test: boolean;
                lmin,lmax: integer;
          begin
            txt := true; deffil := true;
            if sy = lparent then
              begin insymbol;
                variable(fsys + [comma,rparent], true);
                lsp := gattr.typtr; test := false;
                if lsp <> nil then
                  if lsp^.form = files then
                    with gattr, lsp^ do
                      begin
                        txt := lsp = textptr;
                        if not txt and (lkey = 11) then error(116);
                        loadaddress; deffil := false;
                        if sy = rparent then
                          begin if lkey = 5 then error(116);
                            test := true
                          end
                        else
                          if sy <> comma then
                            begin error(116); skip(fsys + [comma,rparent]) end;
                        if sy = comma then
                          begin insymbol; variable(fsys + [comma,rparent], true)
                          end
                        else test := true
                      end
                  else if not inputhdf and not inputerr then
                    begin error(175); inputerr := true end;
               if not test then
                repeat loadaddress;
                  if deffil then begin
                    { file was not loaded, we load and swap so that it ends up
                      on the bottom.}
                    gen1(37(*lao*),inputptr^.vaddr);
                    gen1(72(*swp*),ptrsize); { note 2nd is always pointer }
                    deffil := false
                  end;
                  if txt then begin
                    if gattr.typtr <> nil then
                      if gattr.typtr^.form <= subrange then
                        if comptypes(intptr,gattr.typtr) then begin
                          if debug then begin
                            getbounds(gattr.typtr, lmin, lmax);
                            gen1t(51(*ldc*),lmin,basetype(gattr.typtr));
                            gen1t(51(*ldc*),lmax,basetype(gattr.typtr));
                            gen1(30(*csp*),40(*rib*))
                          end else gen1(30(*csp*),3(*rdi*))
                        end else
                          if comptypes(realptr,gattr.typtr) then
                            gen1(30(*csp*),4(*rdr*))
                          else
                            if comptypes(charptr,gattr.typtr) then begin
                              if debug then begin
                                getbounds(gattr.typtr, lmin, lmax);
                                gen2(51(*ldc*),6,lmin);
                                gen2(51(*ldc*),6,lmax);
                                gen1(30(*csp*),41(*rcb*))
                              end else gen1(30(*csp*),5(*rdc*))
                            end else error(153)
                      else error(116);
                  end else begin { binary file }
                    if not comptypes(gattr.typtr,lsp^.filtype) then error(129);
                    if gattr.typtr <> nil then begin
                      if debug and (gattr.typtr^.form <= subrange) then begin
                        getbounds(gattr.typtr, lmin, lmax);
                        gen2(51(*ldc*),1,lsp^.filtype^.size);
                        gen1t(51(*ldc*),lmin,intptr); { have to override this }
                        gen1t(51(*ldc*),lmax,intptr);
                        gen1(30(*csp*),49(*rbr*))
                      end else begin
                        gen2(51(*ldc*),1,lsp^.filtype^.size);
                        gen1(30(*csp*),35(*rbf*))
                      end
                    end;
                  end;
                  test := sy <> comma;
                  if not test then
                    begin insymbol; variable(fsys + [comma,rparent], true)
                    end
                until test;
                if sy = rparent then insymbol else error(4)
              end
            else begin
              if not inputhdf and not inputerr then
                begin error(175); inputerr := true end;
              if lkey = 5 then error(116);
              gen1(37(*lao*),inputptr^.vaddr);
            end;
            if lkey = 11 then gen1(30(*csp*),21(*rln*));
            { remove the file pointer from stack }
            gen1(71(*dmp*),ptrsize);
          end (*read*) ;

          procedure writeprocedure;
            var lsp,lsp1: stp; default, default1: boolean; llkey: 1..15;
                len:addrrange;
                txt: boolean; { is a text file }
                byt: boolean; { is a byte file }
                deffil: boolean; { default file was loaded }
                test: boolean;
          begin llkey := lkey; txt := true; deffil := true; byt := false;
            if sy = lparent then
            begin insymbol;
            expression(fsys + [comma,colon,rparent], false);
            lsp := gattr.typtr; test := false;
            if lsp <> nil then
              if lsp^.form = files then
                with gattr, lsp^ do
                  begin lsp1 := lsp;
                    txt := lsp = textptr;
                    if not txt then begin
                      if lkey = 12 then error(116);
                      byt := isbyte(lsp^.filtype)
                    end;
                    loadaddress; deffil := false;
                    if sy = rparent then
                      begin if llkey = 6 then error(116);
                        test := true
                      end
                    else
                      if sy <> comma then
                        begin error(116); skip(fsys+[comma,rparent]) end;
                    if sy = comma then
                      begin insymbol;
                        expression(fsys+[comma,colon,rparent], false)
                      end
                    else test := true
                  end
              else if not outputhdf and not outputerr then
                begin error(176); outputerr := true end;
            if not test then
            repeat
              lsp := gattr.typtr;
              if lsp <> nil then
                if lsp^.form <= subrange then load else loadaddress;
              lsp := basetype(lsp); { remove any subrange }
              if deffil then begin
                { file was not loaded, we load and swap so that it ends up
                  on the bottom.}
                gen1(37(*lao*),outputptr^.vaddr);
                if lsp <> nil then
                  if lsp^.form <= subrange then begin
                  if lsp^.size < stackelsize then
                    gen1(72(*swp*),stackelsize) { size of 2nd is minimum stack }
                  else
                    gen1(72(*swp*),lsp^.size) { size of 2nd is operand }
                end else
                  gen1(72(*swp*),ptrsize); { size of 2nd is pointer }
                deffil := false
              end;
              if txt then begin
                if sy = colon then
                  begin insymbol;
                    expression(fsys + [comma,colon,rparent], false);
                    if gattr.typtr <> nil then
                      if not intt(gattr.typtr) then error(116);
                    load; default := false
                  end
                else default := true;
                if sy = colon then
                  begin insymbol; expression(fsys + [comma,rparent], false);
                    if gattr.typtr <> nil then
                      if not intt(gattr.typtr) then error(116);
                    if lsp <> realptr then error(124);
                    load; default1 := false
                  end else default1 := true;
                if intt(lsp) then
                  begin if default then gen2(51(*ldc*),1,intdeff);
                    gen1(30(*csp*),6(*wri*))
                  end
                else
                  if lsp = realptr then
                    begin
                      if default1 then begin
                        if default then gen2(51(*ldc*),1,reldeff);
                        gen1(30(*csp*),8(*wrr*))
                      end else begin
                        if default then gen2(51(*ldc*),1,reldeff);
                        gen1(30(*csp*),28(*wrf*))
                      end
                    end
                  else
                    if lsp = charptr then
                      begin if default then gen2(51(*ldc*),1,chrdeff);
                        gen1(30(*csp*),9(*wrc*))
                      end
                    else
                      if lsp = boolptr then
                        begin if default then gen2(51(*ldc*),1,boldeff);
                          gen1(30(*csp*),27(*wrb*))
                        end
                      else
                        if lsp <> nil then
                          begin
                            if lsp^.form = scalar then error(236)
                            else
                              if stringt(lsp) then
                                begin len := lsp^.size div charmax;
                                  if default then
                                        gen2(51(*ldc*),1,len);
                                  gen2(51(*ldc*),1,len);
                                  gen1(30(*csp*),10(*wrs*))
                                end
                              else error(116)
                          end
              end else begin { binary file }
                if not comptypes(lsp1^.filtype,lsp) then error(129);
                if lsp <> nil then begin
                  checkbnds(lsp1^.filtype);
                  if intt(lsp) and not byt then gen1(30(*csp*),31(*wbi*))
                  else
                    if lsp = realptr then gen1(30(*csp*),32(*wbr*))
                    else
                      if lsp = charptr then gen1(30(*csp*),33(*wbc*))
                      else
                        if lsp = boolptr then gen1(30(*csp*),34(*wbb*))
                        else
                          if lsp^.form <= subrange then begin
                            if byt then gen1(30(*csp*),48(*wbx*))
                            else gen1(30(*csp*),31(*wbi*))
                          end else begin
                                  gen2(51(*ldc*),1,lsp1^.filtype^.size);
                                  gen1(30(*csp*),30(*wbf*))
                                end
                end
              end;
              test := sy <> comma;
              if not test then
                begin insymbol; expression(fsys + [comma,colon,rparent], false)
                end
            until test;
            if sy = rparent then insymbol else error(4)
            end else begin
              if not outputhdf and not outputerr then
                begin error(176); outputerr := true end;
              if lkey = 6 then error(116);
              gen1(37(*lao*),outputptr^.vaddr);
            end;
            if llkey = 12 then (*writeln*)
              gen1(30(*csp*),22(*wln*));
            { remove the file pointer from stack }
            gen1(71(*dmp*),ptrsize);
          end (*write*) ;

          procedure packprocedure;
            var lsp,lsp1: stp; lb, bs: integer; lattr: attr;
          begin variable(fsys + [comma,rparent], false); loadaddress;
            lsp := nil; lsp1 := nil; lb := 1; bs := 1;
            lattr := gattr;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then
                  begin lsp := inxtype; lsp1 := aeltype;
                    if (inxtype = charptr) or (inxtype = boolptr) then lb := 0
                    else if inxtype^.form = subrange then lb := inxtype^.min.ival;
                    bs := aeltype^.size
                  end
                else error(116);
            if sy = comma then insymbol else error(20);
            expression(fsys + [comma,rparent], false); load;
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> scalar then error(116)
              else
                if not comptypes(lsp,gattr.typtr) then error(116);
            if lattr.typtr <> nil then if lattr.typtr^.form = arrays then
              checkbnds(lattr.typtr^.inxtype);
            gen2(51(*ldc*),1,lb);
            gen0(21(*sbi*));
            gen2(51(*ldc*),1,bs);
            gen0(15(*mpi*));
            if sy = comma then insymbol else error(20);
            variable(fsys + [rparent], false); loadaddress;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then
                  begin
                    if not comptypes(aeltype,lsp1) then error(116)
                  end
                else error(116);
            if (gattr.typtr <> nil) and (lattr.typtr <> nil) then
              gen2(62(*pck*),gattr.typtr^.size,lattr.typtr^.size)
          end (*pack*) ;

          procedure unpackprocedure;
            var lsp,lsp1: stp; lattr,lattr1: attr; lb, bs: integer;
          begin variable(fsys + [comma,rparent], false); loadaddress;
            lattr := gattr;
            lsp := nil; lsp1 := nil; lb := 1; bs := 1;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then lsp1 := aeltype
                else error(116);
            if sy = comma then insymbol else error(20);
            variable(fsys + [comma,rparent], false); loadaddress;
            lattr1 := gattr;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = arrays then
                  begin
                    if not comptypes(aeltype,lsp1) then error(116);
                    if (inxtype = charptr) or (inxtype = boolptr) then lb := 0
                    else if inxtype^.form = subrange then lb := inxtype^.min.ival;
                    bs := aeltype^.size;
                    lsp := inxtype;
                  end
                else error(116);
            if sy = comma then insymbol else error(20);
            expression(fsys + [rparent], false); load;
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> scalar then error(116)
              else
                if not comptypes(lsp,gattr.typtr) then error(116);
            if lattr1.typtr <> nil then if lattr1.typtr^.form = arrays then
              checkbnds(lattr1.typtr^.inxtype);
            gen2(51(*ldc*),1,lb);
            gen0(21(*sbi*));
            gen2(51(*ldc*),1,bs);
            gen0(15(*mpi*));
            if (lattr.typtr <> nil) and (lattr1.typtr <> nil) then
              gen2(63(*upk*),lattr.typtr^.size,lattr1.typtr^.size)
          end (*unpack*) ;

          procedure newdisposeprocedure(disp: boolean);
            label 1;
            var lsp,lsp1,lsp2,lsp3: stp; varts: integer;
                lsize: addrrange; lval: valu; tagc: integer; tagrec: boolean;
                mn,mx: integer;
          begin
            if disp then begin
              expression(fsys + [comma, rparent], false);
              load
            end else begin
              variable(fsys + [comma,rparent], false);
              loadaddress
            end;
            lsp := nil; varts := 0; lsize := 0; tagc := 0; tagrec := false;
            if gattr.typtr <> nil then
              with gattr.typtr^ do
                if form = pointer then
                  begin
                    if eltype <> nil then
                      begin lsize := eltype^.size;
                        if eltype^.form = records then lsp := eltype^.recvar
                      end
                  end
                else error(116);
            tagrec := taggedrec(lsp);
            while sy = comma do
              begin insymbol;constant(fsys + [comma,rparent],lsp1,lval);
                varts := varts + 1; lsp2 := lsp1;
                (*check to insert here: is constant in tagfieldtype range*)
                if lsp = nil then error(158)
                else
                  if lsp^.form <> tagfld then error(162)
                  else
                    if lsp^.tagfieldp <> nil then
                      if stringt(lsp1) or (lsp1 = realptr) then error(159)
                      else
                        if comptypes(lsp^.tagfieldp^.idtype,lsp1) then
                          begin
                            getbounds(lsp^.tagfieldp^.idtype, mn, mx);
                            lsp3 := lsp; lsp1 := lsp^.fstvar;
                            while lsp1 <> nil do
                              with lsp1^ do
                                if varval.ival = lval.ival then
                                  begin lsize := size; lsp := subvar;
                                    if debug then begin
                                      if lsp3^.vart = nil then error(510);
                                      if lsp2=charptr then
                                        gen2(51(*ldc*),6,lsp3^.vart^[varval.ival-mn])
                                      else gen2(51(*ldc*),1,lsp3^.vart^[varval.ival-mn])
                                    end;
                                    tagc := tagc+1;
                                    goto 1
                                  end
                                else lsp1 := nxtvar;
                            lsize := lsp^.size; lsp := nil;
                          end
                        else error(116);
          1:  end (*while*) ;
            if debug and tagrec then gen2(51(*ldc*),1,tagc);
            gen2(51(*ldc*),1,lsize);
            if debug and tagrec then begin
              if lkey = 9 then gen1(30(*csp*),42(*nwl*))
              else gen1(30(*csp*),43(*dsl*));
              mesl(tagc*intsize)
            end else begin
              if lkey = 9 then gen1(30(*csp*),12(*new*))
              else gen1(30(*csp*),29(*dsp*))
            end;
          end (*newdisposeprocedure*) ;

          procedure absfunction;
          begin
            if gattr.typtr <> nil then
              if intt(gattr.typtr) then gen0(0(*abi*))
              else
                if gattr.typtr = realptr then gen0(1(*abr*))
                else begin error(125); gattr.typtr := intptr end
          end (*abs*) ;

          procedure sqrfunction;
          begin
            if gattr.typtr <> nil then
              if intt(gattr.typtr) then gen0(24(*sqi*))
              else
                if gattr.typtr = realptr then gen0(25(*sqr*))
                else begin error(125); gattr.typtr := intptr end
          end (*sqr*) ;

          procedure truncfunction;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr <> realptr then error(125);
            gen0(27(*trc*));
            gattr.typtr := intptr
          end (*trunc*) ;

          procedure roundfunction;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr <> realptr then error(125);
            gen0(61(*rnd*));
            gattr.typtr := intptr
          end (*round*) ;

          procedure oddfunction;
          begin
            if gattr.typtr <> nil then
              if not intt(gattr.typtr) then error(125);
            gen0(20(*odd*));
            gattr.typtr := boolptr
          end (*odd*) ;

          procedure ordfunction;
          begin
            if gattr.typtr <> nil then
              if gattr.typtr^.form >= power then error(125);
            gen0t(58(*ord*),gattr.typtr);
            gattr.typtr := intptr
          end (*ord*) ;

          procedure chrfunction;
          begin
            if gattr.typtr <> nil then
              if not intt(gattr.typtr) then error(125);
            gen0(59(*chr*));
            gattr.typtr := charptr
          end (*chr*) ;

          procedure predsuccfunction;
          begin
            if gattr.typtr <> nil then
              if (gattr.typtr^.form <> scalar) or (gattr.typtr = realptr) then
                error(125)
              else begin
                if lkey = 7 then gen1t(31(*dec*),1,gattr.typtr)
                else gen1t(34(*inc*),1,gattr.typtr);
                checkbnds(gattr.typtr)
              end
          end (*predsucc*) ;

          procedure eofeolnfunction;
          begin
            if sy = lparent then
              begin insymbol; variable(fsys + [rparent], false);
                if sy = rparent then insymbol else error(4);
                loadaddress
              end
            else begin
              if not inputhdf and not inputerr then
                begin error(175); inputerr := true end;
              gen1(37(*lao*),inputptr^.vaddr);
              gattr.typtr := textptr
            end;
            if gattr.typtr <> nil then
              if gattr.typtr^.form <> files then error(125)
              else if (lkey = 10) and (gattr.typtr <> textptr) then error(116);
            if lkey = 9 then begin
              if gattr.typtr = textptr then gen1(30(*csp*),44(*eof*))
              else gen1(30(*csp*),45(*efb*))
            end else gen1(30(*csp*),14(*eln*));
              gattr.typtr := boolptr
          end (*eof*) ;

          procedure callnonstandard(fcp: ctp);
            var nxt,lcp: ctp; lsp: stp; lkind: idkind; lb: boolean;
                locpar, llc: addrrange; varp: boolean; lsize: addrrange;
                frlab: integer;

          procedure compparam(pla, plb: ctp);
          begin
            while (pla <> nil) and (plb <> nil) do begin
              if pla^.klass <> plb^.klass then error(189)
              else if pla^.klass in [proc, func] then
                compparam(pla^.pflist, plb^.pflist)
              else if pla^.klass <> vars then error(500)
              else begin
                if (pla^.vkind <> plb^.vkind) or
                   (pla^.idtype <> plb^.idtype) then error(189)
              end;
              pla := pla^.next; plb := plb^.next
            end;
            if (pla <> nil) or (plb <> nil) then error(189)
          end;

          begin locpar := 0; genlabel(frlab);
            with fcp^ do
              begin nxt := pflist; lkind := pfkind;
                if pfkind = actual then begin { it's a system call }
                  if not externl then genmst(abs(level-pflev),frlab)
                end else genmst(abs(level-pflev),frlab) { its an indirect }
              end;
            if sy = lparent then
              begin llc := lc;
                repeat lb := false; (*decide whether proc/func must be passed*)
                  if nxt = nil then error(126)
                  else lb := nxt^.klass in [proc,func];
                  insymbol;
                  if lb then   (*pass function or procedure*)
                    begin
                      if sy <> ident then
                        begin error(2); skip(fsys + [comma,rparent]) end
                      else if nxt <> nil then
                        begin
                          if nxt^.klass = proc then searchid([proc],lcp)
                          else
                            begin searchid([func],lcp);
                              { compare result types }
                              if lcp^.idtype <> nxt^.idtype then error(128)
                            end;
                          { compare parameter lists }
                          if (nxt^.klass in [proc,func]) and
                             (lcp^.klass in [proc,func]) then
                            compparam(nxt^.pflist, lcp^.pflist);
                          if lcp^.pfkind = actual then genlpa(lcp^.pfname,abs(level-lcp^.pflev))
                          else gen2(74(*lip*),abs(level-lcp^.pflev),lcp^.pfaddr);
                          locpar := locpar+ptrsize*2;
                          insymbol;
                          if not (sy in fsys + [comma,rparent]) then
                            begin error(6); skip(fsys + [comma,rparent]) end
                        end
                    end (*if lb*)
                  else
                    begin varp := false;
                      if nxt <> nil then varp := nxt^.vkind = formal;
                      if varp then begin
                        variable(fsys + [comma,rparent], varp);
                        if gattr.ptrref and taggedrec(gattr.typtr) then gen0(83(*ctp*))
                      end else expression(fsys + [comma,rparent], varp);
                      if gattr.typtr <> nil then
                        begin
                          if nxt <> nil then
                            begin lsp := nxt^.idtype;
                              if lsp <> nil then
                                begin
                                  if (nxt^.vkind = actual) then begin
                                    if lsp^.form <= power then
                                      begin load;
                                        if debug then checkbnds(lsp);
                                        if comptypes(realptr,lsp)
                                           and intt(gattr.typtr) then
                                          begin gen0(10(*flt*));
                                            gattr.typtr := realptr
                                          end;
                                        locpar := locpar+lsp^.size;
                                        alignu(parmptr,locpar);
                                      end
                                    else
                                      begin
                                        loadaddress;
                                        locpar := locpar+ptrsize;
                                        alignu(parmptr,locpar)
                                      end;
                                      if not comptypes(lsp,gattr.typtr) then
                                        error(142)
                                  end else begin
                                    if gattr.kind = varbl then
                                      begin if gattr.packcom then error(197);
                                        if gattr.tagfield then error(198);
                                        loadaddress;
                                        locpar := locpar+ptrsize;
                                        alignu(parmptr,locpar);
                                      end
                                    else error(154);
                                    if lsp <> gattr.typtr then error(199)
                                  end

                                end
                            end
                        end
                    end;
                  if nxt <> nil then nxt := nxt^.next
                until sy <> comma;
                lc := llc;
                if sy = rparent then insymbol else error(4)
              end (*if lparent*);
            lsize := 0;
            if fcp^.klass = func then begin
              lsize := fcp^.idtype^.size;
              alignu(parmptr,lsize);
            end;
            if prcode then begin prtlabel(frlab); writeln(prr,'=',lsize:1) end;
            if lkind = actual then
              begin if nxt <> nil then error(126);
                with fcp^ do
                  begin
                    if externl then gen1(30(*csp*),pfname)
                    else begin
                      gencupent(46(*cup*),locpar,pfname);
                      if fcp^.klass = func then
                        if fcp^.idtype <> nil then begin
                        { add size of function result back to stack }
                        mesl(-lsize)
                      end
                    end
                  end
              end
            else begin { call procedure or function parameter }
              gen2(50(*lda*),abs(level-fcp^.pflev),fcp^.pfaddr);
              gen1(67(*cip*),locpar);
              mesl(locpar); { remove stack parameters }
              mesl(-lsize)
            end;
            gattr.typtr := fcp^.idtype
          end (*callnonstandard*) ;

        begin (*call*)
          if fcp^.pfdeckind = standard then
            begin lkey := fcp^.key;
              if fcp^.klass = proc then
                begin
                  if not(lkey in [5,6,11,12,17]) then
                    if sy = lparent then insymbol else error(9);
                  case lkey of
                    1,2,
                    3,4:   getputresetrewriteprocedure;
                    17:    pageprocedure;
                    5,11:  readprocedure;
                    6,12:  writeprocedure;
                    7:     packprocedure;
                    8:     unpackprocedure;
                    9,18:  newdisposeprocedure(lkey = 18);
                    10,13: error(500)
                  end;
                  if not(lkey in [5,6,11,12,17]) then
                    if sy = rparent then insymbol else error(4)
                end
              else
                begin
                  if (lkey <= 8) or (lkey = 16) then
                    begin
                      if sy = lparent then insymbol else error(9);
                      expression(fsys+[rparent], false); load
                    end;
                  case lkey of
                    1:    absfunction;
                    2:    sqrfunction;
                    3:    truncfunction;
                    16:   roundfunction;
                    4:    oddfunction;
                    5:    ordfunction;
                    6:    chrfunction;
                    7,8:  predsuccfunction;
                    9,10: eofeolnfunction
                  end;
                  if (lkey <= 8) or (lkey = 16) then
                    if sy = rparent then insymbol else error(4)
                end;
            end (*standard procedures and functions*)
          else callnonstandard(fcp)
        end (*call*) ;

        procedure expression;
          var lattr: attr; lop: operatort; typind: char; lsize: addrrange;

          procedure simpleexpression(fsys: setofsys; threaten: boolean);
            var lattr: attr; lop: operatort; fsy: symbol; fop: operatort;

            procedure term(fsys: setofsys; threaten: boolean);
              var lattr: attr; lop: operatort;

              procedure factor(fsys: setofsys; threaten: boolean);
                var lcp: ctp; lvp: csp; varpart: boolean;
                    cstpart: setty; lsp: stp;
                    tattr, rattr: attr;
                    test: boolean;
              begin
                if not (sy in facbegsys) then
                  begin error(58); skip(fsys + facbegsys);
                    gattr.typtr := nil
                  end;
                while sy in facbegsys do
                  begin
                    case sy of
              (*id*)    ident:
                        begin searchid([konst,vars,field,func],lcp);
                          insymbol;
                          if lcp^.klass = func then
                            begin call(fsys,lcp);
                              with gattr do
                                begin kind := expr;
                                  if typtr <> nil then
                                    if typtr^.form=subrange then
                                      typtr := typtr^.rangetype
                                end
                            end
                          else
                            if lcp^.klass = konst then
                              with gattr, lcp^ do
                                begin typtr := idtype; kind := cst;
                                  cval := values
                                end
                            else
                              begin selector(fsys,lcp);
                                if threaten and (lcp^.klass = vars) then with lcp^ do begin
                                  if vlev < level then threat := true;
                                  if forcnt > 0 then error(195);
                                end;
                                if gattr.ptrref and taggedrec(gattr.typtr) then
                                  gen0(83(*ctp*));
                                if gattr.typtr<>nil then(*elim.subr.types to*)
                                  with gattr,typtr^ do(*simplify later tests*)
                              end
                        end;
              (*cst*)   intconst:
                        begin
                          with gattr do
                            begin typtr := intptr; kind := cst;
                              cval := val
                            end;
                          insymbol
                        end;
                        realconst:
                        begin
                          with gattr do
                            begin typtr := realptr; kind := cst;
                              cval := val
                            end;
                          insymbol
                        end;
                        stringconst:
                        begin
                          with gattr do
                            begin
                              if lgth = 1 then typtr := charptr
                              else
                                begin new(lsp,arrays); pshstc(lsp);
                                  with lsp^ do
                                    begin aeltype := charptr; form:=arrays;
                                      packing := true;
                                      inxtype := nil; size := lgth*charsize
                                    end;
                                  typtr := lsp
                                end;
                              kind := cst; cval := val
                            end;
                          insymbol
                        end;
              (* ( *)   lparent:
                        begin insymbol; expression(fsys + [rparent], false);
                          if sy = rparent then insymbol else error(4)
                        end;
              (*not*)   notsy:
                        begin insymbol; factor(fsys, false);
                          load; gen0(19(*not*));
                          if gattr.typtr <> nil then
                            if gattr.typtr <> boolptr then
                              begin error(135); gattr.typtr := nil end;
                        end;
              (*[*)     lbrack:
                        begin insymbol; cstpart := [ ]; varpart := false;
                          new(lsp,power); pshstc(lsp);
                          with lsp^ do
                            begin elset:=nil;size:=setsize;form:=power;
                                  packing := false; matchpack := false end;
                          if sy = rbrack then
                            begin
                              with gattr do
                                begin typtr := lsp; kind := cst end;
                              insymbol
                            end
                          else
                            begin
                              repeat
                                expression(fsys + [comma,range,rbrack], false);
                                rattr.typtr := nil;
                                if sy = range then begin insymbol;
                                  { if the left side is not constant, load it
                                    and coerce it to integer now }
                                  if gattr.kind <> cst then begin
                                    load;
                                    if not comptypes(gattr.typtr,intptr)
                                    then gen0t(58(*ord*),gattr.typtr);
                                  end;
                                  tattr := gattr;
                                  expression(fsys + [comma,rbrack], false);
                                  rattr := gattr; gattr := tattr;
                                end;
                                if gattr.typtr <> nil then
                                  if (gattr.typtr^.form <> scalar) and
                                     (gattr.typtr^.form <> subrange) then
                                    begin error(136); gattr.typtr := nil end
                                  else if comptypes(gattr.typtr,realptr) then
                                    begin error(109); gattr.typtr := nil end
                                  else
                                    if comptypes(lsp^.elset,gattr.typtr) then
                                      begin
                                        if rattr.typtr <> nil then begin { x..y form }
                                          if (rattr.typtr^.form <> scalar) and
                                             (rattr.typtr^.form <> subrange) then
                                            begin error(136); rattr.typtr := nil end
                                          else if comptypes(rattr.typtr,realptr) then
                                            begin error(109); rattr.typtr := nil end
                                          else
                                            if comptypes(lsp^.elset,rattr.typtr) then
                                              begin
                                                if (gattr.kind = cst) and
                                                   (rattr.kind = cst) then
                                                  if (rattr.cval.ival < setlow) or
                                                     (rattr.cval.ival > sethigh) or
                                                     (gattr.cval.ival < setlow) or
                                                     (gattr.cval.ival > sethigh) then
                                                    error(304)
                                                  else
                                                    cstpart := cstpart+
                                                      [gattr.cval.ival..rattr.cval.ival]
                                                else
                                                  begin
                                                    if gattr.kind = cst then begin
                                                      load;
                                                      if not comptypes(gattr.typtr,intptr)
                                                        then gen0t(58(*ord*),gattr.typtr)
                                                    end;
                                                    tattr := gattr; gattr := rattr;
                                                    load;
                                                    gattr := tattr;
                                                    if not comptypes(rattr.typtr,intptr)
                                                    then gen0t(58(*ord*),rattr.typtr);
                                                    gen0(64(*rgs*));
                                                    if varpart then gen0(28(*uni*))
                                                    else varpart := true
                                                  end
                                              end
                                            else error(137)
                                        end else begin
                                          if gattr.kind = cst then
                                            if (gattr.cval.ival < setlow) or
                                              (gattr.cval.ival > sethigh) then
                                              error(304)
                                            else
                                              cstpart := cstpart+[gattr.cval.ival]
                                          else
                                            begin load;
                                              if not comptypes(gattr.typtr,intptr)
                                              then gen0t(58(*ord*),gattr.typtr);
                                              gen0(23(*sgs*));
                                              if varpart then gen0(28(*uni*))
                                              else varpart := true
                                            end
                                        end;
                                        lsp^.elset := gattr.typtr;
                                        gattr.typtr := lsp
                                      end
                                    else begin error(137); gattr.typtr := nil end;
                                test := sy <> comma;
                                if not test then insymbol
                              until test;
                              if sy = rbrack then insymbol else error(12)
                            end;
                          if varpart then
                            begin
                              if cstpart <> [ ] then
                                begin new(lvp,pset); pshcst(lvp);
                                  lvp^.pval := cstpart;
                                  lvp^.cclass := pset;
                                  if cstptrix = cstoccmax then error(254)
                                  else
                                    begin cstptrix := cstptrix + 1;
                                      cstptr[cstptrix] := lvp;
                                      gen2(51(*ldc*),5,cstptrix);
                                      gen0(28(*uni*)); gattr.kind := expr
                                    end
                                end
                            end
                          else
                            begin new(lvp,pset); pshcst(lvp);
                              lvp^.cclass := pset;
                              lvp^.pval := cstpart;
                              gattr.cval.intval := false;
                              gattr.cval.valp := lvp
                            end
                        end;
              (*nil*)   nilsy: with gattr do
                                 begin typtr := nilptr; kind := cst;
                                       cval.intval := true; cval.ival := nilval;
                                       insymbol
                                 end
                    end (*case*) ;
                    if not (sy in fsys) then
                      begin error(6); skip(fsys + facbegsys) end
                  end (*while*)
              end (*factor*) ;

            begin (*term*)
              factor(fsys + [mulop], threaten);
              while sy = mulop do
                begin load; lattr := gattr; lop := op;
                  insymbol; factor(fsys + [mulop], threaten); load;
                  if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                    case lop of
            (***)     mul:  if intt(lattr.typtr) and intt(gattr.typtr)
                            then gen0(15(*mpi*))
                            else
                              begin
                                if intt(lattr.typtr) then
                                  begin gen0(9(*flo*));
                                    lattr.typtr := realptr
                                  end
                                else
                                  if intt(gattr.typtr) then
                                    begin gen0(10(*flt*));
                                      gattr.typtr := realptr
                                    end;
                                if (lattr.typtr = realptr)
                                  and(gattr.typtr=realptr)then gen0(16(*mpr*))
                                else
                                  if(lattr.typtr^.form=power)
                                    and comptypes(lattr.typtr,gattr.typtr)then
                                    gen0(12(*int*))
                                  else begin error(134); gattr.typtr:=nil end
                              end;
            (* / *)   rdiv: begin
                              if intt(gattr.typtr) then
                                begin gen0(10(*flt*));
                                  gattr.typtr := realptr
                                end;
                              if intt(lattr.typtr) then
                                begin gen0(9(*flo*));
                                  lattr.typtr := realptr
                                end;
                              if (lattr.typtr = realptr)
                                and (gattr.typtr=realptr)then gen0(7(*dvr*))
                              else begin error(134); gattr.typtr := nil end
                            end;
            (*div*)   idiv: if intt(lattr.typtr)
                              and intt(gattr.typtr) then gen0(6(*dvi*))
                            else begin error(134); gattr.typtr := nil end;
            (*mod*)   imod: if intt(lattr.typtr)
                              and intt(gattr.typtr) then gen0(14(*mod*))
                            else begin error(134); gattr.typtr := nil end;
            (*and*)   andop:if (lattr.typtr = boolptr)
                              and (gattr.typtr = boolptr) then gen0(4(*and*))
                            else begin error(134); gattr.typtr := nil end
                    end (*case*)
                  else gattr.typtr := nil
                end (*while*)
            end (*term*) ;

          begin (*simpleexpression*)
            fsy := sy; fop := op;
            if (sy = addop) and (op in [plus,minus]) then insymbol;
            term(fsys + [addop], threaten);
            if (fsy = addop) and (fop in [plus, minus]) then begin
                if fop = minus then begin
                  load;
                  if intt(gattr.typtr) then gen0(17(*ngi*))
                  else
                    if gattr.typtr = realptr then gen0(18(*ngr*))
                    else begin error(134); gattr.typtr := nil end
                end else begin
                  if not intt(gattr.typtr) and
                     (gattr.typtr <> realptr) then
                    begin error(134); gattr.typtr := nil end
                end
            end;
            while sy = addop do
              begin load; lattr := gattr; lop := op;
                insymbol; term(fsys + [addop], threaten); load;
                if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                  case lop of
          (*+*)       plus:
                      if intt(lattr.typtr) and intt(gattr.typtr) then
                        gen0(2(*adi*))
                      else
                        begin
                          if intt(lattr.typtr) then
                            begin gen0(9(*flo*));
                              lattr.typtr := realptr
                            end
                          else
                            if intt(gattr.typtr) then
                              begin gen0(10(*flt*));
                                gattr.typtr := realptr
                              end;
                          if (lattr.typtr = realptr)and(gattr.typtr = realptr)
                            then gen0(3(*adr*))
                          else if(lattr.typtr^.form=power)
                                 and comptypes(lattr.typtr,gattr.typtr) then
                                 gen0(28(*uni*))
                               else begin error(134); gattr.typtr:=nil end
                        end;
          (*-*)       minus:
                      if intt(lattr.typtr) and intt(gattr.typtr) then
                        gen0(21(*sbi*))
                      else
                        begin
                          if intt(lattr.typtr) then
                            begin gen0(9(*flo*));
                              lattr.typtr := realptr
                            end
                          else
                            if intt(gattr.typtr) then
                              begin gen0(10(*flt*));
                                gattr.typtr := realptr
                              end;
                          if (lattr.typtr = realptr)and(gattr.typtr = realptr)
                            then gen0(22(*sbr*))
                          else
                            if (lattr.typtr^.form = power)
                              and comptypes(lattr.typtr,gattr.typtr) then
                              gen0(5(*dif*))
                            else begin error(134); gattr.typtr := nil end
                        end;
          (*or*)      orop:
                      if(lattr.typtr=boolptr)and(gattr.typtr=boolptr)then
                        gen0(13(*ior*))
                      else begin error(134); gattr.typtr := nil end
                  end (*case*)
                else gattr.typtr := nil
              end (*while*)
          end (*simpleexpression*) ;

        begin (*expression*)
          simpleexpression(fsys + [relop], threaten);
          if sy = relop then
            begin
              if gattr.typtr <> nil then
                if gattr.typtr^.form <= power then load
                else loadaddress;
              lattr := gattr; lop := op;
              if lop = inop then
                if not comptypes(gattr.typtr,intptr) then
                  gen0t(58(*ord*),gattr.typtr);
              insymbol; simpleexpression(fsys, threaten);
              if gattr.typtr <> nil then
                if gattr.typtr^.form <= power then load
                else loadaddress;
              if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                if lop = inop then
                  if gattr.typtr^.form = power then
                    if comptypes(lattr.typtr,gattr.typtr^.elset) then
                      gen0(11(*inn*))
                    else begin error(129); gattr.typtr := nil end
                  else begin error(130); gattr.typtr := nil end
                else
                  begin
                    if lattr.typtr <> gattr.typtr then
                      if intt(lattr.typtr) then
                        begin gen0(9(*flo*));
                          lattr.typtr := realptr
                        end
                      else
                        if intt(gattr.typtr) then
                          begin gen0(10(*flt*));
                            gattr.typtr := realptr
                          end;
                    if comptypes(lattr.typtr,gattr.typtr) then
                      begin lsize := lattr.typtr^.size;
                        case lattr.typtr^.form of
                          scalar:
                            if lattr.typtr = realptr then typind := 'r'
                            else
                              if lattr.typtr = boolptr then typind := 'b'
                              else
                                if lattr.typtr = charptr then typind := 'c'
                                else typind := 'i';
                          pointer:
                            begin
                              if lop in [ltop,leop,gtop,geop] then error(131);
                              typind := 'a'
                            end;
                          power:
                            begin if lop in [ltop,gtop] then error(132);
                              typind := 's'
                            end;
                          arrays:
                            begin
                              if not stringt(lattr.typtr)
                                then error(134);
                              typind := 'm'
                            end;
                          records:
                            begin
                              error(134);
                              typind := 'm'
                            end;
                          files:
                            begin error(133); typind := 'f' end
                        end;
                        if typind <> 'f' then case lop of
                          ltop: gen2(53(*les*),ord(typind),lsize);
                          leop: gen2(52(*leq*),ord(typind),lsize);
                          gtop: gen2(49(*grt*),ord(typind),lsize);
                          geop: gen2(48(*geq*),ord(typind),lsize);
                          neop: gen2(55(*neq*),ord(typind),lsize);
                          eqop: gen2(47(*equ*),ord(typind),lsize)
                        end
                      end
                    else error(129)
                  end;
              gattr.typtr := boolptr; gattr.kind := expr
            end (*sy = relop*)
        end (*expression*) ;

        procedure assignment(fcp: ctp);
          var lattr, lattr2: attr; tagasc: boolean;
        begin tagasc := false; selector(fsys + [becomes],fcp);
          if sy = becomes then
            begin
              if gattr.ptrref and taggedrec(gattr.typtr) then gen0(83(*ctp*));
              { if function result, set assigned }
              if fcp^.klass = func then fcp^.asgn := true
              else if fcp^.klass = vars then with fcp^ do begin
                 if vlev < level then threat := true;
                 if forcnt > 0 then error(195)
              end;
              if gattr.kind = varbl then tagasc := gattr.tagfield and debug;
              lattr2 := gattr; { save access before load }
              if gattr.typtr <> nil then
                if (gattr.access<>drct) or (gattr.typtr^.form>power) or
                   tagasc then { if tag checking, force address load }
                  loadaddress;
              lattr := gattr;
              insymbol; expression(fsys, false);
              if gattr.typtr <> nil then
                if gattr.typtr^.form <= power then load
                else loadaddress;
              if (lattr.typtr <> nil) and (gattr.typtr <> nil) then
                begin
                  if comptypes(realptr,lattr.typtr) and intt(gattr.typtr) then
                    begin gen0(10(*flt*));
                      gattr.typtr := realptr
                    end;
                  if comptypes(lattr.typtr,gattr.typtr) then begin
                    if filecomponent(gattr.typtr) then error(191);
                    with lattr2 do
                      if kind = varbl then begin
                        if access = indrct then
                          if debug and tagfield and ptrrec then
                            { check tag assignment to pointer record }
                            genctaivtcvb(81(*cta*),idplmt,taglvl,vartl, lattr2.typtr);
                        if chkvbk and tagfield then
                        genctaivtcvb(65(*cvb*),vartagoff,varssize,vartl,
                                     lattr2.typtr);
                        if debug and tagfield then
                          genctaivtcvb(82(*ivt*),vartagoff,varssize,vartl,
                                       lattr2.typtr)
                      end;
                    { if tag checking, bypass normal store }
                    if tagasc then
                       gen0t(26(*sto*),lattr.typtr)
                    else case lattr.typtr^.form of
                      scalar,
                      subrange,
                      power: begin
                                  if debug then checkbnds(lattr.typtr);
                                  store(lattr)
                                end;
                      pointer: begin
                                 if debug then begin
                                   if taggedrec(lattr.typtr^.eltype) then
                                     gen2t(80(*ckl*),0,maxaddr,nilptr)
                                   else gen2t(45(*chk*),0,maxaddr,nilptr);
                                 end;
                                 store(lattr)
                               end;
                      arrays,
                      records: gen1(40(*mov*),lattr.typtr^.size);
                      files: error(146)
                    end;
                  end else error(129)
                end
            end (*sy = becomes*)
          else error(51)
        end (*assignment*) ;

        procedure gotostatement;
          var llp: lbp; ttop,ttop1: disprange;
              wp: wtp;
        begin
          if sy = intconst then
            begin
              ttop := top;
              while display[ttop].occur <> blck do ttop := ttop - 1;
              ttop1 := ttop;
              repeat
                searchlabel(llp, ttop); { find label }
                if llp <> nil then with llp^ do begin
                  refer := true;
                  if defined then
                    if slevel > stalvl then { defining point level greater than
                                              present statement level }
                      error(185) { goto references deeper nested statement }
                    else if (slevel > 1) and not bact then
                      error(187); { Goto references label in different nested
                                    statement }
                  { establish the minimum statement level a goto appeared at }
                  if minlvl > stalvl then minlvl := stalvl;
                  { remove any with statement levels to target }
                  wp := wthstk;
                  while wp <> nil do begin
                    if wp^.sl <> slevel then gen0(85(*wbe*));
                    wp := wp^.next
                  end;
                  if ttop = ttop1 then
                    genujpxjp(57(*ujp*),labname)
                  else begin { interprocedural goto }
                    genipj(66(*ipj*),abs(level-vlevel),labname);
                    ipcref := true
                  end
                end;
                ttop := ttop - 1
              until (llp <> nil) or (ttop = 0);
              if llp = nil then begin
                error(167); { undeclared label }
                newlabel(llp); { create dummy label in current context }
                llp^.refer := true
              end;
              insymbol
            end
          else error(15)
        end (*gotostatement*) ;

        procedure compoundstatement;
        var test: boolean;
        begin
          addlvl;
          repeat
            repeat statement(fsys + [semicolon,endsy])
            until not (sy in statbegsys);
            test := sy <> semicolon;
            if not test then insymbol
          until test;
          if sy = endsy then insymbol else error(13);
          sublvl
        end (*compoundstatemenet*) ;

        procedure ifstatement;
          var lcix1,lcix2: integer;
        begin expression(fsys + [thensy], false);
          genlabel(lcix1); genfjp(lcix1);
          if sy = thensy then insymbol else error(52);
          addlvl;
          statement(fsys + [elsesy]);
          sublvl;
          if sy = elsesy then
            begin genlabel(lcix2); genujpxjp(57(*ujp*),lcix2);
              putlabel(lcix1);
              markline;
              insymbol;
              addlvl;
              statement(fsys);
              sublvl;
              putlabel(lcix2);
              markline
            end
          else begin putlabel(lcix1); markline end
        end (*ifstatement*) ;

        procedure casestatement;
          label 1;
          var lsp,lsp1: stp; fstptr,lpt1,lpt2,lpt3: cip; lval: valu;
              laddr, lcix, lcix1, lmin, lmax: integer;
              test: boolean;
        begin expression(fsys + [ofsy,comma,colon], false);
          load; genlabel(lcix);
          lsp := gattr.typtr;
          if lsp <> nil then
            if (lsp^.form <> scalar) or (lsp = realptr) then
              begin error(144); lsp := nil end
            else if not comptypes(lsp,intptr) then gen0t(58(*ord*),lsp);
          genujpxjp(57(*ujp*),lcix);
          mesl(-intsize); { remove selector from stack }
          if sy = ofsy then insymbol else error(8);
          fstptr := nil; genlabel(laddr);
          repeat
            lpt3 := nil; genlabel(lcix1);
            if not(sy in [semicolon,endsy]) then
              begin
                repeat constant(fsys + [comma,colon],lsp1,lval);
                  if lsp <> nil then
                    if comptypes(lsp,lsp1) then
                      begin lpt1 := fstptr; lpt2 := nil;
                        while lpt1 <> nil do
                          with lpt1^ do
                            begin
                              if cslab <= lval.ival then
                                begin if cslab = lval.ival then error(156);
                                  goto 1
                                end;
                              lpt2 := lpt1; lpt1 := next
                            end;
            1:    getcas(lpt3);
                        with lpt3^ do
                          begin next := lpt1; cslab := lval.ival;
                            csstart := lcix1
                          end;
                        if lpt2 = nil then fstptr := lpt3
                        else lpt2^.next := lpt3
                      end
                    else error(147);
                  test := sy <> comma;
                  if not test then insymbol
                until test;
                if sy = colon then insymbol else error(5);
                putlabel(lcix1);
                markline;
                repeat
                  addlvl;
                  statement(fsys + [semicolon]);
                  sublvl
                until not (sy in statbegsys);
                if lpt3 <> nil then
                  genujpxjp(57(*ujp*),laddr);
              end;
            test := sy <> semicolon;
            if not test then insymbol
          until test;
          putlabel(lcix);
          markline;
          mesl(+intsize); { put selector back on stack }
          if fstptr <> nil then
            begin lmax := fstptr^.cslab;
              (*reverse pointers*)
              lpt1 := fstptr; fstptr := nil;
              repeat lpt2 := lpt1^.next; lpt1^.next := fstptr;
                fstptr := lpt1; lpt1 := lpt2
              until lpt1 = nil;
              lmin := fstptr^.cslab;
              if lmax - lmin < cixmax then
                begin
                  gen2t(45(*chk*),lmin,lmax,intptr);
                  gen2(51(*ldc*),1,lmin); gen0(21(*sbi*)); genlabel(lcix);
                  genujpxjp(44(*xjp*),lcix); putlabel(lcix);
                  repeat
                    with fstptr^ do
                      begin
                        while cslab > lmin do
                           begin gen0(60(*ujc error*));
                             lmin := lmin+1
                           end;
                        genujpxjp(57(*ujp*),csstart);
                        lpt1 := fstptr; fstptr := next; lmin := lmin + 1
                      end;
                      putcas(lpt1)
                  until fstptr = nil;
                  putlabel(laddr);
                  markline
                end
              else begin
                error(157);
                repeat
                  with fstptr^ do
                    begin
                      lpt1 := fstptr; fstptr := next;
                      putcas(lpt1);
                    end
                until fstptr = nil
              end
            end;
            if sy = endsy then insymbol else error(13)
        end (*casestatement*) ;

        procedure repeatstatement;
          var laddr: integer;
        begin genlabel(laddr); putlabel(laddr); markline;
          addlvl;
          repeat
            statement(fsys + [semicolon,untilsy]);
            if sy in statbegsys then error(14)
          until not(sy in statbegsys);
          while sy = semicolon do
            begin insymbol;
              repeat
                statement(fsys + [semicolon,untilsy]);
                if sy in statbegsys then error(14);
              until not (sy in statbegsys);
            end;
          if sy = untilsy then
            begin insymbol; expression(fsys, false); genfjp(laddr)
            end
          else error(53);
          sublvl
        end (*repeatstatement*) ;

        procedure whilestatement;
          var laddr, lcix: integer;
        begin genlabel(laddr); putlabel(laddr); markline;
          expression(fsys + [dosy], false); genlabel(lcix); genfjp(lcix);
          if sy = dosy then insymbol else error(54);
          addlvl;
          statement(fsys);
          sublvl;
          genujpxjp(57(*ujp*),laddr); putlabel(lcix); markline
        end (*whilestatement*) ;

        procedure forstatement;
          var lattr: attr;  lsy: symbol;
              lcix, laddr: integer;
                    llc, lcs: addrrange;
              typind: char; (* added for typing [sam] *)
              typ: stp;
        begin lcp := nil; llc := lc;
          with lattr do
            begin typtr := nil; kind := varbl;
              access := drct; vlevel := level; dplmt := 0; packing := false
            end;
          typind := 'i'; (* default to integer [sam] *)
          if sy = ident then
            begin searchid([vars],lcp);
              with lcp^, lattr do
                begin typtr := idtype; kind := varbl; packing := false;
                  if threat or (forcnt > 0) then error(195); forcnt := forcnt+1;
                  if vkind = actual then
                    begin access := drct; vlevel := vlev;
                      if vlev <> level then error(183);
                      dplmt := vaddr
                    end
                  else begin error(155); typtr := nil end
                end;
              (* determine type of control variable [sam] *)
              if lattr.typtr = boolptr then typind := 'b'
              else if lattr.typtr = charptr then typind := 'c';
              if lattr.typtr <> nil then
                if (lattr.typtr^.form > subrange)
                   or comptypes(realptr,lattr.typtr) then
                  begin error(143); lattr.typtr := nil end;
              insymbol
            end
          else
            begin error(2); skip(fsys + [becomes,tosy,downtosy,dosy]) end;
          if sy = becomes then
            begin insymbol; expression(fsys + [tosy,downtosy,dosy], false);
              typ := basetype(gattr.typtr); { get base type }
              if typ <> nil then
                  if typ^.form <> scalar then error(144)
                  else
                    if comptypes(lattr.typtr,gattr.typtr) then begin
                      load; alignd(intptr,lc);
                      { store start to temp }
                      gen2t(56(*str*),0,lc-intsize,intptr);
                    end else error(145)
            end
          else
            begin error(51); skip(fsys + [tosy,downtosy,dosy]) end;
          if sy in [tosy,downtosy] then
            begin lsy := sy; insymbol; expression(fsys + [dosy], false);
              typ := basetype(gattr.typtr); { get base type }
              if typ <> nil then
              if typ^.form <> scalar then error(144)
                else
                  if comptypes(lattr.typtr,gattr.typtr) then
                    begin
                      load; alignd(intptr,lc);
                      if not comptypes(lattr.typtr,intptr) then
                        gen0t(58(*ord*),gattr.typtr);
                      gen2t(56(*str*),0,lc-intsize*2,intptr);
                      { set initial value of index }
                      gen2t(54(*lod*),0,lc-intsize,intptr);
                      if debug and (lattr.typtr <> nil) then
                        checkbnds(lattr.typtr);
                      store(lattr);
                      genlabel(laddr); putlabel(laddr); markline;
                      gattr := lattr; load;
                      if not comptypes(gattr.typtr,intptr) then
                        gen0t(58(*ord*),gattr.typtr);
                      gen2t(54(*lod*),0,lc-intsize*2,intptr);
                      lcs := lc;
                      lc := lc - intsize*2;
                      if lc < lcmin then lcmin := lc;
                      if lsy = tosy then gen2(52(*leq*),ord(typind),1)
                      else gen2(48(*geq*),ord(typind),1);
                    end
                  else error(145)
            end
          else begin error(55); skip(fsys + [dosy]) end;
          genlabel(lcix); genujpxjp(33(*fjp*),lcix);
          if sy = dosy then insymbol else error(54);
          addlvl;
          statement(fsys);
          sublvl;
          gattr := lattr; load;
          if not comptypes(gattr.typtr,intptr) then
            gen0t(58(*ord*),gattr.typtr);
          gen2t(54(*lod*),0,lcs-intsize*2,intptr);
          gen2(47(*equ*),ord(typind),1);
          genujpxjp(73(*tjp*),lcix);
          gattr := lattr; load;
          if lsy=tosy then gen1t(34(*inc*),1,gattr.typtr)
          else  gen1t(31(*dec*),1,gattr.typtr);
          if debug and (lattr.typtr <> nil) then
            checkbnds(lattr.typtr);
          store(lattr);
          genujpxjp(57(*ujp*),laddr); putlabel(lcix); markline;
          gattr := lattr; loadaddress; gen0(79(*inv*));
          lc := llc;
          if lcp <> nil then lcp^.forcnt := lcp^.forcnt-1
        end (*forstatement*) ;

        procedure withstatement;
          var lcp: ctp; lcnt1: disprange; llc: addrrange;
              test: boolean; lattr: attr;
              wbscnt: integer;
        begin lcnt1 := 0; llc := lc; wbscnt := 0;
          repeat
            if sy = ident then
              begin searchid([vars,field],lcp); insymbol end
            else begin error(2); lcp := uvarptr end;
            selector(fsys + [comma,dosy],lcp);
            lattr := gattr;
            if gattr.typtr <> nil then
              if gattr.typtr^.form = records then
                if top < displimit then
                  begin top := top + 1; scopen := scopen+1; lcnt1 := lcnt1 + 1;
                    with display[top] do
                      begin fname := gattr.typtr^.fstfld;
                        flabel := nil;
                        flabel := nil;
                        fconst := nil;
                        fstruct := nil;
                        packing := gattr.packing;
                        packcom := gattr.packcom;
                        ptrrec := gattr.ptrref
                      end;
                    if gattr.access = drct then
                      with display[top] do
                        begin occur := crec; clev := gattr.vlevel;
                          cdspl := gattr.dplmt
                        end
                    else
                      begin loadaddress;
                        if debug and gattr.ptrref then
                          begin gen0(84(*wbs*)); wbscnt := wbscnt+1; pshwth(stalvl) end;
                        alignd(nilptr,lc);
                        lc := lc-ptrsize;
                        gen2t(56(*str*),0,lc,nilptr);
                        with display[top] do
                          begin occur := vrec; vdspl := lc end;
                        if lc < lcmin then lcmin := lc
                      end
                  end
                else error(250)
              else error(140);
            test := sy <> comma;
            if not test then insymbol
          until test;
          if sy = dosy then insymbol else error(54);
          addlvl;
          statement(fsys);
          sublvl;
          while wbscnt > 0 do begin gen0(85(*wbe*)); wbscnt := wbscnt-1; popwth end;
          { purge display levels }
          while lcnt1 > 0 do begin
             { don't recycle the record context }
             display[top].fname := nil;
             putdsp(top); { purge }
             top := top-1; lcnt1 := lcnt1-1; { count off }
          end;
          lc := llc;
        end (*withstatement*) ;

      begin (*statement*)
        if sy = intconst then (*label*)
          begin
            searchlabel(llp, level); { search label }
            if llp <> nil then with llp^ do begin { found }
              if defined then error(165); { multidefined label }
              bact := true; { set in active block now }
              slevel := stalvl; { establish statement level }
              defined := true; { set defined }
              if ipcref and (stalvl > 1) then
                error(184) { intraprocedure goto does not reference outter block }
              else if minlvl < stalvl then
                { Label referenced by goto at lesser statement level or
                  differently nested statement }
                error(186);
              putlabel(labname); { output label to intermediate }
              markline
            end else begin { not found }
              error(167); { undeclared label }
              newlabel(llp) { create a dummy level }
            end;
            insymbol;
            if sy = colon then insymbol else error(5)
          end;
        if not (sy in fsys + [ident]) then perror(6, fsys+[ident], fsys);
        if sy in statbegsys + [ident] then
          begin
            case sy of
              ident:    begin searchid([vars,field,func,proc],lcp); insymbol;
                          if lcp^.klass = proc then call(fsys,lcp)
                          else assignment(lcp)
                        end;
              beginsy:  begin insymbol; compoundstatement end;
              gotosy:   begin insymbol; gotostatement end;
              ifsy:     begin insymbol; ifstatement end;
              casesy:   begin insymbol; casestatement end;
              whilesy:  begin insymbol; whilestatement end;
              repeatsy: begin insymbol; repeatstatement end;
              forsy:    begin insymbol; forstatement end;
              withsy:   begin insymbol; withstatement end
            end;
            if not (sy in [semicolon,endsy,elsesy,untilsy]) then
              begin error(6); skip(fsys) end
          end
      end (*statement*) ;

    begin (*body*)
      if fprocp <> nil then entname := fprocp^.pfname
      else genlabel(entname);
      cstptrix := 0; topnew := 0; topmin := 0;
      putlabel(entname); markline; genlabel(segsize); genlabel(stackbot);
      genlabel(gblsize);
      gencupent(32(*ents*),1,segsize); gencupent(32(*ente*),2,stackbot);
      if fprocp <> nil then (*copy multiple values into local cells*)
        begin llc1 := lcaftermarkstack;
          lcp := fprocp^.pflist;
          while lcp <> nil do
            with lcp^ do
              begin
                if klass = vars then
                  if idtype <> nil then
                    if idtype^.form > power then
                      begin
                        llc1 := llc1 - ptrsize;
                        alignd(parmptr,llc1);
                        if vkind = actual then
                          begin
                            gen2(50(*lda*),0,vaddr);
                            gen2t(54(*lod*),0,llc1,nilptr);
                            gen1(40(*mov*),idtype^.size);
                          end
                      end
                    else
                      begin
                        llc1 := llc1 - idtype^.size;
                        alignd(parmptr,llc1);
                      end;
                    if chkvbk and (vkind = formal) then begin
                      { establish var block }
                      gen2t(54(*lod*),0,llc1,nilptr);
                      gen1(69(*vbs*),idtype^.size)
                    end;
                lcp := lcp^.next;
              end;
        end;
      lcmin := lc;
      addlvl;
      repeat
        repeat statement(fsys + [semicolon,endsy])
        until not (sy in statbegsys);
        test := sy <> semicolon;
        if not test then insymbol
      until test;
      sublvl;
      if sy = endsy then insymbol else error(13);
      llp := display[top].flabel; (*test for undefined and unreferenced labels*)
      while llp <> nil do
        with llp^ do
          begin
            if not defined or not refer then
              begin if not defined then error(168);
                writeln(output); write(output,'label ',labval:11);
                if not refer then write(' unreferenced');
                writeln;
                write(output,' ':chcnt+16)
              end;
            llp := nextlab
          end;
      printed := false; chkrefs(display[top].fname, printed);
      chkudf(display[top].fname);
      if toterr = 0 then
        if (topnew <> 0) and prcode then 
          error(500); { stack should have wound to zero }
      if fprocp <> nil then
        begin
          { output var block ends for each var parameter }
          lcp := fprocp^.pflist;
          while lcp <> nil do
            with lcp^ do begin
              if klass = vars then
                if chkvbk and (vkind = formal) then gen0(70(*vbe*));
              lcp := next
            end;
          if fprocp^.idtype = nil then gen1(42(*ret*),ord('p'))
          else gen0t(42(*ret*),basetype(fprocp^.idtype));
          alignd(parmptr,lcmin);
          if prcode then
          begin writeln(prr,'l',segsize:4,'=',lcmin:1);
             writeln(prr,'l',stackbot:4,'=',topmin:1);
             writeln(prr,'g ',gc:1)
            end
        end
      else
        begin gen1(42(*ret*),ord('p'));
          alignd(parmptr,lcmin);
          if prcode then
          begin writeln(prr,'l',segsize:4,'=',lcmin:1);
              writeln(prr,'l',stackbot:4,'=',topmin:1);
              writeln(prr,'g ',gc:1);
              writeln(prr,'q')
            end;
          ic := 0;
          (*generate call of main program; note that this call must be loaded
            at absolute address zero*)
          genlabel(frlab); prtlabel(frlab); writeln(prr,'=',0:1);
          genmst(0,frlab); gencupent(46(*cup*),0,entname); gen0(29(*stp*));
          if prcode then begin
            writeln(prr, 'f ', toterr:1);
            writeln(prr,'q');
          end;
          saveid := id;
          while fextfilep <> nil do
            begin
              with fextfilep^ do
                if not (strequri('input    ', filename) or
                        strequri('output   ', filename) or
                        strequri('prd      ', filename) or
                        strequri('prr      ', filename))
                then begin id := filename;
                       { output general error for undefined external file }
                       writeln(output);
                       writeln(output,'**** Error: external file unknown ''',
                                      fextfilep^.filename:8, '''');
                       toterr := toterr+1;
                       { hold the error in case not found, since this error
                         occurs far from the original symbol }
                       searchidne([vars],llcp,false);
                       if llcp = nil then begin
                         { a header file was never defined in a var statement }
                         writeln(output);
                         writeln(output,'**** Error: Undeclared external file ''',
                                        fextfilep^.filename:8, '''');
                         toterr := toterr+1;
                         llcp := uvarptr
                       end;
                       if llcp^.idtype<>nil then
                         if llcp^.idtype^.form<>files then
                           begin writeln(output);
                             writeln(output,'**** Error: Undeclared external file ''',
                                            fextfilep^.filename:8, '''');
                             toterr := toterr+1
                           end
                     end;
                fp := fextfilep; fextfilep := fextfilep^.nextfile; putfil(fp);
            end;
          id := saveid;
          if prtables then
            begin writeln(output); printtables(true)
            end
        end;
    end (*body*) ;

  begin (*block*)
    stalvl := 0; { clear statement nesting level }
    dp := true;
    repeat
      if sy = labelsy then
        begin insymbol; labeldeclaration end;
      if sy = constsy then
        begin insymbol; constdeclaration end;
      if sy = typesy then
        begin insymbol; typedeclaration end;
      if sy = varsy then
        begin insymbol; vardeclaration end;
      while sy in [procsy,funcsy] do
        begin lsy := sy; insymbol; procdeclaration(lsy) end;
      if sy <> beginsy then perror(18, fsys+[ident], fsys)
    until (sy in statbegsys) or eof(prd);
    dp := false;
    if sy = beginsy then insymbol else error(17);
    repeat body(fsys + [casesy]);
      if sy <> fsy then
        begin error(6); skip(fsys) end
    until ((sy = fsy) or (sy in blockbegsys)) or eof(prd)
  end (*block*) ;

  function searchext: boolean;
  var fp: extfilep; f: boolean;
  begin f := false; fp := fextfilep;
    while fp <> nil do
      begin if id = fp^.filename then f := true; fp := fp^.nextfile end;
    searchext := f
  end;

  procedure programme(fsys:setofsys);
    var extfp:extfilep;
  begin
    chkudtf := chkudtc; { finalize undefined tag checking flag }
    if sy = progsy then
      begin insymbol; if sy <> ident then error(2) else insymbol;
        if not (sy in [lparent,semicolon]) then error(14);
        if sy = lparent  then
          begin
            repeat insymbol;
              if sy = ident then
                begin getfil(extfp); if searchext then error(240);
                  with extfp^ do
                    begin filename := id; nextfile := fextfilep end;
                  fextfilep := extfp;
                  { check 'input' or 'output' appears in header for defaults }
                  if strequri('input    ', id) then inputhdf := true
                  else if strequri('output   ', id) then outputhdf := true;
                  insymbol;
                  if not ( sy in [comma,rparent] ) then
                    perror(20, fsys+[ident,comma,rparent,semicolon], [])
                end
              else perror(2, fsys+[ident,comma,rparent,semicolon], [])
            until sy <> comma;
            if sy <> rparent then perror(4, fsys+[rparent,semicolon], []);
            insymbol;
            if sy <> semicolon then perror(14, fsys+[rparent,semicolon], [])
          end;
        if sy = semicolon then insymbol
      end else error(3);
    repeat block(fsys,period,nil);
      if sy <> period then error(21)
    until (sy = period) or eof(prd);
    if list then writeln(output);
    if errinx <> 0 then
      begin list := false; endofline end;
  end (*programme*) ;


  procedure stdnames;
  begin
    { 'mark' and 'release' were removed and replaced with placeholders }
    na[ 1] := 'false    '; na[ 2] := 'true     '; na[ 3] := 'input    ';
    na[ 4] := 'output   '; na[ 5] := 'get      '; na[ 6] := 'put      ';
    na[ 7] := 'reset    '; na[ 8] := 'rewrite  '; na[ 9] := 'read     ';
    na[10] := 'write    '; na[11] := 'pack     '; na[12] := 'unpack   ';
    na[13] := 'new      '; na[14] := '---      '; na[15] := 'readln   ';
    na[16] := 'writeln  ';
    na[17] := 'abs      '; na[18] := 'sqr      '; na[19] := 'trunc    ';
    na[20] := 'odd      '; na[21] := 'ord      '; na[22] := 'chr      ';
    na[23] := 'pred     '; na[24] := 'succ     '; na[25] := 'eof      ';
    na[26] := 'eoln     ';
    na[27] := 'sin      '; na[28] := 'cos      '; na[29] := 'exp      ';
    na[30] := 'sqrt     '; na[31] := 'ln       '; na[32] := 'arctan   ';
    na[33] := 'prd      '; na[34] := 'prr      '; na[35] := '---      ';
    na[36] := 'maxint   '; na[37] := 'round    '; na[38] := 'page     ';
    na[39] := 'dispose  ';
  end (*stdnames*) ;

  procedure enterstdtypes;

  begin                                                 (*type underlying:*)
                                                        (******************)

    new(intptr,scalar,standard); pshstc(intptr);               (*integer*)
    with intptr^ do
      begin form := scalar; scalkind := standard; size := intsize;
            packing := false end;
    new(realptr,scalar,standard); pshstc(realptr);             (*real*)
    with realptr^ do
      begin form := scalar; scalkind := standard; size := realsize;
            packing := false end;
    new(charptr,scalar,standard); pshstc(charptr);             (*char*)
    with charptr^ do
      begin form := scalar; scalkind := standard; size := charsize;
            packing := false end;
    new(boolptr,scalar,declared); pshstc(boolptr);             (*boolean*)
    with boolptr^ do
      begin form := scalar; scalkind := declared; size := boolsize;
            packing := false end;
    new(nilptr,pointer); pshstc(nilptr);                       (*nil*)
    with nilptr^ do
      begin form := pointer; eltype := nil; size := ptrsize;
            packing := false end;
    (*for alignment of parameters*)
    new(parmptr,scalar,standard); pshstc(parmptr);
    with parmptr^ do
      begin form := scalar; scalkind := standard; size := parmsize;
            packing := false end ;
    new(textptr,files); pshstc(textptr);                       (*text*)
    with textptr^ do
      begin form := files; filtype := charptr; size := filesize+charsize;
            packing := false end
  end (*enterstdtypes*) ;

  procedure entstdnames;
    var cp,cp1: ctp; i: integer;

  begin                                                       (*name:*)
                                                              (*******)

    new(cp,types); ininam(cp);                                (*integer*)
    with cp^ do
      begin klass := types; strassvr(name, 'integer  '); idtype := intptr end;
    enterid(cp);
    new(cp,types); ininam(cp);                                (*real*)
    with cp^ do
      begin klass := types; strassvr(name, 'real     '); idtype := realptr end;
    enterid(cp);
    new(cp,types); ininam(cp);                                (*char*)
    with cp^ do
      begin klass := types; strassvr(name, 'char     '); idtype := charptr end;
    enterid(cp);
    new(cp,types); ininam(cp);                                (*boolean*)
    with cp^ do
      begin klass := types; strassvr(name, 'boolean  '); idtype := boolptr end;
    usclrptr := cp; { save to satisfy broken tags }
    enterid(cp);
    new(cp,types); ininam(cp);                                (*text*)
    with cp^ do
      begin klass := types; strassvr(name, 'text     '); idtype := textptr end;
    enterid(cp);
    cp1 := nil;
    for i := 1 to 2 do
      begin new(cp,konst); ininam(cp);                        (*false,true*)
        with cp^ do
          begin klass := konst; strassvr(name, na[i]); idtype := boolptr;
            next := cp1; values.intval := true; values.ival := i - 1
          end;
        enterid(cp); cp1 := cp
      end;
    boolptr^.fconst := cp;
    for i := 3 to 4 do
      begin new(cp,vars); ininam(cp);                         (*input,output*)
        with cp^ do
          begin klass := vars; strassvr(name, na[i]); idtype := textptr;
            vkind := actual; next := nil; vlev := 1;
            vaddr := gc; gc := gc+filesize+charsize; { files are global now }
            threat := false; forcnt := 0
          end;
        enterid(cp);
        if i = 3 then inputptr := cp else outputptr := cp
      end;
    for i:=33 to 34 do
      begin new(cp,vars); ininam(cp);                         (*prd,prr files*)
         with cp^ do
           begin klass := vars; strassvr(name, na[i]); idtype := textptr;
              vkind := actual; next := nil; vlev := 1;
              vaddr := gc; gc := gc+filesize+charsize; { alloc global file }
              threat := false; forcnt := 0
           end;
         enterid(cp)
      end;
    for i := 5 to 16 do if i <> 14 then { no longer doing release }
      begin new(cp,proc,standard); ininam(cp);                 (*get,put,reset*)
        with cp^ do                                            (*rewrite,read*)
          begin klass := proc; pfdeckind := standard;          (*write,pack*)
            strassvr(name, na[i]); idtype := nil;              (*unpack,new*)
            pflist := nil; next := nil; key := i - 4           (*readln,writeln*)
          end;
        enterid(cp)
      end;
    for i := 17 to 26 do
      begin new(cp,func,standard); ininam(cp);                 (*abs,sqr,trunc*)
        with cp^ do                                            (*odd,ord,chr*)
          begin klass := func; pfdeckind := standard;          (*pred,succ,eof*)
            strassvr(name, na[i]); idtype := nil;
            pflist := nil; next := nil; key := i - 16;
          end;
        enterid(cp)
      end;
    for i := 27 to 32 do
      begin
        new(cp,vars); ininam(cp);                                (*parameter of predeclared functions*)
        with cp^ do
          begin klass := vars; strassvr(name, '         '); idtype := realptr;
            vkind := actual; next := nil; vlev := 1; vaddr := 0;
            threat := false; forcnt := 0
          end;
        new(cp1,func,declared,actual); ininam(cp1);            (*sin,cos,exp*)
        with cp1^ do                                           (*sqrt,ln,arctan*)
          begin klass := func; pfdeckind := declared; pfkind := actual;
            strassvr(name, na[i]); idtype := realptr; pflist := cp;
            forwdecl := false; externl := true; pflev := 0; pfname := i - 12
          end;
        enterid(cp1)
      end;
    new(cp,konst); ininam(cp);                                 (*maxint*)
    with cp^ do
      begin klass := konst; strassvr(name, na[36]); idtype := intptr;
        next := nil; values.intval := true; values.ival := maxint;
      end; enterid(cp);
    new(cp,func,standard); ininam(cp);                         (*round*)
    with cp^ do
      begin klass := func; pfdeckind := standard;
        strassvr(name, na[37]); idtype := nil;
        pflist := nil; next := nil; key := 16;
      end; enterid(cp);
    new(cp,proc,standard); ininam(cp);                         (*page*)
    with cp^ do
      begin klass := proc; pfdeckind := standard;
        strassvr(name, na[38]); idtype := nil;
        pflist := nil; next := nil; key := 17;
      end; enterid(cp);
    new(cp,proc,standard); ininam(cp);                         (*dispose*)
    with cp^ do
      begin klass := proc; pfdeckind := standard;
        strassvr(name, na[39]); idtype := nil;
        pflist := nil; next := nil; key := 18;
      end; enterid(cp)
  end (*entstdnames*) ;

  procedure enterundecl;
  begin
    new(uvarptr,vars); ininam(uvarptr);
    with uvarptr^ do
      begin klass := vars; strassvr(name, '         '); idtype := nil;
        vkind := actual; next := nil; vlev := 0; vaddr := 0;
        threat := false; forcnt := 0
      end;
    new(uprcptr,proc,declared,actual); ininam(uprcptr);
    with uprcptr^ do
      begin klass := proc; pfdeckind := declared; pfkind := actual;
        strassvr(name, '         '); idtype := nil; forwdecl := false;
        next := nil; externl := false; pflev := 0; genlabel(pfname);
        pflist := nil
      end;
    new(ufctptr,func,declared,actual); ininam(ufctptr);
    with ufctptr^ do
      begin klass := func; pfdeckind := declared; pfkind := actual;
        strassvr(name, '         '); idtype := nil; next := nil;
        forwdecl := false; externl := false; pflev := 0; genlabel(pfname);
        pflist := nil;
      end
  end (*enterundecl*) ;

  { tear down storage allocations from enterundecl }
  procedure exitundecl;
  begin
    putnam(uvarptr);
    putnam(uprcptr);
    putnam(ufctptr);
  end (*exitundecl*) ;

  procedure initscalars;
  var i: integer; c: char;
  begin fwptr := nil; for c := 'a' to 'z' do option[c] := false;
    prtables := false; option['t'] := false; list := true; option['l'] := true;
    prcode := true; option['c'] := true; debug := true; option['d'] := true;
    chkvar := true; option['v'] := true; chkref := true; option['r'] := true;
    chkudtc := true; option['u'] := true;
    dodmplex := false; option['x'] := false; doprtryc := false; option['z'] := false;
    doprtlab := false; option['b'] := false; dodmpdsp := false; option['y'] := false;
    chkvbk := false; option['i'] := false;
    dp := true; errinx := 0;
    intlabel := 0; kk := maxids; fextfilep := nil; wthstk := nil;
    lc := lcaftermarkstack; gc := 0;
    (* note in the above reservation of buffer store for 2 text files *)
    ic := 3; eol := true; linecount := 0;
    ch := ' '; chcnt := 0;
    mxint10 := maxint div 10;
    inputhdf := false; { set 'input' not in header files }
    inputerr := false; { set no missing 'input' error flagged }
    outputhdf := false; { set 'output' not in header files }
    outputerr := false; { set no missing 'output' error flagged }
    for i := 1 to 500 do errtbl[i] := false; { initialize error tracking }
    toterr := 0; { clear error count }
    { clear the recycling tracking counters }
    strcnt := 0; { strings }
    cspcnt := 0; { constants }
    stpcnt := 0; { structures }
    ctpcnt := 0; { identifiers }
    lbpcnt := 0; { label counts }
    filcnt := 0; { file tracking counts }
    cipcnt := 0; { case entry tracking counts }
    ttpcnt := 0; { tag tracking entry counts }
    wtpcnt := 0; { with tracking entry counts }

    { clear id counts }
    ctpsnm := 0;
    stpsnm := 0
  end (*initscalars*) ;

  procedure initsets;
  begin
    constbegsys := [addop,intconst,realconst,stringconst,ident];
    simptypebegsys := [lparent] + constbegsys;
    typebegsys:=[arrow,packedsy,arraysy,recordsy,setsy,filesy]+simptypebegsys;
    typedels := [arraysy,recordsy,setsy,filesy];
    blockbegsys := [labelsy,constsy,typesy,varsy,procsy,funcsy,beginsy];
    selectsys := [arrow,period,lbrack];
    facbegsys := [intconst,realconst,stringconst,ident,lparent,lbrack,notsy,nilsy];
    statbegsys := [beginsy,gotosy,ifsy,whilesy,repeatsy,forsy,withsy,casesy];
  end (*initsets*) ;

  procedure inittables;
    procedure reswords;
    begin
      rw[ 1] := 'if       '; rw[ 2] := 'do       '; rw[ 3] := 'of       ';
      rw[ 4] := 'to       '; rw[ 5] := 'in       '; rw[ 6] := 'or       ';
      rw[ 7] := 'end      '; rw[ 8] := 'for      '; rw[ 9] := 'var      ';
      rw[10] := 'div      '; rw[11] := 'mod      '; rw[12] := 'set      ';
      rw[13] := 'and      '; rw[14] := 'not      '; rw[15] := 'nil      ';
      rw[16] := 'then     '; rw[17] := 'else     '; rw[18] := 'with     ';
      rw[19] := 'goto     '; rw[20] := 'case     '; rw[21] := 'type     ';
      rw[22] := 'file     '; rw[23] := 'begin    '; rw[24] := 'until    ';
      rw[25] := 'while    '; rw[26] := 'array    '; rw[27] := 'const    ';
      rw[28] := 'label    '; rw[29] := 'repeat   '; rw[30] := 'record   ';
      rw[31] := 'downto   '; rw[32] := 'packed   '; rw[33] := 'program  ';
      rw[34] := 'function '; rw[35] := 'procedure';
      frw[1] :=  1; frw[2] :=  1; frw[3] :=  7; frw[4] := 16; frw[5] := 23;
      frw[6] := 29; frw[7] := 33; frw[8] := 34; frw[9] := 35; frw[10] := 36;
    end (*reswords*) ;

    procedure symbols;
    var i: integer;
    begin
      rsy[ 1] := ifsy;      rsy[ 2] := dosy;      rsy[ 3] := ofsy;
      rsy[ 4] := tosy;      rsy[ 5] := relop;     rsy[ 6] := addop;
      rsy[ 7] := endsy;     rsy[ 8] := forsy;     rsy[ 9] := varsy;
      rsy[10] := mulop;     rsy[11] := mulop;     rsy[12] := setsy;
      rsy[13] := mulop;     rsy[14] := notsy;     rsy[15] := nilsy;
      rsy[16] := thensy;    rsy[17] := elsesy;    rsy[18] := withsy;
      rsy[19] := gotosy;    rsy[20] := casesy;    rsy[21] := typesy;
      rsy[22] := filesy;    rsy[23] := beginsy;   rsy[24] := untilsy;
      rsy[25] := whilesy;   rsy[26] := arraysy;   rsy[27] := constsy;
      rsy[28] := labelsy;   rsy[29] := repeatsy;  rsy[30] := recordsy;
      rsy[31] := downtosy;  rsy[32] := packedsy;  rsy[33] := progsy;
      rsy[34] := funcsy;    rsy[35] := procsy;

      for i := ordminchar to ordmaxchar do ssy[chr(i)] := othersy;
      ssy['+'] := addop ;   ssy['-'] := addop;    ssy['*'] := mulop;
      ssy['/'] := mulop ;   ssy['('] := lparent;  ssy[')'] := rparent;
      ssy['$'] := othersy ; ssy['='] := relop;    ssy[' '] := othersy;
      ssy[','] := comma ;   ssy['.'] := period;   ssy['''']:= othersy;
      ssy['['] := lbrack ;  ssy[']'] := rbrack;   ssy[':'] := colon;
      ssy['^'] := arrow ;   ssy['<'] := relop;    ssy['>'] := relop;
      ssy[';'] := semicolon; ssy['@'] := arrow;   ssy['}'] := othersy;
    end (*symbols*) ;

    procedure rators;
      var i: integer;
    begin
      for i := 1 to maxres (*nr of res words*) do rop[i] := noop;
      rop[5] := inop; rop[10] := idiv; rop[11] := imod;
      rop[6] := orop; rop[13] := andop;
      for i := ordminchar to ordmaxchar do sop[chr(i)] := noop;
      sop['+'] := plus; sop['-'] := minus; sop['*'] := mul; sop['/'] := rdiv;
      sop['='] := eqop; sop['<'] := ltop;  sop['>'] := gtop;
    end (*rators*) ;

    procedure procmnemonics;
    begin
      { There are two mnemonics that have no counterpart in the
        assembler/interpreter: wro, pak. I didn't find a generator for them, and
        suspect they are abandoned. }
      sna[ 1] :=' get'; sna[ 2] :=' put'; sna[ 3] :=' rdi'; sna[ 4] :=' rdr';
      sna[ 5] :=' rdc'; sna[ 6] :=' wri'; sna[ 7] :=' wro'; sna[ 8] :=' wrr';
      sna[ 9] :=' wrc'; sna[10] :=' wrs'; sna[11] :=' pak'; sna[12] :=' new';
      sna[13] :=' rst'; sna[14] :=' eln'; sna[15] :=' sin'; sna[16] :=' cos';
      sna[17] :=' exp'; sna[18] :=' sqt'; sna[19] :=' log'; sna[20] :=' atn';
      sna[21] :=' rln'; sna[22] :=' wln'; sna[23] :=' sav';
      { new procedure/function memonics for p5 }
      sna[24] :=' pag'; sna[25] :=' rsf'; sna[26] :=' rwf'; sna[27] :=' wrb';
      sna[28] :=' wrf'; sna[29] :=' dsp'; sna[30] :=' wbf'; sna[31] :=' wbi';
      sna[32] :=' wbr'; sna[33] :=' wbc'; sna[34] :=' wbb'; sna[35] :=' rbf';
      sna[36] :=' rsb'; sna[37] :=' rwb'; sna[38] :=' gbf'; sna[39] :=' pbf';
      sna[40] :=' rib'; sna[41] :=' rcb'; sna[42] :=' nwl'; sna[43] :=' dsl';
      sna[44] :=' eof'; sna[45] :=' efb'; sna[46] :=' fbv'; sna[47] :=' fvb';
      sna[48] :=' wbx'; sna[49] :=' rbr';


    end (*procmnemonics*) ;

    procedure instrmnemonics;
    begin
      { ??? memnemonics are placeholders }
      mn[ 0] :=' abi'; mn[ 1] :=' abr'; mn[ 2] :=' adi'; mn[ 3] :=' adr';
      mn[ 4] :=' and'; mn[ 5] :=' dif'; mn[ 6] :=' dvi'; mn[ 7] :=' dvr';
      mn[ 8] :=' ???'; mn[ 9] :=' flo'; mn[10] :=' flt'; mn[11] :=' inn';
      mn[12] :=' int'; mn[13] :=' ior'; mn[14] :=' mod'; mn[15] :=' mpi';
      mn[16] :=' mpr'; mn[17] :=' ngi'; mn[18] :=' ngr'; mn[19] :=' not';
      mn[20] :=' odd'; mn[21] :=' sbi'; mn[22] :=' sbr'; mn[23] :=' sgs';
      mn[24] :=' sqi'; mn[25] :=' sqr'; mn[26] :=' sto'; mn[27] :=' trc';
      mn[28] :=' uni'; mn[29] :=' stp'; mn[30] :=' csp'; mn[31] :=' dec';
      mn[32] :=' ent'; mn[33] :=' fjp'; mn[34] :=' inc'; mn[35] :=' ind';
      mn[36] :=' ixa'; mn[37] :=' lao'; mn[38] :=' lca'; mn[39] :=' ldo';
      mn[40] :=' mov'; mn[41] :=' mst'; mn[42] :=' ret'; mn[43] :=' sro';
      mn[44] :=' xjp'; mn[45] :=' chk'; mn[46] :=' cup'; mn[47] :=' equ';
      mn[48] :=' geq'; mn[49] :=' grt'; mn[50] :=' lda'; mn[51] :=' ldc';
      mn[52] :=' leq'; mn[53] :=' les'; mn[54] :=' lod'; mn[55] :=' neq';
      mn[56] :=' str'; mn[57] :=' ujp'; mn[58] :=' ord'; mn[59] :=' chr';
      mn[60] :=' ujc';
      { new instruction memonics for p5 }
      mn[61] :=' rnd'; mn[62] :=' pck'; mn[63] :=' upk'; mn[64] :=' rgs';
      mn[65] :=' cvb'; mn[66] :=' ipj'; mn[67] :=' cip'; mn[68] :=' lpa';
      mn[69] :=' vbs'; mn[70] :=' vbe'; mn[71] :=' dmp'; mn[72] :=' swp';
      mn[73] :=' tjp'; mn[74] :=' lip'; mn[75] :=' ckv'; mn[76] :=' dup';
      mn[77] :=' cke'; mn[78] :=' cks'; mn[79] :=' inv'; mn[80] :=' ckl';
      mn[81] :=' cta'; mn[82] :=' ivt'; mn[83] :=' ctp'; mn[84] :=' wbs';
      mn[85] :=' wbe';

    end (*instrmnemonics*) ;

    procedure chartypes;
    var i : integer;
    begin
      for i := ordminchar to ordmaxchar do chartp[chr(i)] := illegal;
      chartp['a'] := letter  ;
      chartp['b'] := letter  ; chartp['c'] := letter  ;
      chartp['d'] := letter  ; chartp['e'] := letter  ;
      chartp['f'] := letter  ; chartp['g'] := letter  ;
      chartp['h'] := letter  ; chartp['i'] := letter  ;
      chartp['j'] := letter  ; chartp['k'] := letter  ;
      chartp['l'] := letter  ; chartp['m'] := letter  ;
      chartp['n'] := letter  ; chartp['o'] := letter  ;
      chartp['p'] := letter  ; chartp['q'] := letter  ;
      chartp['r'] := letter  ; chartp['s'] := letter  ;
      chartp['t'] := letter  ; chartp['u'] := letter  ;
      chartp['v'] := letter  ; chartp['w'] := letter  ;
      chartp['x'] := letter  ; chartp['y'] := letter  ;
      chartp['z'] := letter  ;
      chartp['A'] := letter  ;
      chartp['B'] := letter  ; chartp['C'] := letter  ;
      chartp['D'] := letter  ; chartp['E'] := letter  ;
      chartp['F'] := letter  ; chartp['G'] := letter  ;
      chartp['H'] := letter  ; chartp['I'] := letter  ;
      chartp['J'] := letter  ; chartp['K'] := letter  ;
      chartp['L'] := letter  ; chartp['M'] := letter  ;
      chartp['N'] := letter  ; chartp['O'] := letter  ;
      chartp['P'] := letter  ; chartp['Q'] := letter  ;
      chartp['R'] := letter  ; chartp['S'] := letter  ;
      chartp['T'] := letter  ; chartp['U'] := letter  ;
      chartp['V'] := letter  ; chartp['W'] := letter  ;
      chartp['X'] := letter  ; chartp['Y'] := letter  ;
      chartp['Z'] := letter  ;
      chartp['0'] := number  ;
      chartp['1'] := number  ; chartp['2'] := number  ;
      chartp['3'] := number  ; chartp['4'] := number  ;
      chartp['5'] := number  ; chartp['6'] := number  ;
      chartp['7'] := number  ; chartp['8'] := number  ;
      chartp['9'] := number  ; chartp['+'] := special ;
      chartp['-'] := special ; chartp['*'] := special ;
      chartp['/'] := special ; chartp['('] := chlparen;
      chartp[')'] := special ; chartp['$'] := special ;
      chartp['='] := special ; chartp[' '] := chspace ;
      chartp[','] := special ; chartp['.'] := chperiod;
      chartp['''']:= chstrquo; chartp['['] := special ;
      chartp[']'] := special ; chartp[':'] := chcolon ;
      chartp['^'] := special ; chartp[';'] := special ;
      chartp['<'] := chlt    ; chartp['>'] := chgt    ;
      chartp['{'] := chlcmt  ; chartp['}'] := special ;
      chartp['@'] := special ;

      ordint['0'] := 0; ordint['1'] := 1; ordint['2'] := 2;
      ordint['3'] := 3; ordint['4'] := 4; ordint['5'] := 5;
      ordint['6'] := 6; ordint['7'] := 7; ordint['8'] := 8;
      ordint['9'] := 9;
    end;

    procedure initdx;
    begin
      { [sam] if your sizes are not even multiples of
        stackelsize, you are going to need to compensate this.
        entries marked with * go to secondary table }
      cdx[ 0] :=  0;                   cdx[ 1] :=  0;
      cdx[ 2] := +intsize;             cdx[ 3] := +realsize;
      cdx[ 4] := +intsize;             cdx[ 5] := +setsize;
      cdx[ 6] := +intsize;             cdx[ 7] := +realsize;
      cdx[ 8] :=  0;                   cdx[ 9] := +intsize-realsize;
      cdx[10] :=  -realsize+intsize;   cdx[11] := +setsize;
      cdx[12] := +setsize;             cdx[13] := +intsize;
      cdx[14] := +intsize;             cdx[15] := +intsize;
      cdx[16] := +realsize;            cdx[17] :=  0;
      cdx[18] :=  0;                   cdx[19] :=  0;
      cdx[20] :=  0;                   cdx[21] := +intsize;
      cdx[22] := +realsize;            cdx[23] := +intsize-setsize;
      cdx[24] :=  0;                   cdx[25] :=  0;
      cdx[26] := 1{*};                 cdx[27] := +realsize-intsize;
      cdx[28] := +setsize;             cdx[29] :=  0;
      cdx[30] :=  0;                   cdx[31] :=  2{*};
      cdx[32] :=  0;                   cdx[33] := +intsize;
      cdx[34] :=  2{*};                cdx[35] :=  3{*};
      cdx[36] := +intsize;             cdx[37] := -adrsize;
      cdx[38] := -adrsize;             cdx[39] :=  4{*};
      cdx[40] := +adrsize*2;           cdx[41] :=  0;
      cdx[42] :=  2{*};                cdx[43] :=  5{*};
      cdx[44] := +intsize;             cdx[45] :=  2{*};
      cdx[46] :=  0;                   cdx[47] :=  6{*};
      cdx[48] :=  6{*};                cdx[49] :=  6{*};
      cdx[50] := -adrsize;             cdx[51] :=  4{*};
      cdx[52] :=  6{*};                cdx[53] :=  6{*};
      cdx[54] :=  4{*};                cdx[55] :=  6{*};
      cdx[56] :=  5{*};                cdx[57] :=  0;
      cdx[58] :=  2{*};                cdx[59] :=  0;
      cdx[60] :=  0;                   cdx[61] :=  +realsize-intsize;
      cdx[62] := +adrsize*3;           cdx[63] := +adrsize*3;
      cdx[64] := +intsize*2-setsize;   cdx[65] :=  0;
      cdx[66] :=  0;                   cdx[67] := +ptrsize;
      cdx[68] := -adrsize*2;           cdx[69] := +intsize;
      cdx[70] :=  0;                   cdx[71] := +ptrsize;
      cdx[72] :=  0;                   cdx[73] := +intsize;
      cdx[74] := -adrsize*2;           cdx[75] :=  2{*};
      cdx[76] :=  4{*};                cdx[77] :=  +intsize*2;
      cdx[78] := -intsize;             cdx[79] :=  +adrsize;
      cdx[80] :=  2{*};                cdx[81] :=  0;
      cdx[82] :=  0;                   cdx[83] :=  0;
      cdx[84] :=  0;                   cdx[85] :=  0;

      { secondary table order is i, r, b, c, a, s, m }
      cdxs[1][1] := +(adrsize+intsize);  { stoi }
      cdxs[1][2] := +(adrsize+realsize); { stor }
      cdxs[1][3] := +(adrsize+intsize);  { stob }
      cdxs[1][4] := +(adrsize+intsize);  { stoc }
      cdxs[1][5] := +(adrsize+adrsize);  { stoa }
      cdxs[1][6] := +(adrsize+setsize);  { stos }
      cdxs[1][7] := 0;

      cdxs[2][1] := 0; { deci/inci/ordi/chki/reti }
      cdxs[2][2] := 0; { chkr/retr }
      cdxs[2][3] := 0; { decb/incb/ordb/chkb/retb }
      cdxs[2][4] := 0; { decc/incc/ordc/chkc/retc }
      cdxs[2][5] := 0; { chka/reta }
      cdxs[2][6] := 0; { chks }
      cdxs[2][7] := 0;

      cdxs[3][1] := +adrsize-intsize;  { indi }
      cdxs[3][2] := +adrsize-realsize; { indr }
      cdxs[3][3] := +adrsize-intsize;  { indb }
      cdxs[3][4] := +adrsize-intsize;  { indc }
      cdxs[3][5] := +adrsize-adrsize;  { inda }
      cdxs[3][6] := +adrsize-setsize;  { inds }
      cdxs[3][7] := 0;

      cdxs[4][1] := -intsize;  { ldoi/ldc/lodi/dupi }
      cdxs[4][2] := -realsize; { ldor/ldc/lodr/dupr }
      cdxs[4][3] := -intsize;  { ldob/ldc/lodb/dupb }
      cdxs[4][4] := -intsize;  { ldoc/ldc/lodc/dupc }
      cdxs[4][5] := -adrsize;  { ldoa/ldc/loda/dupa }
      cdxs[4][6] := -setsize;  { ldos/ldc/lods/dups }
      cdxs[4][7] := 0;

      cdxs[5][1] := +intsize;  { sroi/stri }
      cdxs[5][2] := +realsize; { sror/strr }
      cdxs[5][3] := +intsize;  { srob/strb }
      cdxs[5][4] := +intsize;  { sroc/strc }
      cdxs[5][5] := +adrsize;  { sroa/stra }
      cdxs[5][6] := +setsize;  { sros/strs }
      cdxs[5][7] := 0;

      { note that all of the comparisions share the same table }
      cdxs[6][1] := +(intsize+intsize)-intsize; { equi/neqi/geqi/grti/leqi/lesi }
      cdxs[6][2] := +(realsize+realsize)-intsize; { equr/neqr/geqr/grtr/leqr/lesr }
      cdxs[6][3] := +(intsize+intsize)-intsize; { equb/neqb/geqb/grtb/leqb/lesb }
      cdxs[6][4] := +(intsize+intsize)-intsize; { equc/neqc/geqc/grtc/leqc/lesc }
      cdxs[6][5] := +(adrsize+intsize)-adrsize; { equa/neqa/geqa/grta/leqa/lesa }
      cdxs[6][6] := +(setsize+setsize)-intsize; { equs/neqs/geqs/grts/leqs/less }
      cdxs[6][7] := +(adrsize+adrsize)-intsize; { equm/neqm/geqm/grtm/leqm/lesm }

      pdx[ 1] := +adrsize;             pdx[ 2] := +adrsize;
      pdx[ 3] := +adrsize;             pdx[ 4] := +adrsize;
      pdx[ 5] := +adrsize;             pdx[ 6] := +adrsize*2;
      pdx[ 7] := 0;                    pdx[ 8] := +(realsize+intsize);
      pdx[ 9] := +intsize*2;           pdx[10] := +(adrsize+intsize*2);
      pdx[11] :=  0;                   pdx[12] := +ptrsize*2;
      pdx[13] :=  0;                   pdx[14] := +adrsize-intsize;
      pdx[15] :=  0;                   pdx[16] :=  0;
      pdx[17] :=  0;                   pdx[18] :=  0;
      pdx[19] :=  0;                   pdx[20] :=  0;
      pdx[21] :=  0;                   pdx[22] :=  0;
      pdx[23] :=  0;                   pdx[24] := +adrsize;
      pdx[25] := +adrsize;             pdx[26] := +adrsize;
      pdx[27] := +intsize*2;           pdx[28] := +(realsize+intsize*2);
      pdx[29] := +adrsize*2;           pdx[30] := +(adrsize+intsize);
      pdx[31] := +intsize;             pdx[32] := +realsize;
      pdx[33] := +intsize;             pdx[34] := +intsize;
      pdx[35] := +(intsize+adrsize);   pdx[36] := +adrsize;
      pdx[37] := +adrsize;             pdx[38] := +(intsize+adrsize);
      pdx[39] := +(intsize+adrsize);   pdx[40] := +(adrsize+intsize*2);
      pdx[41] := +(adrsize+intsize*2); pdx[42] := +(adrsize+intsize*2);
      pdx[43] := +(adrsize+intsize*2); pdx[44] := +adrsize-intsize;
      pdx[45] := +adrsize-intsize;     pdx[46] :=  0;
      pdx[47] := +intsize;             pdx[48] := +intsize;
      pdx[49] := +(intsize*3+adrsize);
    end;

  begin (*inittables*)
    reswords; symbols; rators;
    instrmnemonics; procmnemonics;
    chartypes; initdx;
  end (*inittables*) ;

begin

  { Suppress unreferenced errors. These are all MPB (machine parameter
    block) equations that need to stay the same between front end and backend. }
  if begincode = 0 then;
  if heapal = 0 then;
  if inthex = 0 then;
  if market = 0 then;
  if markwb = 0 then;
  if markep = 0 then;
  if markdl = 0 then;
  if markra = 0 then;
  if marksb = 0 then;
  if marksl = 0 then;
  if maxresult = 0 then;
  if maxsize = 0 then;
  if gbsal = 0 then;
  if ujplen = 0 then;

  write('P5 Pascal compiler vs. ', majorver:1, '.', minorver:1);
  if experiment then write('.x');
  writeln;
  writeln('Pascal-P5 complies with the requirements of level 0 of ISO/IEC 7185.');
  writeln;

  (*initialize*)
  (************)
  initscalars; initsets; inittables;

  (*enter standard names and standard types:*)
  (******************************************)
  level := 0; top := 0; scopen := 0;
  with display[0] do
    begin fname := nil; flabel := nil; fconst := nil; fstruct := nil;
          packing := false; occur := blck; bname := nil end;
  enterstdtypes;   stdnames; entstdnames;   enterundecl;
  top := 1; level := 1; scopen := scopen+1;
  with display[1] do
    begin fname := nil; flabel := nil; fconst := nil; fstruct := nil;
          packing := false; occur := blck; bname := nil end;

  (*compile:*)
  (**********)

  { !!! remove these statements for self compile }
#ifndef SELF_COMPILE
  reset(prd); rewrite(prr); { open output file }
#endif

  { write generator comment }
  writeln(prr, '!');
  writeln(prr, '! Pascal intermediate file Generated by P5 Pascal compiler vs. ',
          majorver:1, '.', minorver:1);
  writeln(prr, '!');

  { write initial option values }
  write(prr, 'o ');
  for c := 'a' to 'z' do
    if not (c in ['o','e','g','a','f','m','j','h','k','w','n','p','q']) then
      begin write(prr, c);
    if option[c] then write(prr, '+') else write(prr, '-')
  end;
  writeln(prr);

  insymbol;
  programme(blockbegsys+statbegsys);

  { dispose of levels 0 and 1 }
  putdsp(1);
  putdsp(0);

  { remove undeclared ids }
  exitundecl;

  writeln;
  writeln('Errors in program: ', toterr:1);
  { output error report as required }
  f := true;
  for i := 1 to 500 do if errtbl[i] then begin
    if f then begin
      writeln;
      writeln('Error numbers in listing:');
      writeln('-------------------------');
      f := false
    end;
    write(i:3, '  '); errmsg(i); writeln
  end;
  if not f then writeln;

  if doprtryc then begin { print recyling tracking counts }

    writeln;
    writeln('Recycling tracking counts:');
    writeln;
    writeln('string quants:              ', strcnt:1);
    writeln('constants:                  ', cspcnt:1);
    writeln('structures:                 ', stpcnt:1);
    writeln('identifiers:                ', ctpcnt:1);
    writeln('label counts:               ', lbpcnt:1);
    writeln('file tracking counts:       ', filcnt:1);
    writeln('case entry tracking counts: ', cipcnt:1);
    writeln;

  end;

  if doprtlab then prtlabels; { dump labels}
  if dodmpdsp then prtdsp; { dump display }

  { perform errors for recycling balance }

  if strcnt <> 0 then
     writeln('*** Error: Compiler internal error: string recycle balance: ',
             strcnt:1);
  if cspcnt <> 0 then
     writeln('*** Error: Compiler internal error: constant recycle balance: ',
             cspcnt:1);
  if stpcnt <> 0 then
     writeln('*** Error: Compiler internal error: structure recycle balance: ',
             stpcnt:1);
  if ctpcnt <> 0 then
     writeln('*** Error: Compiler internal error: identifier recycle balance: ',
             ctpcnt:1);
  if lbpcnt <> 0 then
     writeln('*** Error: Compiler internal error: label recycle balance: ',
             lbpcnt:1);
  if filcnt <> 0 then
     writeln('*** Error: Compiler internal error: file recycle balance: ',
             filcnt:1);
  if cipcnt <> 0 then
     writeln('*** Error: Compiler internal error: case recycle balance: ',
             cipcnt:1);
  if wtpcnt <> 0 then
     writeln('*** Error: Compiler internal error: with recycle balance: ',
             wtpcnt:1);

  99:

end.
