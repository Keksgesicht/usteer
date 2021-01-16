

struct BeaconData{
    int opClass;
    void* BSSID;
    void* Adress;
    void* BSSID
};
int getOpClassFromFreq(int freq) {
	int channel = 0;
	
	/* see 802.11-2007 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		channel = 14;
	else if (freq < 2484)
		channel = (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		channel = (freq - 4000) / 5;
	else if (freq <= 45000) /* DMG band lower limit */
		channel = (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		channel = (freq - 56160) / 2160;
	else
		channel = 0;
	

	if (channel == 36 |
		channel == 40 |
		channel == 44 |
		channel == 48 ){

		return 115; // 1 in nicht global
	}
	else if(channel == 52 |
			channel == 56 |
			channel == 60 |
			channel == 64 ){

		return 118; // 2
	}
	else if(
			channel == 100 |
			channel == 104 |
			channel == 108 |
			channel == 112 |
			channel == 116 |
			channel == 120 |
			channel == 124 |
			channel == 128 |
			channel == 132 |
			channel == 136 |
			channel == 140 ){

		return 121; // 3
	}
	else if(channel == 1  |
			channel == 2  |
			channel == 3  |
			channel == 4  |
			channel == 5  |
			channel == 6  |
			channel == 7  |
			channel == 8  |
			channel == 9  |
			channel == 10 |
			channel == 11 |
			channel == 12 |
			channel == 13 ){

		return 81; // 4
	}
	else{
		return 0; // z.B channel 14 nicht in dokument
	}
	
}