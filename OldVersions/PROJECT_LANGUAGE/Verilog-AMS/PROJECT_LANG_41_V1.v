// Start of script

`include "constants.vams"
`include "disciplines.vams"

// Project language file 41
// For: WacOS
// About:
// I decided to make Verilog-AMS the forty-first project language file for this project (Seanpm2001/WacOS) as Verilog-AMS is a good language for machine instructions at direct programmable hardware level (such as FPGA devices) it can be used for some portions of this project. It is being included for software diversity reasons, and will be used for some portions of the project. It is getting its own project language file, starting here.

// Simple DAC model

module dac_simple(aout, clk, din, vref);
	
	// Parameters
	parameter integer bits = 4 from [1:24];
	parameter integer td = 1n from[0:inf);  // Processing delay of the DAC
	
	// Define input/output
	input clk, vref;
	input [bits-1:0] din;
	output aout;
		
	//Define port types
	logic clk;
	logic [bits-1:0] din;
	electrical  aout, vref;
	
	// Internal variables
	real aout_new, ref;
	integer i;
	
	// Change signal in the analog part
	analog begin
		@(posedge clk) begin // Change output only for rising clock edge	
			
			aout_new = 0;
			ref = V(vref);
			
			for(i=0; i<bits; i=i+1) begin
				ref = ref/2;
				aout_new = aout_new + ref * din[i];
			end
		end	
		V(aout) <+ transition(aout_new, td, 5n); // Get a smoother transition when output level changes
	end
endmodule

/* File info
* File type: Verilog AMS source file (*.v *.vh)
* File version: 1 (2021, Friday, December 24th at 4:37 pm)
* Line count (including blank lines and compiler line): 55
*/
// End of script
