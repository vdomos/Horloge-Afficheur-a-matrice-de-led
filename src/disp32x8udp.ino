/*
disp32x8udp
Afficheur SURE 32x8 piloté par un Arduino + Ethernet Schield
*/


/*
$ ino build
$ ino upload -p /dev//USB_BUB  && picocom -b 115200 -d 8 -f n -p n /dev/USB_BUB 


1/10/2014
- Correction fonction heure hiver.
- Correction affichage heure sans '0' en premier chiffre.
- Ajout serveur ntp sur reseau local en premier et serveur ntp internet en second.


20/7/2014
Ajout cacul heure été/hiver:
	        if ( (month >  3  &&  month < 10) ||  	// Cacule heure été.
		   ( month ==  3  &&  day > 24  &&  (day - weekday) > 24  ) ||
		   ( month == 10  &&  (day - weekday) < 25  ) ) 	timeZoneOffset = 7200 ;
		 else timeZoneOffset = 3600 ;
weekday = dim à sam (0..6) 

Tableau de la dernière semaine de Mars avec les jours de la semaine (Dimanche à Samedi  = 0..6)
La diagonale des '0' (dimanche) marque le changement d'heure:
		       Mars
		25 26 27 28 29 30 31
Hiver		 1  2  3  4  5  6  0 <=		Eté
		 2  3  4  5  6  0  1
		 3  4  5  6  0  1  2
		 4  5  6  0  1  2  3
		 5  6  0  1  2  3  4
		 6  0  1  2  3  4  5
	      => 0  1  2  3  4  5  6
	      
		       Octobre
		25 26 27 28 29 30 31
Eté		 1  2  3  4  5  6  0 <=		Hiver
		 2  3  4  5  6  0  1
		 3  4  5  6  0  1  2
		 4  5  6  0  1  2  3
		 5  6  0  1  2  3  4
		 6  0  1  2  3  4  5
	      => 0  1  2  3  4  5  6

Idem en octobre en inversant le coté de la diagonale été/hiver.


19/7/2014
Afficheur piloté par carte Ethernet Pro
- Beep toutes les heures
- Beep lors d'un affichage de message défilant.
- Gère la luminosité afficheur avec une LDR.


16/7/2014
Ajout connexion serveur ntp pour mise à l'heure horloge locale au boot, à 11:59:50 et 23:59:50


12/7/2014
Arduino + Ethernet Schield ou  Arduino Pro
Fonctionne comme un afficheur simple commandé par ethernet UDP
N'affiche uniquement ce qu'il recoit sous ce protocol:

En fin de texte '@' aligné à gauche		"Texte@"
En fin de texte '#' centré			"Texte#"
En fin de texte '*' aligné à droite		"Texte*"
En fin de texte '$' beep n milli-secondes	"300$"
En fin de texte '%' Défilement texte		"TexteLong%"
En fin de texte '!' Maj horloge locale\n	"140716095900!"

Arduino recoit un message UDP et retourne un ack:
---------------------------------------------------------------------
$ picocom -b 115200 -d 8 -f n -p n /dev/arduino  
picocom v1.7
...
Terminal ready

Init. Arduino Disp32x8 UDP V20140712 ...
Arduino is at 192.168.1.125
Info. protocole en réception:
'Texte@': Texte aligné à gauche
'Texte#': Texte centré
'Texte*': Texte aligné à droite
'300$'  : Beep 300ms
'TexteLong%'  : Défilement texte
>
Fin init. Arduino.

Received packet of size 7 from 192.168.1.4, port 54851
Contents: 18:15#
ExeCcmd: 18:15#
Send response Ack

Received packet of size 39 from 192.168.1.4, port 54851
Contents: Attention porte garage restee ouverte%
ExeCcmd: Attention porte garage restee ouverte%
Send response Ack

Received packet of size 9 from 192.168.1.4, port 54851
Contents: -17.8°*
ExeCcmd: -17.8&*
Send response Ack


--------------------------------------------------
Programme 'netcat' pour envoyé des message en UDP:
$ ncat 192.168.1.125 8888 -u -v
Ncat: Version 6.46 ( http://nmap.org/ncat )
Ncat: Connected to 192.168.1.125:8888.
18:15#
Ack
Attention porte garage restee ouverte%
Ack
-17.8°*
Ack

---------------------------------
Script bash pour afficher l'heure:
$ echo "300$"  | nc -u -w1 192.168.1.125 8888

$ while sleep 60
do
echo  "$(date +%H:%M#)" | nc -u -w1 192.168.1.125 8888
done

*/




#include <stdlib.h>
#include <SPI.h>             	// Nécessaire depuis la version Arduino 0018
#include <Ethernet.h>  
#include <EthernetUdp.h>	// UDP library from: bjoern@cs.stanford.edu 12/30/2008
#include <font_5x7.h>
#include <HT1632.h>
#include <Time.h>  


// Cablage I/O Ethernet Schield
// PINs 10,11,12,13 et 4 (SD) 

// Cablage I/O afficheur SURE
#define BUZZERPIN    2		// Pin => Buzzer => Gnd
#define BUZZERGND    3		// Pin => Gnd Buzzer
#define LDR	    A0		// Analog 0 => LDR
#define LDRGND	    A1		// Analog 1 pour mettre à Gnd la LDR (configurée en sortie à gnd)
#define HT1632_CS1   5		// Arduino Digital pin to Pin 3 HE-10 SURE Display (CS1)  	afficheur SURE 3208
#define HT1632_WR    6		// Arduino Digital pin to Pin 5 HE-10 SURE Display (WR)   	afficheur SURE 3208	bleue
#define HT1632_DATA  7		// Arduino Digital pin to Pin 7 HE-10 SURE Display (DATA)	afficheur SURE 3208	vert
				// Arduino Gnd to Pin 11 HE-10 SURE Display (Gnd)		afficheur SURE 3208	jaune
				// Arduino +5V to Pin 12 HE-10 SURE Display (+5V)		afficheur SURE 3208

// Variables
uint8_t luminosity = 0 ;
char buffer[255] ; 		// Variable port série
int  nbpixels ;
int  xpos ;
unsigned long lumtimestamp = 0 ;


//  ---------------------------------------------------------------------------
byte mac[] = { 0x90, 0xA2, 0xDA, 0xXX, 0xXX, 0xXX };	// arduino
IPAddress ip(192,168,1,125);
IPAddress broadcast(192, 168, 1, 255);
IPAddress dns1(192, 168, 1, 254);
IPAddress gateway(192, 168, 1, 254 );			// the router's gateway address:
IPAddress subnet( 255, 255, 255, 0 );			// the subnet:
unsigned int localPort = 8888;      			// local port to listen on

//IPAddress timeServer1(5,135,166,37);      		// ntp.deuza.net OK
//IPAddress timeServer2(129, 6, 15, 28);  		// time.nist.gov OK
IPAddress timeServer3(212, 83, 133, 52) ;		// Serveurs ntp du pool fr: 0.fr.pool.ntp.org	core-2.zeroloop.net (212, 83, 133, 52 ok), ntp-2.arkena.net (95.81.173.74) A tester
IPAddress timeServer4(192, 168, 1, 4) ;			// ntp local
long timeZoneOffset  ;	 				// Offset in seconds to your local time, par défaut heure d'été.
unsigned long timestamp ;
int secondes_courantes = 60 ;
char time_str[32];

// buffers for receiving and sending data
const int PACKET_SIZE= 48; 				// NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[PACKET_SIZE]; 			//buffer to hold incoming and outgoing packets 

char packetBuffer2[64];		 			//buffer to hold incoming packet,
char  ReplyBuffer[6] ;       				// Buffer of string to send back

// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

/*------------------------------------------------------------------------------*/
/* Setup									*/
/*------------------------------------------------------------------------------*/
void setup() 
{
	delay(1000) ;
        Serial.begin(115200);
	Serial.println(F("Init. Arduino Disp32x8 UDP V20140712 ..."));
	
	pinMode(LDRGND, OUTPUT) ;
	digitalWrite(LDRGND, LOW) ;		// Utilise Analog 1 pour mettre à Gnd la LDR (configurée en sortie à gnd)
	digitalWrite(LDR, HIGH);  		// Active R pullup sur Anlog 0 connectée à la LDR (pont diviseur) 

	pinMode(BUZZERGND, OUTPUT) ;
	digitalWrite(BUZZERGND, LOW) ;		// Utilise D3 pour mettre à Gnd le Buzzer (configurée en sortie à gnd)
	pinMode(BUZZERPIN, OUTPUT) ;
	beep(30) ;

	// Setup and begin the matrix
	HT1632.begin(HT1632_CS1, HT1632_WR, HT1632_DATA);
	HT1632.setBrightness(8) ;

	aff_32x8_defilltxt("Disp32x8 UDP V20140712") ;
	
	Serial.println(F("\nInfo. protocole en réception:")) ;
	Serial.println(F("'Texte@': Texte aligné à gauche")) ;
	Serial.println(F("'Texte#': Texte centré")) ;
	Serial.println(F("'Texte*': Texte aligné à droite")) ;
	Serial.println(F("'300$'  : Beep 300ms")) ;
	Serial.println(F("'TexteLong%'  : Défilement texte")) ;
	Serial.println(F("'140716095900!'  : Maj horloge locale\n")) ;

	Serial.println(F("Init. ethernet ..."));
//	Ethernet.begin(mac, ip, dns1, gateway, subnet);
	aff_32x8_txt(4, "DHCP") ;
	while (Ethernet.begin(mac) == 0) 		     // Mode dhcp pour passerelle, nécessaire pour NTP !
	{ 
		Serial.println(F("Failed to configure Ethernet using DHCP"));
		aff_32x8_txt(8, "LAN") ;
		delay(30000) ;
		aff_32x8_txt(4, "DHCP") ;		
	}
	Serial.print(F("Arduino is at "));
	Serial.println(Ethernet.localIP());

	Udp.begin(localPort);
	
	aff_32x8_txt(8, "NTP") ;
	majTimeNTP() ;
	aff_32x8_txt(4, "PRET") ;
	Serial.println(F("Fin init. Arduino.\n"));
}


/*------------------------------------------------------------------------------*/
/* Maj horloge locale avec timestamp NTP
/*------------------------------------------------------------------------------*/
void majTimeNTP()
{
	timestamp = getNTPEpoch(timeServer4) ;
	if (! timestamp) timestamp = getNTPEpoch(timeServer3) ;
	if (timestamp)
	{
		Serial.print(F("Maj local time from NTP: ")); 
		setTime(timestamp) ;				// Init time en UTC, test ci-dessous fait sur date en UTC !
	        if ( (month() >  3  &&  month() < 10) ||  	// Cacule heure été.
		   ( month() ==  3  &&  day() > 24  &&  ( day() - weekday() ) > 24) ||
		   ( month() == 10  &&  (day() - weekday()) < 25) )   timeZoneOffset = 7200 ;
		else timeZoneOffset = 3600 ;			// Init time locale.
		setTime(timestamp + timeZoneOffset) ;
		adjustTime(1); 					// Décalage de 3s de retard avec rpi.
		sprintf(time_str, "%d/%d/%d, %d:%02d:%02d\n", day(), month(), year(), hour(), minute(), second() ) ;  
		Serial.println(time_str);
	}
	else Serial.println(F("Pas de serveur ntp !"));
}


/*------------------------------------------------------------------------------*/
/* Return timestamp Unix from NTP Server
/*------------------------------------------------------------------------------*/
unsigned long getNTPEpoch(IPAddress &addr)
{
	Serial.print(F("Send NTP request packet to ")) ;
	Serial.println(addr) ;
	sendNTPpacket(addr); // send an NTP packet to a time server

	// wait to see if a reply is available
	delay(1000);  
	Serial.println(F("Wait NTP packet ...")) ;
	if ( Udp.parsePacket() ) 
	{  
		Serial.println(F("Packet received"));
		// We've received a packet, read the data from it
		Udp.read(packetBuffer,PACKET_SIZE);  // read the packet into the buffer

		//the timestamp starts at byte 40 of the received packet and is four bytes,
		// or two words, long. First, esxtract the two words:

		unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
		unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);  
		// combine the four bytes (two words) into a long integer
		// this is NTP time (seconds since Jan 1 1900):
		unsigned long secsSince1900 = highWord << 16 | lowWord;  
		Serial.print("Seconds since Jan 1 1900 = " );
		Serial.println(secsSince1900);               

		// now convert NTP time into everyday time:
		Serial.print("Unix time = ");
		// Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
		const unsigned long seventyYears = 2208988800UL;     
		// subtract seventy years:
		unsigned long epoch = secsSince1900 - seventyYears;  
		// print Unix time:
		Serial.println(epoch);                               

		// print the hour, minute and second:
		Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
		Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
		Serial.print(':');  
		if ( ((epoch % 3600) / 60) < 10 ) {
			// In the first 10 minutes of each hour, we'll want a leading '0'
			Serial.print('0');
		}
		Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
		Serial.print(':'); 
		if ( (epoch % 60) < 10 ) {
			// In the first 10 seconds of each minute, we'll want a leading '0'
			Serial.print('0');
		}
		Serial.println(epoch %60); // print the second
		return epoch ;
	}
	return 0 ;
}


/*------------------------------------------------------------------------------*/
// Send an NTP request to the time server at the given address 
/*------------------------------------------------------------------------------*/
unsigned long sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, PACKET_SIZE); 
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49; 
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp: 		   
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer,PACKET_SIZE);
  Udp.endPacket(); 
}

/*------------------------------------------------------------------------------*/
/* Fonction réception commande sur Ethernet UDP, doit se terminer par '\n'	*/
/*------------------------------------------------------------------------------*/
int GetRxCmd(char *buff)
{
	int packetSize = Udp.parsePacket();
	// if there's data available, read a packet
	if (packetSize) 
	{
		Serial.print(F("Received packet of size "));
		Serial.print(packetSize);
		Serial.print(F(" from "));
		Serial.print(Udp.remoteIP());
		Serial.print(F(", port "));
		Serial.println(Udp.remotePort());

		// read the packet into packetBufffer
		Udp.read(buff, 255);
		buff[packetSize - 1] = '\0' ;		// Supprime '/n'.
		Serial.print(F("Contents: "));
		Serial.println(buff);
	}
	return packetSize ;
}


/*------------------------------------------------------------------------------*/
/* Envoie message ACK/NACK par Ethernet UDP 						*/
/*------------------------------------------------------------------------------*/
void SendResponse(char ReplyBuffer[])
{
	// send a reply, to the IP address and port that sent us the packet we received
	Serial.print(F("Send response "));
	Serial.println(ReplyBuffer);
	Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
	Udp.write(ReplyBuffer);
	Udp.endPacket();
}


/*------------------------------------------------------------------------------*/
/* Excute commande reçu								*/
/*------------------------------------------------------------------------------*/
int ExecDisplayCmd(char buff[])
{
	int lastcharpos ;
	char champs[3] ;
	int heures, minutes, secondes, jour, mois, annee ;

	suppAccentUTF8Str(buff)	;							// Remplace tout code caractères  UTF8 accentués et '°'
	lastcharpos = strlen(buff) - 1 ;
	Serial.print(F("ExecDisplayCmd: "));
	Serial.println(buff);
	
	strcpy(ReplyBuffer, "Ack\n") ;
	switch (buff[lastcharpos])
	{
		case '@':    // Texte aligné à gauche
			xpos = 1 ;
			buff[lastcharpos] = '\0' ;
			aff_32x8_txt(xpos, buff) ;
			SendResponse(ReplyBuffer) ;
		break;
		case '#':    // Texte centré
			buff[lastcharpos] = '\0' ;
			nbpixels = HT1632.getTextWidth(buff, FONT_5X7_WIDTH, FONT_5X7_HEIGHT) ; 
			xpos = (32 - nbpixels) >> 1 ;    						// 2 x xpos + nbpixels = 32
			aff_32x8_txt(xpos, buff) ;
			SendResponse(ReplyBuffer) ;
		break;
		case '*':    // Texte aligné à droite
			buff[lastcharpos] = '\0' ;
			nbpixels = HT1632.getTextWidth(buff, FONT_5X7_WIDTH, FONT_5X7_HEIGHT) ; 
			xpos = 32 - nbpixels ;
			aff_32x8_txt(xpos, buff) ;
			SendResponse(ReplyBuffer) ;
		break;
		case '%':    // Défile Text
			buff[lastcharpos] = '\0' ;
			beep(100) ; delay(200) ; beep(100) ;
			aff_32x8_defilltxt(buff) ;
			SendResponse(ReplyBuffer) ;
		break;
		case '$':    // Beep
			buff[lastcharpos] = '\0' ;
			beep(atoi(buff)) ;
			SendResponse(ReplyBuffer) ;
		break;
		case '!':    // Commande mise à l'heure avec chaine 'YYMMddhhmmss!'.
			strncpy(champs, buff + 0, 2) ; champs[2]='\0' ;
	    		annee = atoi(champs);
	    		strncpy(champs, buff + 2, 2) ; champs[2]='\0' ;
	    		mois = atoi(champs);
	    		strncpy(champs, buff + 4, 2) ; champs[2]='\0' ;
	    		jour = atoi(champs);
	    		strncpy(champs, buff + 6, 2) ; champs[2]='\0' ;
	    		heures = atoi(champs);
	    		strncpy(champs, buff + 8, 2) ; champs[2]='\0' ;
	    		minutes = atoi(champs);
	    		strncpy(champs, buff + 10, 2) ; champs[2]='\0' ;
	    		secondes = atoi(champs);
	    		setTime(heures, minutes, secondes, jour, mois, annee) ;
			SendResponse(ReplyBuffer) ;
		break;
		default:
			Serial.print("Commande inconnue : ") ;
			Serial.println(buff[lastcharpos], HEX) ;
			strcpy(ReplyBuffer, "Nack\n") ;
			SendResponse(ReplyBuffer) ;
			return 0 ;
	}
	//Serial.println(buff) ;

	return 1 ;
}


/*------------------------------------------------------------------------------*/
/* Fonction affichage text sur afficheur 32x8.				  */
/*------------------------------------------------------------------------------*/
void aff_32x8_txt(int x, char text[])
{
	HT1632.drawTarget(BUFFER_BOARD(1));
	HT1632.clear() ;
	HT1632.drawText(text, x, 1, FONT_5X7, FONT_5X7_WIDTH, FONT_5X7_HEIGHT, FONT_5X7_STEP_GLYPH) ;   // x ,y à partir de gauche, haut
	HT1632.render() ;  
}

/*------------------------------------------------------------------------------*/
void aff_32x8_defilltxt(char text[])
{
	int i ;
	int decalage = 0 ;
	nbpixels = HT1632.getTextWidth(text, FONT_5X7_WIDTH, FONT_5X7_HEIGHT) ; 
	for(i = 0; i <= (nbpixels + OUT_SIZE * 2) * 2; i++)
	{
		HT1632.drawTarget(BUFFER_BOARD(1));
		HT1632.clear() ;
		HT1632.drawText(text, 2*OUT_SIZE - decalage, 1, FONT_5X7, FONT_5X7_WIDTH, FONT_5X7_HEIGHT, FONT_5X7_STEP_GLYPH);
		HT1632.render();  

		decalage = (decalage + 1) % (nbpixels + OUT_SIZE * 2) ;  	// OUT_SIZE = 32
		delay(30) ;
	}
}


/*------------------------------------------------------------------------------*/
/* Fonctions buzzer: Beep: Active buzzer pendant periode ms			*/
/*------------------------------------------------------------------------------*/
void beep(int periode)
{
    int res ;
    char cmd ;

    digitalWrite(BUZZERPIN, HIGH);     	// Active buzzer,
    delay(periode);		    	// Pendant periode,
    digitalWrite(BUZZERPIN, LOW);      	// Eteint buzzer.
}


/*------------------------------------------------------------------------------*/
/* Fonctions remplace codes UTF accents + '°' par les code ascii		*/
/*------------------------------------------------------------------------------*/
int suppAccentUTF8Str(char *chaine)
{
        char *accentptr ;
        char ch[255]  ;
        int i ;

        // $ echo "àçéèëêïîöôùüû°" | hexdump -C
        char codesUTF8AccentTab[14][3] = {"\xc3\xa0", "\xc3\xa7", "\xc3\xa9","\xc3\xa8", "\xc3\xab", "\xc3\xaa", "\xc3\xaf", "\xc3\xae", "\xc3\xb6", "\xc3\xb4", "\xc3\xb9", "\xc3\xbc", "\xc3\xbb", "\xc2\xb0"} ;
        char carSansAccentTab[14][2] = {"a", "c", "e", "e", "e", "e", "i", "i", "o", "o", "u", "u", "u", "&"} ;		// '&' remplace le '°'

        for(i=0; i<14; i++)
        {
                accentptr = strstr(chaine, codesUTF8AccentTab[i]) ;
                if (accentptr != NULL)
                {
                        memset(ch, '\0', sizeof(ch));
                        strncpy(ch, chaine, accentptr - chaine) ;
                        strcat(ch, carSansAccentTab[i]) ;
                        strcat(ch, accentptr + 2 ) ;
                        strcpy(chaine, ch) ;
                }
        }
        return 0 ;
}



/*------------------------------------------------------------------------------*/
/* Main									 */
/*------------------------------------------------------------------------------*/
void loop() 
{
	if ( secondes_courantes != second() )
	{
		secondes_courantes = second() ;
	
		if ( secondes_courantes == 0)
		{
			sprintf( time_str, "%d:%02d", hour(), minute() ) ; 
			Serial.print(F("Local Time: "));    	
			Serial.println(time_str);    	
			aff_32x8_txt(3, time_str) ;
			if ( minute() == 0 ) beep(40) ;					// Si mn à 0, beep.
			
			if ( (minute() % 5) == 0 )					// Toutes les 5mn
			{
				luminosity = ((1024 - analogRead(0)) / 64) + 1 ;	// analogRead retourne int 0 to 1023
				if (luminosity == 17) luminosity = 16 ;
				Serial.print(F("Luminosity: "));    	
				Serial.println(luminosity);    	
				HT1632.setBrightness(luminosity) ;			// Modifie la luminosité de l'afficheur en fonction de celle ambiamte.
				if ( year() == 1970 ) 					// Vérifie toutes les 5mn si à la bonne date (cas pb serveur ntp au reboot).
				{
					//Ethernet.maintain(); 				// dhcp renew
					majTimeNTP() ;
				}
			}
		}
		else if ( secondes_courantes == 50)
		{
			if ( (hour() == 2 || hour() == 11) && minute() == 59 ) { beep(30) ; majTimeNTP() ; }
		}
	}

	if ( GetRxCmd(packetBuffer2) ) ExecDisplayCmd(packetBuffer2) ;
}

