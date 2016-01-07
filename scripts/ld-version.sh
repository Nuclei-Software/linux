#!/usr/bin/awk -f
# extract linker version number from stdin and turn into single number
	{
	gsub(".*[)]", "");
	split($1,a, "[-.]");
	printf "%i%04i\n", a[1]*10000 + a[2]*100 + a[3], (a[4]*100 + a[5])%10000;
	exit
	}
