{ Start of script }
{ The original MacPaint that this project is based off of uses 2 offscreen data buffers to prevent flickering when stretching shapes. I plan to do the same here }

{ Data buffer A }

function dbufferA;
begin
  asm 10011001 01011010 00110010 11001100 { Dummy values as a placeholder, does not work }
  write('Data buffer A has failed. Please reload the program')
end.

{ Data buffer B }

function dbufferB;
begin
  asm 10011001 01011010 00110010 11001100 { Dummy values as a placeholder, does not work }
  write('Data buffer B has failed. Please reload the program')
end.

{ Main process }

program wacPaintDBUFF;
begin
  write('Initializing data buffers')
  dbufferA();
  dbufferB();
  write('Initialized data buffers successfully');
  Exit(Value*3);
end;

{ Learning how to return in Pascal }
{ Source: https://stackoverflow.com/questions/9641652/returning-a-value-in-pascal#9641727 }
{ Because this doesn't work: return foo() }

{ Commented out to keep program stable

function Foo (Value : integer) : Integer;
begin      
  Exit(Value*2);
  DoSomethingElse();   // This will never execute
end;

}

{ File info }
{ File type: Pascal source file (*.pas) }
{ File version: 1 (Friday, 2021 September 24th at 5:38 pm) }
{ Line count (including blank lines and compiler line): 50 }
{ End of script }
