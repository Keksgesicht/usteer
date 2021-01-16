

struct BeaconData{
    int opClass;
    void* BSSID;
    void* Adress;
    void* RCPI;
};
int getChannelFromFreq(int freq) {
	int channel = 0;
	
	/* see 802.11-2007 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000) /* DMG band lower limit */
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
	
}
int getOPClassFromChannel(int channel){
	if (channel >= 36 ||
		channel <= 48 ){

		return 115; // 1 in nicht global
	}
	else if(channel >= 52 ||
			channel <= 64 ){

		return 118; // 2
	}
	else if(
			channel >= 100 ||
			channel <= 140 ){

		return 121; // 3
	}
	else if(channel >= 1  ||
			channel <= 13 ){

		return 81; // 4
	}
	else{
		return 0; // z.B channel 14 nicht in dokument
	}
	
}