{******************************************************************************

32 bit Machine Parameter block (MPB)

******************************************************************************}

      intsize     =        4;   { size of integer }
      intal       =        4;   { alignment of integer }
      intdig      =        11;  { number of decimal digits in integer }
      inthex      =        8;   { number of hex digits of integer }
      realsize    =        8;   { size of real }
      realal      =        4;   { alignment of real }
      charsize    =        1;   { size of char }
      charal      =        1;   { alignment of char }
      charmax     =        1;
      boolsize    =        1;   { size of boolean }
      boolal      =        1;   { alignment of boolean }
      ptrsize     =        4;   { size of pointer }
      adrsize     =        4;   { size of address }
      adral       =        4;   { alignment of address }
      setsize     =       32;   { size of set }
      setal       =        1;   { alignment of set }
      filesize    =        1;   { required runtime space for file (lfn) }
      fileidsize  =        1;   { size of the lfn only }
      fileal      =        1;   { alignment of file lfn }
      stackal     =        4;   { alignment of stack }
      stackelsize =        4;   { stack element size }
      maxsize     =       32;   { this is the largest type that can be on the
                                  stack }
      { Heap alignment should be either the natural word alignment of the
        machine, or the largest object needing alignment that will be allocated.
        It can also be used to enforce minimum block allocation policy. }
      heapal      =        4;   { alignment for each heap arena }
      gbsal       =        4;   { globals area alignment }
      sethigh     =      255;   { Sets are 256 values }
      setlow      =        0;
      ordmaxchar  =      255;   { Characters are 8 bit ISO/IEC 8859-1 }
      ordminchar  =        0;
      maxresult   = realsize;   { maximum size of function result }
      marksize    =       28;   { maxresult+7*ptrsize }
      ujplen      =       5;    { length of ujp instruction (used for case
                                  jumps) }
      maxexp      =      308;   { maximum exponent of real }
      { Value of nil is 1 because this allows checks for pointers that were
        initialized, which would be zero (since we clear all space to zero).
        In the new unified code/data space scheme, 0 and 1 are always invalid
        addresses, since the startup code is at least that long. }
      nilval      =        1;   { value of 'nil' }

      { beginning of code, offset by program preamble:

        2:    mst
        6:    cup
        1:    stp

      }
      begincode   =        9;

      { Mark element offsets

        Mark format is:

        -4: Static link.
        -8: Dynamic link.
        -12: Saved EP from previous frame.
        -16: Stack bottom after locals allocate. Used for interprocdural gotos.
        -20: EP from current frame. Used for interprocedural gotos.
        -24: With stack base count. Used for interprocedural gotos.
        -28: Return address

      }
      marksl      =        -4; { static link }
      markdl      =        -8; { dynamic link }
      markep      =        -12; { (old) maximum frame size }
      marksb      =        -16; { stack bottom }
      market      =        -20; { current ep }
      markwb      =        -24; { with stack base count }
      markra      =        -28; { return address }
