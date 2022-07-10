(*$l-*)
(******************************************************************************)
(*                                                                            *)
(* Sieve benchmark program reproduced verbatum from                           *)
(* page 285 of BYTE magazine's January 1983 edition,                          *)
(* with the exception of the 'output' file specification                      *)
(* inserted in the program header. This is required for                       *)
(* IP Pascal.                                                                 *)
(*                                                                            *)
(* The Result figures are as follows:                                         *)
(*                                                                            *)
(* Z80 4mhz 100 iterations IP Pascal                                          *)
(*                                                                            *)
(*      Compiled bytes:                                                       *)
(*      Memory used: 2,242 bytes                                              *)
(*      Compile plus load: 125 seconds                                        *)
(*      Execution time: 21.0 seconds, 0.21 sec/iter                           *)
(*                                                                            *)
(* 80486DX2 66mhz 10000 iterations SVS compiler:                              *)
(*                                                                            *)
(*      Size: 90,708 (includes DOS extender)                                  *)
(*      Execution time: 77 sec. 0.0077 sec/iter                               *)
(*                                                                            *)
(* 80486Dx2 66mhz 10000 iterations Prospero OS/2 2.x                          *)
(* compiler:                                                                  *)
(*                                                                            *)
(*      Size: 16,404                                                          *)
(*      Execution time: 163 sec. 0.0163 sec/iter                              *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 100000 iterations Delphi 5.0                               *)
(*                                                                            *)
(*      Size:                                                                 *)
(*      Execution time: 11 sec., 0.00011 sec/iter                             *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 10000 iterations IP Pascal, check                          *)
(* encoder:                                                                   *)
(*                                                                            *)
(*      Size: 134,656                                                         *)
(*      Execution time: 16 sec., 0.0016 sec/iter                              *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 100000 iterations FPC Pascal                               *)
(*                                                                            *)
(*      Size:                                                                 *)
(*      Execution time: 30 sec., 0.0003 sec/iter                              *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 100 iterations Irie Pascal                                 *)
(*                                                                            *)
(*      Size:                                                                 *)
(*      Execution time: 9 sec., 0.09 sec/iter                                 *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 100000 iterations IP Pascal, full encoder, no optos        *)
(*                                                                            *)
(*      Size:                                                                 *)
(*      Execution time: 61 sec., 0.00061 sec/iter                             *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 1000000 iterations 80386 Assembly                          *)
(*                                                                            *)
(*      Size:                                                                 *)
(*      Execution time: 112 sec., 0.000112 sec/iter                           *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 100000 iterations GPC                                      *)
(*                                                                            *)
(*      Size:                                                                 *)
(*      Execution time: 19 sec., 0.00019 sec/iter                             *)
(*                                                                            *)
(* AMD CPU 1.8 GHZ 100000 iterations GCC                                      *)
(*                                                                            *)
(*      Size:                                                                 *)
(*      Execution time: 11 sec., 0.00011 sec/iter                             *)
(*                                                                            *)
(******************************************************************************)

program sieve(output);

const

  size = 8190;

var

  flags : array [0..size] of boolean;
  i, prime, k, count, iter : integer;

begin

   writeln('10 iterations');
   for iter := 1 to 10 do begin { do program 10000 times }

      count := 0; { prime counter }
      for i := 0 to size do flags[i] := true; { set flags all true }
      for i := 0 to size do
         if flags[i] then begin { found a prime }

            prime := i+i+3; { twice the index + 3 }
            { writeln(prime); }
            k := i + prime; { first multiple to kill }
            while k <= size do begin
               flags[k] := false; { zero a non-prime }
               k := k + prime { next multiple }
            end;
            count := count + 1 { primes found }

         end;

   end;
   writeln(count, ' primes') { primes found in 10th pass }

end.
