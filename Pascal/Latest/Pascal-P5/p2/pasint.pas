
(* ASSEMBLER AND INTERPRETER OF PASCAL CODE.  K.JENSEN, N.WIRTH, E.T.H. 15.3.73 *)
PROGRAM PCODE(INPUT,OUTPUT,PRD,PRR);
(* NOTE FOR THE IMPLEMENTATION.
   ===========================
THIS INTERPRETER IS WRITTEN FOR THE CASE WHERE ALL THE FUNDAMENTAL
TYPES TAKE ONE STORAGE UNIT.
IN AN IMPLEMENTATION ALL THE HANDLING OF THE SP POINTER HAS TO TAKE
INTO ACCOUNT THE FACT TAHT THE TYPES MAY HAVE A LENGTH DIFFERENT FROM
ONE. SO IN PUSH AND POP OPERATIONS THE IMPLEMENTOR HAS TO INCREASE
AND DECREASE THE SP NOT BY 1 BUT BY A NUMBER DEPENDING ON THE TYPE
CONCERNED.
WHERE A COMMENT SAYS THAT SOME VARIABLE IS EXPRESSED 'IN UNITS OF
STORAGE' THE VALUE OF THIS VARIABLE MUST NOT BE CORRECTED, BECAUSE
THE COMPILER HAS COMPUTED IT TAKING INTO ACCOUNT THE LENGTHS OF THE
TYPES INVOLVED.
THE SAME HOLDS FOR THE HANDLING OF THE NP POINTER (WHICH MUST NOT BE
CORRECTED)                                                       *)

(*****************************************************
 *                                                   *
 * CONVERTED TO ISO 7185 PASCAL BY SCOTT A. MOORE    *
 * [SAM] ON JAN 22, 2011.                            *
 *                                                   *
 * VARIOUS CHANGES WERE MADE, ALL MARKED WITH MY     *
 * INITIALS THUS [SAM]. THERE ARE COMMENTS FOR ALL   *
 * CHANGES MADE. THE ONLY OTHERS WERE MINOR FORMAT   *
 * GLITCHES, APPARENTLY DUE TO SEVERAL EOLS          *
 * INSERTED AT VARIOUS PLACES INTO THE CODE.         *
 *                                                   *
 *****************************************************)

LABEL 1;
CONST  CODEMAX     = 6735;  (* SIZE OF PROGRAM AREA *)
       PCMAX       = 13470; (* 2 * CODEMAX *)
       MAXSTK      = 13650; (* SIZE OF VARIABLE STORE *)
       OVERI       = 13655; (* SIZE OF INTEGER CONSTANT TABLE = 5 *)
       OVERR       = 13660; (* SIZE OF REAL CONSTANT TABLE = 5 *)
       OVERS       = 13730; (* SIZE OF SET CONSTANT TABLE = 70 *)
       OVERB       = 13734; (* SIZE OF BOUNDARY CONSTANT TABLE = 4 *)
       OVERM       = 15034; (* SIZE OF MULTIPLE CONSTANT TABLE = 1300 *)
       MAXSTR      = 15035;
       LARGEINT    = 524288;  (* = 2**19 *)
       BEGINCODE   = 3;
       INPUTADR    = 4;    (* ABSOLUTE ADDRESS *)
       OUTPUTADR   = 5;
       PRDADR      = 6;
       PRRADR      = 7;

TYPE  BIT4         = 0..15;
      BIT6         = 0..63;
      BIT20        = -524287..524287;
      DATATYPE     = (UNDEF,INT,REEL,BOOL,SETT,ADR,MARK);
      ADDRESS      = -1..MAXSTR;
      BETA         = PACKED ARRAY[1..25] OF CHAR; (*ERROR MESSAGE*)
      { ALFA WAS APPARENTLY A COMPILER DEFINED TYPE. [SAM] }
      ALFA = PACKED ARRAY [1..10] OF CHAR;

VAR  CODE          : ARRAY[0..CODEMAX] OF   (* THE PROGRAM *)
                     PACKED RECORD  OP1    :BIT6;
                                    P1     :BIT4;
                                    Q1     :BIT20;
                                    OP2    :BIT6;
                                    P2     :BIT4;
                                    Q2     :BIT20
                            END;
     PC            : 0..PCMAX;  (*PROGRAM ADDRESS REGISTER*)
     OP : BIT6;  P : BIT4;  Q : BIT20;  (*INSTRUCTION REGISTER*)

     STORE         : ARRAY [0..OVERM] OF
                        RECORD  CASE STYPE :DATATYPE OF
                                UNDEF      :(); { ISO 7185 requires all cases present. [sam] }
                                INT        :(VI :INTEGER);
                                REEL       :(VR :REAL);
                                BOOL       :(VB :BOOLEAN);
                                SETT       :(VS :SET OF 0..58);
                                ADR        :(VA :ADDRESS); (*ADDRESS IN STORE*)
                                MARK       :(VM :INTEGER);
                        END;
     MP,SP,NP      : ADDRESS;  (* ADDRESS REGISTERS *)
     (*MP  POINTS TO BEGINNING OF A DATA SEGMENT
      SP  POINTS TO TOP OF THE STACK
      NP  POINTS TO TOP OF DYNAMICLY ALLOCATED AREA*)

     INTERPRETING  : BOOLEAN;
     { PRD,PRR: TEXT; } (*PRD FOR READ ONLY, PRR FOR WRITE ONLY *)

     INSTR         : ARRAY[BIT6] OF ALFA; (* MNEMONIC INSTRUCTION CODES *)
     SPTABLE       : ARRAY[0..20] OF ALFA; (* STANDARD FUNCTIONS AND  PROCEDURES *)


(*-----------------------------------------------------------------------------*)

PROCEDURE LOAD;
   CONST MAXLABEL = 1550; (* COMPLETE COMPILER PROCESSING *)
   TYPE  LABELST  = (ENTERED,DEFINED); (*LABEL SITUATION*)
         LABELRG  = 0..MAXLABEL;   (*LABEL RANGE*)
         LABELREC = RECORD
                          VAL: ADDRESS;
                           ST: LABELST
                    END;
   VAR  ICP,RCP,SCP,BCP,MCP  : ADDRESS;  (*POINTERS TO NEXT FREE POSITION*)
        WORD : ARRAY[1..10] OF CHAR; I  : INTEGER;  CH  : CHAR;
        LABELTAB: ARRAY[LABELRG] OF LABELREC;
        LABELVALUE: ADDRESS;

   PROCEDURE INIT;
      VAR I: INTEGER;
   BEGIN INSTR[ 0]:='LOD       '; INSTR[ 1]:='LDO       ';
         INSTR[ 2]:='STR       '; INSTR[ 3]:='SRO       ';
         INSTR[ 4]:='LDA       '; INSTR[ 5]:='LAO       ';
         INSTR[ 6]:='STO       '; INSTR[ 7]:='LDC       ';
         INSTR[ 8]:='...       '; INSTR[ 9]:='IND       ';
         INSTR[10]:='INC       '; INSTR[11]:='MST       ';
         INSTR[12]:='CUP       '; INSTR[13]:='ENT       ';
         INSTR[14]:='RET       '; INSTR[15]:='CSP       ';
         INSTR[16]:='IXA       '; INSTR[17]:='EQU       ';
         INSTR[18]:='NEQ       '; INSTR[19]:='GEQ       ';
         INSTR[20]:='GRT       '; INSTR[21]:='LEQ       ';
         INSTR[22]:='LES       '; INSTR[23]:='UJP       ';
         INSTR[24]:='FJP       '; INSTR[25]:='XJP       ';
         INSTR[26]:='CHK       '; INSTR[27]:='EOF       ';
         INSTR[28]:='ADI       '; INSTR[29]:='ADR       ';
         INSTR[30]:='SBI       '; INSTR[31]:='SBR       ';
         INSTR[32]:='SGS       '; INSTR[33]:='FLT       ';
         INSTR[34]:='FLO       '; INSTR[35]:='TRC       ';
         INSTR[36]:='NGI       '; INSTR[37]:='NGR       ';
         INSTR[38]:='SQI       '; INSTR[39]:='SQR       ';
         INSTR[40]:='ABI       '; INSTR[41]:='ABR       ';
         INSTR[42]:='NOT       '; INSTR[43]:='AND       ';
         INSTR[44]:='IOR       '; INSTR[45]:='DIF       ';
         INSTR[46]:='INT       '; INSTR[47]:='UNI       ';
         INSTR[48]:='INN       '; INSTR[49]:='MOD       ';
         INSTR[50]:='ODD       '; INSTR[51]:='MPI       ';
         INSTR[52]:='MPR       '; INSTR[53]:='DVI       ';
         INSTR[54]:='DVR       '; INSTR[55]:='MOV       ';
         INSTR[56]:='LCA       '; INSTR[57]:='DEC       ';
         INSTR[58]:='STP       ';

         SPTABLE[ 0]:='GET       '; SPTABLE[ 1]:='PUT       ';
         SPTABLE[ 2]:='RST       '; SPTABLE[ 3]:='RLN       ';
         SPTABLE[ 4]:='NEW       '; SPTABLE[ 5]:='WLN       ';
         SPTABLE[ 6]:='WRS       '; SPTABLE[ 7]:='ELN       ';
         SPTABLE[ 8]:='WRI       '; SPTABLE[ 9]:='WRR       ';
         SPTABLE[10]:='WRC       '; SPTABLE[11]:='RDI       ';
         SPTABLE[12]:='RDR       '; SPTABLE[13]:='RDC       ';
         SPTABLE[14]:='SIN       '; SPTABLE[15]:='COS       ';
         SPTABLE[16]:='EXP       '; SPTABLE[17]:='LOG       ';
         SPTABLE[18]:='SQT       '; SPTABLE[19]:='ATN       ';
         SPTABLE[20]:='SAV       ';
         PC:= BEGINCODE;
         ICP:= MAXSTK+1; FOR I:= ICP TO OVERI DO STORE[I].STYPE:= INT;
         RCP:= OVERI+1; FOR I:= RCP TO OVERR DO STORE[I].STYPE:= REEL;
         SCP:= OVERR+1; FOR I:= SCP TO OVERS DO STORE[I].STYPE:= SETT;
         BCP:= OVERS+2; FOR I:= OVERS+1 TO OVERB DO STORE[I].STYPE:= INT;
         MCP:= OVERB+1; FOR I:= MCP TO OVERM DO STORE[I].STYPE:= INT;
         FOR I:= 1 TO 10 DO WORD[I]:= ' ';
         FOR I:= 0 TO MAXLABEL DO
             WITH LABELTAB[I] DO BEGIN VAL:=-1; ST:= ENTERED END;
         { RESET(PRD); }
   END;(*INIT*)
   PROCEDURE ERRORL(STRING: BETA); (*ERROR IN LOADING*)
   BEGIN WRITELN;
         WRITE(STRING); GOTO 1 (* TO END PROGRAM PCODE*)
   END; (*ERRORL*)
   PROCEDURE UPDATE(X: LABELRG); (*WHEN A LABEL DEFINITION LX IS FOUND*)
      VAR CURR,SUCC: -1..PCMAX; (*RESP. CURRENT ELEMENT AND SUCCESSOR ELEMENT
                                      OF A LIST OF FUTURE REFERENCE*)
          ENDLIST: BOOLEAN;
   BEGIN
      IF LABELTAB[X].ST=DEFINED THEN ERRORL(' DUPLICATED LABEL        ')
      ELSE BEGIN
             IF LABELTAB[X].VAL<>-1 THEN (*FORWARD REFERENCE(S)*)
             BEGIN CURR:= LABELTAB[X].VAL; ENDLIST:= FALSE;
                WHILE NOT ENDLIST DO
                      WITH CODE[CURR DIV 2] DO
                      BEGIN
                         IF ODD(CURR) THEN BEGIN SUCC:= Q2;
                                                 Q2:= LABELVALUE
                                           END
                                          ELSE BEGIN SUCC:= Q1;
                                                     Q1:= LABELVALUE
                                               END;
                         IF SUCC=-1 THEN ENDLIST:= TRUE
                                    ELSE CURR:= SUCC
                      END;
              END;
              LABELTAB[X].ST:= DEFINED;
              LABELTAB[X].VAL:= LABELVALUE;
           END
   END;(*UPDATE*)
   PROCEDURE ASSEMBLE; FORWARD;
   PROCEDURE GENERATE;(*GENERATE SEGMENT OF CODE*)
      VAR X: INTEGER; (* LABEL NUMMER *)
   BEGIN
      WHILE NOT EOLN(PRD) DO
            BEGIN READ(PRD,CH);(* FIRST LINE OF CHARACTER*)
                  CASE CH OF
                       'I': READLN(PRD);
                       'L': BEGIN READ(PRD,X);
                                  IF NOT EOLN(PRD) THEN READ(PRD,CH);
                                  IF CH='=' THEN READ(PRD,LABELVALUE)
                                            ELSE LABELVALUE:= PC;
                                  UPDATE(X); READLN(PRD);
                            END;
                       ' ': BEGIN READ(PRD,CH); ASSEMBLE END
                  END;
            END
   END; (*GENERATE*)
   PROCEDURE ASSEMBLE; (*TRANSLATE SYMBOLIC CODE INTO MACHINE CODE AND STORE*)
      VAR NAME :ALFA;  B :BOOLEAN;  R :REAL;  S :SET OF 0..58;
          C1 :CHAR;  I,S1,LB,UB :INTEGER;
      PROCEDURE LOOKUP(X: LABELRG); (* SEARCH IN LABEL TABLE*)
      BEGIN CASE LABELTAB[X].ST OF
            ENTERED:IF LABELTAB[X].VAL=-1 THEN BEGIN LABELTAB[X].VAL:=PC;
                                                     Q:=-1(*NIL=-1*)
                                               END
                    ELSE BEGIN Q:=LABELTAB[X].VAL;
                               LABELTAB[X].VAL:=PC
                         END;
           DEFINED: Q:= LABELTAB[X].VAL
           END(*CASE LABEL..*)
      END;(*LOOKUP*)
      PROCEDURE LABELSEARCH;
         VAR X: LABELRG;
      BEGIN WHILE (CH<>'L') AND NOT EOLN(PRD) DO READ(PRD,CH);
            READ(PRD,X); LOOKUP(X)
      END;(*LABELSEARCH*)

      PROCEDURE GETNAME;
      BEGIN  WORD[1] := CH;
         READ(PRD,WORD[2],WORD[3]);
         IF NOT EOLN(PRD) THEN READ(PRD,CH) (*NEXT CHARACTER*);
         PACK(WORD,1,NAME)
      END; (*GETNAME*)

   BEGIN  P := 0;  Q := 0;  OP := 0;
      GETNAME;
      WHILE INSTR[OP]<>NAME DO OP := OP+1;

      CASE OP OF  (* GET PARAMETERS P,Q *)

          (*EQU,NEQ,GEQ,GRT,LEQ,LES*)
          17,18,19,
          20,21,22 :    BEGIN CASE CH OF
                              'A': ; (*P = 0*)
                              'I': P := 1;
                              'R': P := 2;
                              'B': P := 3;
                              'S': P := 4;
                              'M' :BEGIN P := 5;
                                     READ(PRD,Q)
                                   END
                              END
                          END;

          (*LOD,STR,LDA*)
          0,2,4 : READ(PRD,P,Q);
          12 (*CUP*): BEGIN READ(PRD,P); LABELSEARCH END;

          11 (*MST*) :    READ(PRD,P);

          14 (*RET*) : CASE CH OF
                            'P': P:=0;
                            'I': P:=1;
                            'R': P:=2;
                            'C': P:=3;
                            'B': P:=4;
                            'A': P:= 5
                       END;

          (*LDO,SRO,LAO,IND,INC,IXA,MOV,DEC*)
          1,3,5,9,10,
          16,55,57: READ(PRD,Q);
          (*ENT,UJP,FJP,XJP*)
          13,23,24,25: LABELSEARCH;

          15 (*CSP*) :    BEGIN FOR I:=1 TO 9 DO READ(PRD,CH); GETNAME;
                           WHILE NAME<>SPTABLE[Q] DO  Q := Q+1
                          END;

          7 (*LDC*) :     BEGIN CASE CH OF  (*GET Q*)
                           'I' :BEGIN  P := 1;  READ(PRD,I);
                                   IF ABS(I)>=LARGEINT THEN
                                   BEGIN  OP := 8;
                                      STORE[ICP].VI := I;  Q := MAXSTK;
                                      REPEAT  Q := Q+1  UNTIL STORE[Q].VI=I;
                                      IF Q=ICP THEN
                                      BEGIN  ICP := ICP+1;
                                         IF ICP=OVERI THEN ERRORL(' INTEGER TABLE OVERFLOW  ')
                                      END
                                   END  ELSE Q := I
                                END;

                           'R' :BEGIN  OP := 8; P := 2;
                                   READ(PRD,R);
                                   STORE[RCP].VR := R;  Q := OVERI;
                                   REPEAT  Q := Q+1  UNTIL STORE[Q].VR=R;
                                   IF Q=RCP THEN
                                      BEGIN  RCP := RCP+1;
                                      IF RCP=OVERR THEN ERRORL(' REAL TABLE OVERFLOW     ')
                                   END
                                END;

                           'N' :; (*P,Q = 0*)

                           'B' :BEGIN  P := 3;  READ(PRD,Q)  END;

                           '(' :BEGIN  OP := 8;  P := 4;
                                   S := [ ];  READ(PRD,CH);
                                   WHILE CH<>')' DO
                                   BEGIN READ(PRD,S1,CH); S := S + [S1]
                                   END;
                                   STORE[SCP].VS := S;  Q := OVERR;
                                   REPEAT  Q := Q+1  UNTIL STORE[Q].VS=S;
                                   IF Q=SCP THEN
                                   BEGIN  SCP := SCP+1;
                                      IF SCP=OVERS THEN ERRORL(' SET TABLE OVERFLOW      ')
                                   END
                                END
                           END (*CASE*)
                        END;

          26 (*CHK*) :    BEGIN  READ(PRD,LB,UB);
                           STORE[BCP-1].VI := LB; STORE[BCP].VI := UB;
                           Q := OVERS;
                           REPEAT  Q := Q+2
                           UNTIL (STORE[Q-1].VI=LB)AND (STORE[Q].VI=UB);
                           IF Q=BCP THEN
                           BEGIN  BCP := BCP+2;
                              IF BCP=OVERB THEN ERRORL(' BOUNDARY TABLE OVERFLOW ')
                           END
                        END;

          56 (*LCA*) :    BEGIN  READ(PRD,CH);  (*CH = FIRST CHAR IN STRING*)
                           Q := MCP;
                           WHILE CH<>'''' DO
                           BEGIN STORE[MCP].VI := ORD(CH);
                              MCP := MCP+1;  READ(PRD,CH)
                           END
                        END;

          6,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
          48,49,50,51,52,53,54,58  :  ;

      END; (*CASE*)

      READLN(PRD);
      (* STORE INSTRUCTION *)
      WITH CODE[PC DIV 2] DO
         IF ODD(PC) THEN
         BEGIN  OP2 := OP; P2 := P; Q2 := Q
         END  ELSE
         BEGIN  OP1 := OP; P1 := P; Q1 := Q
         END;
      PC := PC+1;
   END; (*ASSEMBLE*)

BEGIN (*LOAD*)
   INIT;
   GENERATE;
   PC:=0; READ(PRD,CH);
   GENERATE;

END; (*LOAD*)

(*------------------------------------------------------------------------*)


PROCEDURE PMD;
   VAR S :INTEGER; I: INTEGER;

   PROCEDURE PT;
   { ADDED INDEX VARIABLE J. [SAM] }
   VAR J: INTEGER;
   BEGIN  WRITE(S:6);
      CASE STORE[S].STYPE OF
          UNDEF: BEGIN WHILE (S>=1)AND (STORE[S].STYPE=UNDEF) DO S := S-1;
                    IF S>=1 THEN S := S+1;
                    WRITE(' --',S:5,' UNDEF')
                 END;
          INT  : WRITE(STORE[S].VI);
          REEL : WRITE(STORE[S].VR);
          BOOL : WRITE(STORE[S].VB);
          { CHANGED SET WRITE TO BITS. THE OLD FORMULATION WAS COMPILER
            DEPENDENT. [SAM] }
          SETT : {WRITE(STORE[S].VS:21 OCT);}
                 BEGIN
                   WRITE('[');
                   FOR J := 1 TO 58 DO IF J IN STORE[S].VS THEN WRITE('1')
                   ELSE WRITE('0');
                   WRITE(']')
                 END;
          ADR  : WRITE('  ^ ',STORE[S].VA:6);
          MARK : WRITE(' ***',STORE[S].VM:6)
      END; (*CASE*)
      S := S - 1;
      I := I + 1;
write('>', i:1, '<');
      IF I = 4 THEN
         BEGIN WRITELN(OUTPUT); I := 0 END;
   END; (*PT*)

BEGIN WRITE(' PC =',PC-1:5,' OP =',OP:3,'  SP =',SP:5,'  MP =',MP:5,'  NP =',NP:5);
   WRITELN; WRITELN('--------------------------------------');
   S := SP; I := 0;
   WHILE S>=0 DO PT;
   S := MAXSTK;
   WHILE S>=NP DO PT;
END; (*PMD*)
PROCEDURE ERRORI(STRING: BETA);
BEGIN WRITELN; WRITELN(STRING);
      PMD; GOTO 1
END;(*ERRORI*)

FUNCTION BASE(LD :INTEGER):ADDRESS;
   VAR AD :ADDRESS;
BEGIN  AD := MP;
   WHILE LD>0 DO
   BEGIN  AD := STORE[AD+1].VM;  LD := LD-1
   END;
   BASE := AD
END; (*BASE*)


PROCEDURE EX0;
   VAR AD,AD1 :ADDRESS;  I,J: INTEGER;
   PROCEDURE PUSH;
   BEGIN  SP := SP+1;
      IF SP>=NP THEN ERRORI(' STORE OVERFLOW          ')
   END;
   PROCEDURE CALLSP;
      VAR LINE: BOOLEAN; ADPTR,ADELNT: ADDRESS;
          I: INTEGER;
      PROCEDURE READI(VAR F:TEXT);
         VAR AD: ADDRESS;
      BEGIN AD:= STORE[SP-1].VA;
            STORE[AD].STYPE:= INT; READ(F,STORE[AD].VI);
            STORE[STORE[SP].VA].VI:= ORD(F^);
            SP:= SP-2
      END;(*READI*)
      PROCEDURE READR(VAR F: TEXT);
         VAR AD: ADDRESS;
      BEGIN AD:= STORE[SP-1].VA;
            STORE[AD].STYPE:= REEL; READ(F,STORE[AD].VR);
            STORE[STORE[SP].VA].VI:= ORD(F^);
            SP:= SP-2
      END;(*READR*)
      PROCEDURE READC(VAR F: TEXT);
         VAR C: CHAR; AD: ADDRESS;
      BEGIN READ(F,C);
            AD:= STORE[SP-1].VA;
            STORE[AD].STYPE:= INT; STORE[AD].VI:= ORD(C);
            STORE[STORE[SP].VA].VI:= ORD(F^);
            SP:= SP-2
      END;(*READC*)
      PROCEDURE WRITESTR(VAR F: TEXT);
         VAR I,J,K: INTEGER;
             AD: ADDRESS;
      BEGIN AD:= STORE[SP-3].VA;
            K:= STORE[SP-1].VI; J:= STORE[SP-2].VI;
           (* J AND K ARE NUMBERS OF CHARACTERS *)
            IF K>J THEN FOR I:=1 TO K-J DO WRITE(F,' ')
                   ELSE J:= K;
            FOR I:=0 TO J-1 DO WRITE(F,CHR(STORE[AD+I].VI));
            (* IN THE INDEX OF STORE I HAS TO BE MULTIPLIED
               BY CHARSIZE *)
            SP:= SP-4
      END;(*WRITESTR*)
      PROCEDURE GETFILE(VAR F: TEXT);
         VAR AD: ADDRESS;
      BEGIN AD:=STORE[SP].VA;
            GET(F); STORE[AD].VI:= ORD(F^);
            SP:=SP-1
      END;(*GETFILE*)
      PROCEDURE PUTFILE(VAR F: TEXT);
         VAR AD: ADDRESS;
      BEGIN AD:= STORE[SP].VA;
            F^:= CHR(STORE[AD].VI); PUT(F);
            STORE[AD].STYPE:= UNDEF;
            SP:= SP-1;
      END;(*PUTFILE*)
   BEGIN (*CALLSP*)
         CASE Q OF
              0 (*GET*): CASE STORE[SP].VA OF
                              4: GETFILE(INPUT);
                              5: ERRORI(' GET ON OUTPUT FILE      ');
                              6: GETFILE(PRD);
                              7: ERRORI(' GET ON PRR FILE         ')
                         END;
              1 (*PUT*): CASE STORE[SP].VA OF
                              4: ERRORI(' PUT ON READ FILE        ');
                              5: PUTFILE(OUTPUT);
                              6: ERRORI(' PUT ON PRD FILE         ');
                              7: PUTFILE(PRR)
                         END;
              2 (*RST*): BEGIN NP:= STORE[SP].VI; SP:=SP-1
                         END;
              3 (*RLN*) : BEGIN CASE STORE[SP].VA OF
                                     4: BEGIN READLN(INPUT);STORE[INPUTADR].VI:=ORD(INPUT^) END;
                                     5: ERRORI(' READLN ON OUTPUT FILE   ');
                                     6: BEGIN READLN(PRD); STORE[PRDADR].VI:= ORD(PRD^) END;
                                     7: ERRORI(' READLN ON PRR FILE      ')
                                END;
                                SP:= SP-1
                          END;
               4 (*NEW*): BEGIN AD:= NP-STORE[SP].VA;
                          (*TOP OF STACK GIVES THE LENGTH IN UNITS OF STORAGE *)
                                IF AD<= SP THEN ERRORI(' STORE OVERFLOW          ');
                                FOR I:=NP-1 DOWNTO AD DO STORE[I].STYPE:= UNDEF;
                                NP:= AD; AD:= STORE[SP-1].VA;
                                STORE[AD].STYPE:=ADR; STORE[AD].VA:= NP;
                                SP:=SP-2
                          END;
               5 (*WLN*) : BEGIN CASE STORE[SP].VA OF
                                      4: ERRORI(' WRITELN ON INPUT FILE   ');
                                      5: WRITELN(OUTPUT);
                                      6: ERRORI(' WRITELN ON PRD FILE     ');
                                      7: WRITELN(PRR)
                                 END;
                                 SP:= SP-1
                           END;
               6 (*WRS*): CASE STORE[SP].VA OF
                               4: ERRORI(' WRITE ON INPUT FILE     ');
                               5: WRITESTR(OUTPUT);
                               6: ERRORI(' WRITE ON PRD FILE       ');
                               7: WRITESTR(PRR)
                          END;
               7 (*ELN*) : BEGIN CASE STORE[SP].VA OF
                                      4: LINE:= EOLN(INPUT);
                                      5: ERRORI(' EOLN OUTPUT FILE        ');
                                      6: LINE:=EOLN(PRD);
                                      7: ERRORI(' EOLN ON PRR FILE        ')
                                 END;
                                 STORE[SP].STYPE:= BOOL; STORE[SP].VB:= LINE
                           END;
               8 (*WRI*) : BEGIN CASE STORE[SP].VA OF
                                      4: ERRORI(' WRITE ON INPUT FILE     ');
                                      5: WRITE(OUTPUT,STORE[SP-2].VI:STORE[SP-1].VI);
                                      6: ERRORI(' WRITE ON PRD FILE       ');
                                      7: WRITE(PRR,STORE[SP-2].VI:STORE[SP-1].VI)
                                  END;
                                 SP:=SP-3
                           END;
               9 (*WRR*) : BEGIN CASE STORE[SP].VA OF
                                      4: ERRORI(' WRITE ON INPUT FILE     ');
                                      5: WRITE(OUTPUT,STORE[SP-2].VR:STORE[SP-1].VI);
                                      6: ERRORI(' WRITE ON PRD FILE       ');
                                      7: WRITE(PRR,STORE[SP-2].VR:STORE[SP-1].VI)
                                 END;
                                 SP:=SP-3
                          END;
               10 (*WRC*):BEGIN CASE STORE[SP].VA OF
                                     4: ERRORI(' WRITE ON INPUT FILE     ');
                                     5: WRITE(OUTPUT,CHR(STORE[SP-2].VI):STORE[SP-1].VI);
                                     6: ERRORI(' WRITE ON PRD FILE       ');
                                     7: WRITE(PRR,CHR(STORE[SP-2].VI):STORE[SP-1].VI)
                                END;
                                SP:=SP-3
                          END;
               11(*RDI*) : CASE STORE[SP].VA OF
                                4: READI(INPUT);
                                5: ERRORI(' READ ON OUTPUT FILE     ');
                                6: READI(PRD);
                                7: ERRORI(' READ ON PRR FILE        ')
                          END;
               12(*RDR*) : CASE STORE[SP].VA OF
                                4: READR(INPUT);
                                5: ERRORI(' READ ON OUTPUT FILE     ');
                                6: READR(PRD);
                                7: ERRORI(' READ ON PRR FILE        ')
                           END;
               13(*RDC*):  CASE STORE[SP].VA OF
                                4: READC(INPUT);
                                5: ERRORI(' READ ON OUTPUT FILE     ');
                                6: READC(PRD);
                                7: ERRORI(' READ ON PRR FILE        ')
                           END;
               14(*SIN*): STORE[SP].VR:= SIN(STORE[SP].VR);
               15(*COS*): STORE[SP].VR:= COS(STORE[SP].VR);
               16(*EXP*): STORE[SP].VR:= EXP(STORE[SP].VR);
               17(*LOG*): STORE[SP].VR:= LN(STORE[SP].VR);
               18(*SQT*): STORE[SP].VR:= SQRT(STORE[SP].VR);
               19(*ATN*): STORE[SP].VR:= ARCTAN(STORE[SP].VR);
               20(*SAV*): BEGIN AD:=STORE[SP].VA;
                             STORE[AD].STYPE:=ADR; STORE[AD].VI:= NP;
                             SP:= SP-1
                          END;
         END;(*CASE Q*)
   END;(*CALLSP*)

BEGIN  CASE OP OF (* IN THIS PROCEDURE Q MUST NOT BE CORRECTED *)

          0 (*LOD*): BEGIN  AD := BASE(P) + Q;
                      IF STORE[AD].STYPE=UNDEF THEN ERRORI(' VALUE UNDEFINED         ');
                      PUSH;
                      STORE[SP] := STORE[AD]
                   END;

          1 (*LDO*): BEGIN
                      IF STORE[Q].STYPE=UNDEF THEN ERRORI(' VALUE UNDEFINED         ');
                      PUSH;
                      STORE[SP] := STORE[Q]
                   END;

          2 (*STR*): BEGIN  STORE[BASE(P)+Q] := STORE[SP];  SP := SP-1
                   END;

          3 (*SRO*): BEGIN  STORE[Q] := STORE[SP];  SP := SP-1
                   END;

          4 (*LDA*): BEGIN  PUSH;
                      STORE[SP].STYPE := ADR; STORE[SP].VA := BASE(P) + Q
                   END;

          5 (*LAO*): BEGIN  PUSH;
                      STORE[SP].STYPE := ADR; STORE[SP].VA := Q
                   END;

          6 (*STO*): BEGIN  STORE[STORE[SP-1].VA] := STORE[SP];  SP := SP-2
                   END;

          7 (*LDC*): BEGIN  PUSH;
                      IF P=1 THEN
                      BEGIN  STORE[SP].STYPE := INT; STORE[SP].VI := Q
                      END ELSE
                          IF P=3 THEN
                          BEGIN  STORE[SP].STYPE := BOOL; STORE[SP].VB := Q=1
                          END ELSE (*LOAD NIL*)
                          BEGIN  STORE[SP].STYPE := ADR; STORE[SP].VA := MAXSTR
                          END
                   END;

          8 (*LCI*): BEGIN  PUSH;  STORE[SP] := STORE[Q]
                   END;

          9 (*IND*): BEGIN  AD := STORE[SP].VI + Q; (* Q IS A NUMBER OF STORAGE UNITS *)
                      IF STORE[AD].STYPE=UNDEF THEN ERRORI(' VALUE UNDEFINED         ');
                      STORE[SP] := STORE[AD]
                   END;

          10 (*INC*):STORE[SP].VI := STORE[SP].VI + Q;

          11 (*MST*):BEGIN (*P=LEVEL OF CALLING PROCEDURE MINUS LEVEL OF CALLED
                          PROCEDURE + 1;  SET DL AND SL, INCREMENT SP*)
                      STORE[SP+1].STYPE := UNDEF;
                      (* THEN LENTH OF THIS ELEMENT IS
                        MAX(INTSIZE,REALSIZE,BOOLSIZE,CHARSIZE,PTRSIZE *)
                      STORE[SP+2].STYPE := MARK;  STORE[SP+2].VM := BASE(P);
                      (* THE LENGTH OF THIS ELEMENT IS PTRSIZE *)
                      STORE[SP+3].STYPE := MARK;  STORE[SP+3].VM := MP;
                      (* IDEM *)
                      STORE[SP+4].STYPE := UNDEF;
                      (* IDEM *)
                      SP := SP+4
                   END;

          12 (*CUP*):BEGIN  (*P=NO OF LOCATIONS FOR PARAMETERS, Q=ENTRY POINT*)
                      MP := SP-(P+3);
                      STORE[MP+3].STYPE := MARK;  STORE[MP+3].VM := PC;
                      PC := Q
                   END;

          13 (*ENT*):BEGIN  J := MP+Q;  (*Q=LENGTH OF DATA SEG*)
                      IF J>NP THEN ERRORI(' STORE OVERFLOW          ');
                      (*RESET TO UNDEFINED--MAY DECIDE TO REMOVE THIS TEST*)
                      IF SP<INPUTADR THEN SP := PRDADR;
                      FOR I := SP+1 TO J DO STORE[I].STYPE := UNDEF;
                      SP := J;
                   END;

          14 (*RET*):BEGIN  CASE P OF
                                 0: SP:= MP-1;
                                 1,2,3,4,5: SP:= MP
                            END;
                            PC:= STORE[MP+3].VM;
                            MP:= STORE[MP+2].VM;
                     END;

          15 (*CSP*): CALLSP;

       END (*CASE OP*)
END; (*EX0*)

PROCEDURE EX1;
   VAR I,I1,I2  :INTEGER;  B :BOOLEAN;

   PROCEDURE COMPARE;
   BEGIN  I1 := STORE[SP].VA;  I2 := STORE[SP+1].VA;
      I := 0;  B := TRUE;
      WHILE B AND (I<>Q) DO
         IF STORE[I1+I].VI=STORE[I2+I].VI THEN I := I+1
         ELSE B := FALSE
   END; (*COMPARE*)

BEGIN  CASE OP OF (* IN THIS PROCEDURE Q MUST NOT BE CORRECTED *)

          16 (*IXA*):BEGIN  SP := SP-1; (* Q IS A NUMBER OF STORAGE UNITS *)
                      STORE[SP].VA := Q*STORE[SP+1].VA + STORE[SP].VA
                   END;

          17 (*EQU*):BEGIN  SP := SP-1;
                      CASE P OF
                      0,1: B := STORE[SP].VI=STORE[SP+1].VI;
                        2: B := STORE[SP].VR=STORE[SP+1].VR;
                        3: B := STORE[SP].VB=STORE[SP+1].VB;
                        4: B := STORE[SP].VS=STORE[SP+1].VS;
                        5: COMPARE;
                      END; (*CASE P*)
                      STORE[SP].STYPE := BOOL;
                      STORE[SP].VB := B
                   END;

          18 (*NEQ*):BEGIN  SP := SP-1;
                      CASE P OF
                      0,1: B := STORE[SP].VI<>STORE[SP+1].VI;
                        2: B := STORE[SP].VR<>STORE[SP+1].VR;
                        3: B := STORE[SP].VB<>STORE[SP+1].VB;
                        4: B := STORE[SP].VS<>STORE[SP+1].VS;
                        5: BEGIN  COMPARE;
                              B := NOT B;
                           END
                      END; (*CASE P*)
                      STORE[SP].STYPE := BOOL;
                      STORE[SP].VB := B
                   END;

          19 (*GEQ*):BEGIN  SP := SP-1;
                      CASE P OF
                      0,1: B := STORE[SP].VI>=STORE[SP+1].VI;
                        2: B := STORE[SP].VR>=STORE[SP+1].VR;
                        3: B := STORE[SP].VB>=STORE[SP+1].VB;
                        4: B := STORE[SP].VS>=STORE[SP+1].VS;
                        5: BEGIN COMPARE;
                              B := (STORE[I1+I].VI>=STORE[I2+I].VI)OR B
                           END
                      END; (*CASE P*)
                      STORE[SP].STYPE := BOOL;
                      STORE[SP].VB := B
                   END;

          20 (*GRT*):BEGIN  SP := SP-1;
                      CASE P OF
                      0,1: B := STORE[SP].VI>STORE[SP+1].VI;
                        2: B := STORE[SP].VR>STORE[SP+1].VR;
                        3: B := STORE[SP].VB>STORE[SP+1].VB;
                        4: ERRORI(' SET INCLUSION           ');
                        5: BEGIN  COMPARE;
                              B := (STORE[I1+I].VI>STORE[I2+I].VI)AND NOT B
                           END
                      END; (*CASEP*)
                      STORE[SP].STYPE := BOOL;
                      STORE[SP].VB := B
                   END;

          21 (*LEQ*):BEGIN  SP := SP-1;
                      CASE P OF
                      0,1: B := STORE[SP].VI<=STORE[SP+1].VI;
                        2: B := STORE[SP].VR<=STORE[SP+1].VR;
                        3: B := STORE[SP].VB<=STORE[SP+1].VB;
                        4: B := STORE[SP].VS<=STORE[SP+1].VS;
                        5: BEGIN  COMPARE;
                              B := (STORE[I1+I].VI<=STORE[I2+I].VI)OR B
                           END;
                      END; (*CASE P*)
                      STORE[SP].STYPE := BOOL;
                      STORE[SP].VB := B
                   END;

          22 (*LES*):BEGIN  SP := SP-1;
                      CASE P OF
                      0,1: B := STORE[SP].VI<STORE[SP+1].VI;
                        2: B := STORE[SP].VR<STORE[SP+1].VR;
                        3: B := STORE[SP].VB<STORE[SP+1].VB;
                        5: BEGIN  COMPARE;
                              B := (STORE[I1+I].VI<STORE[I2+I].VI)AND NOT B
                           END
                      END; (*CASE P*)
                      STORE[SP].STYPE := BOOL;
                      STORE[SP].VB := B
                   END;


          23 (*UJP*):PC := Q;

          24 (*FJP*):BEGIN  IF NOT STORE[SP].VB THEN PC := Q;  SP := SP-1
                   END;

          25 (*XJP*):BEGIN  PC := STORE[SP].VI + Q;  SP := SP-1
                   END;

          26 (*CHK*):IF(STORE[SP].VI<STORE[Q-1].VI)OR (STORE[SP].VI>STORE[Q].VI)THEN
                     ERRORI(' VALUE OUT OF RANGE      ');

          27 (*EOF*):BEGIN  I := STORE[SP].VI;
                      IF I=INPUTADR THEN
                      BEGIN STORE[SP].STYPE := BOOL; STORE[SP].VB := EOF(INPUT);
                      END ELSE ERRORI(' CODE IN ERROR           ')
                   END;

          28 (*ADI*):BEGIN  SP := SP-1;
                      STORE[SP].VI := STORE[SP].VI + STORE[SP+1].VI
                   END;

          29 (*ADR*):BEGIN  SP := SP-1;
                      STORE[SP].VR := STORE[SP].VR + STORE[SP+1].VR
                   END;

          30 (*SBI*):BEGIN SP := SP-1;
                      STORE[SP].VI := STORE[SP].VI - STORE[SP+1].VI
                   END;

          31 (*SBR*):BEGIN  SP := SP-1;
                      STORE[SP].VR := STORE[SP].VR - STORE[SP+1].VR
                   END;

       END (*CASE OP*)
END; (*EX1*)

PROCEDURE EX2;
   var i: integer; s: set of 0..58;

BEGIN  CASE OP OF

          32 (*SGS*):BEGIN  s := [STORE[SP].VI];
                      STORE[SP].STYPE := SETT; STORE[SP].VS := s
                   END;

          33 (*FLT*):BEGIN  i := STORE[SP].VI;
                      STORE[SP].STYPE := REEL;  STORE[SP].VR := i
                   END;

          34 (*FLO*):BEGIN  i := STORE[SP-1].VI;
                      STORE[SP-1].STYPE := REEL; STORE[SP-1].VR := i
                   END;

          35 (*TRC*):BEGIN i := TRUNC(STORE[SP].VR);
                      STORE[SP].STYPE := INT; STORE[SP].VI := i
                   END;

          36 (*NGI*):STORE[SP].VI := -STORE[SP].VI;

          37 (*NGR*):STORE[SP].VR := -STORE[SP].VR;

          38 (*SQI*):STORE[SP].VI := SQR(STORE[SP].VI);

          39 (*SQR*):STORE[SP].VR := SQR(STORE[SP].VR);

          40 (*ABI*):STORE[SP].VI := ABS(STORE[SP].VI);

          41 (*ABR*):STORE[SP].VR := ABS(STORE[SP].VR);

          42 (*NOT*):STORE[SP].VB := NOT STORE[SP].VB;

          43 (*AND*):BEGIN  SP := SP-1;
                      STORE[SP].VB := STORE[SP].VB AND STORE[SP+1].VB
                   END;

          44 (*IOR*):BEGIN  SP := SP-1;
                      STORE[SP].VB := STORE[SP].VB OR STORE[SP+1].VB
                   END;

          45 (*DIF*):BEGIN  SP := SP-1;
                      STORE[SP].VS := STORE[SP].VS - STORE[SP+1].VS
                   END;

          46 (*INT*):BEGIN  SP := SP-1;
                      STORE[SP].VS := STORE[SP].VS *  STORE[SP+1].VS
                   END;

          47 (*UNI*):BEGIN  SP := SP-1;
                      STORE[SP].VS := STORE[SP].VS +  STORE[SP+1].VS
                   END;

       END (*CASE OP*)
END; (*EX2*)

PROCEDURE EX3;
   VAR I,I1,I2 :ADDRESS; b: boolean;
BEGIN  CASE OP OF

          48 (*INN*):BEGIN  SP := SP-1;
                      b := STORE[SP].VI IN STORE[SP+1].VS;
                      STORE[SP].STYPE := BOOL; STORE[SP].VB := b
                   END;

          49 (*MOD*):BEGIN  SP := SP-1;
                      STORE[SP].VI := STORE[SP].VI MOD STORE[SP+1].VI
                   END;

          50 (*ODD*):BEGIN  b := ODD(STORE[SP].VI);
                      STORE[SP].STYPE := BOOL; STORE[SP].VB := b
                   END;

          51 (*MPI*):BEGIN  SP := SP-1;
                      STORE[SP].VI := STORE[SP].VI * STORE[SP+1].VI

         END;

          52 (*MPR*):BEGIN  SP := SP-1;
                      STORE[SP].VR := STORE[SP].VR * STORE[SP+1].VR
                   END;

          53 (*DVI*):BEGIN  SP := SP-1;
                      STORE[SP].VI := STORE[SP].VI DIV STORE[SP+1].VI
                   END;

          54 (*DVR*):BEGIN  SP := SP-1;
                      STORE[SP].VR := STORE[SP].VR/STORE[SP+1].VR
                   END;

          55 (*MOV*): BEGIN I1 := STORE[SP-1].VA; I2 := STORE[SP].VA; SP := SP-2;
                       FOR I := 0 TO Q-1 DO STORE[I1+I] := STORE[I2+I]
                      (* Q IS A NUMBER OF STORAGE UNITS *)
                    END;

          56 (*LCA*):BEGIN SP := SP + 1;
                      IF SP >= NP THEN ERRORI(' STORE OVERFLOW          ');
                      STORE[SP].STYPE := ADR; STORE[SP].VA := Q
                   END;

          57 (*DEC*):STORE[SP].VI := STORE[SP].VI - Q;

          58 (*STP*):INTERPRETING := FALSE;

       END (*CASE OP*)
END; (*EX3*)

(*------------------------------------------------------------------------*)

BEGIN   (*  M A I N  *)
   { REWRITE(PRR); }
   LOAD;  (* ASSEMBLES AND STORES CODE *)
   WRITELN(OUTPUT); (*FOR TESTING*)
   PC := 0;  SP := -1;  MP := 0;  NP := MAXSTK+1;
   STORE[INPUTADR].STYPE := INT;  STORE[INPUTADR].VI := ORD(INPUT^);
   STORE[PRDADR].STYPE:= INT; STORE[PRDADR].VI:= ORD(PRD^);
   STORE[OUTPUTADR].STYPE:= UNDEF;
   INTERPRETING := TRUE;
   WHILE INTERPRETING DO
   BEGIN  (*FETCH*)
      WITH CODE[PC DIV 2] DO
         IF ODD(PC) THEN
         BEGIN  OP := OP2;  P := P2;  Q := Q2
         END  ELSE
         BEGIN  OP := OP1;  P := P1;  Q := Q1
         END;
      PC := PC+1;

      (*EXECUTE*)
      CASE OP DIV 16 OF
       0:  EX0;
       1:  EX1;
       2:  EX2;
       3:  EX3
      END
   END; (*WHILE INTERPRETING*)

1 :
END.
