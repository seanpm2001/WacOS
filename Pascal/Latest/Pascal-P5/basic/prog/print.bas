100 rem
110 rem File printer
120 rem
130 input "File to print: "; file$
140 open file$ for input as #10
150 while not eof(#10): input #10, data$: print data$: wend
160 close #10
170 end
