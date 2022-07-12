; Start of script
; Sampled from: https://github.com/dfellis/llvm-hello-world/blob/master/helloWorld.ll
; Declare the string constant as a global constant.
@.str = private unnamed_addr constant [13 x i8] c"Project language file 54\nFor: WacOS\nAbout:\nI decided to make LLVM the 54th project language file for this project (WacOS) as LLVM is used in some newer source code on this project. It is getting its own project language file, starting here.\0A\00"

; External declaration of the puts function
declare i32 @puts(i8* nocapture) nounwind

; Definition of main function
define i32 @main() { ; i32()*
    ; Convert [13 x i8]* to i8  *...
    %cast210 = getelementptr [13 x i8],[13 x i8]* @.str, i64 0, i64 0

    ; Call puts function to write out the string to stdout.
    call i32 @puts(i8* %cast210)
    ret i32 0
}

; Named metadata
!0 = !{i32 42, null, !"string"}
!foo = !{!0}

; File info
; File type: LLVM source file (*.ll)
; File version: 1 (2022, Monday, July 11th at 6:42 pm PST)
; Line count (including blank lines and compiler line): 29

; End of script
