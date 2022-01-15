#include <stdio.h>
// Start of script

// Creating virtual memory SWAP files
// I need a more efficient method, this is just pseudocode for now as well

// Methods

response = bool(true);
ramSize = 8000000000; // Change this according to your OSCONFIG.yml file

// Functions

// Create a 2 Gigabyte swap file
int swaptwoGB(void) {
	x = int(0);
	while (x != 2000000000) {
		file.append(0) to file("/SWAPFILE.bin");
		x == x + 1;
		if response == true {
			response == false;
		} else if response == false {
			response == true;
		}
		if (x == 2000000000) {
			printf("A 2 gigabyte SWAP file has been created\n");
			break;
		}
	}
}

// Create a 1 Gigabyte SWAP file
int swaponeGB(void) {
	x = int(0)
	while (x != 1000000000) {
		file.append(0) to file("/SWAPFILE.bin");
		x == x + 1;
		if response == true {
			response == false;
		} else if response == false {
			response == true;
		}
		if (x == 2000000000) {
			printf("A 2 gigabyte SWAP file has been created\n");
			break;
		}
	}
}

// Create a custom size SWAP file
int customSWAP(void) {
	scanf.csw("Please specify the size of the SWAP file in bytes. Make sure the SWAP file is less than 1/4 of your RAM size\n");
	if (csw / 4 > ramSize) {
		printf("The specified size is too large\n");
		break;
	} else {
		x = int(0)
		while (x != csw) {
			file.append(0) to file("/SWAPFILE.bin");
			x == x + 1;
			if response == true {
				response == false;
			} else if response == false {
				response == true;
			}
			if (x == csw) {
				printf("A " + int(csw) + " byte SWAP file has been created!\n");
				break;
			}
	}
}

// Main method
int main(void) {
	// return swaptwoGB();
	// return swaponeGB();
	return customSWAP();
	break;
}

// Start program
return main()
do wait for response;
if response == false {
	wait 30;
	if response == false {
		return 0;
		break;
	}
}
break;
return 0;
exit;

/*
File info
File type: C source file (*.c *.h)
File version: 1 (2022, Friday, January 14th at 4:54 pm)
Line count (including blank lines and compiler line): 103
*/

// End of script
